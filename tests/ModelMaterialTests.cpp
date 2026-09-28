// How RaylibRenderBackend draws a model's materials, tested on the GPU: each case
// writes a small glTF file, loads it through the backend, draws it into a 64x64
// render texture under a hidden window, and reads the pixels back.
//
// THE ONE LIGHT. Every case lights its model with a white ambient at intensity 1
// and nothing else -- no lights, no environment -- because that is the one
// arrangement whose output is easy to work by hand. From pbr.fs:
//
//     albedo  = (colDiffuse * vertexColour [* texel])^2.2      linearised sRGB
//     colour  = ambientColour * ambient * albedo * ao          = albedo, here
//     colour  = colour / (colour + 1)                          Reinhard
//     output  = colour^(1/2.2)                                 gamma
//
// so a fully saturated channel (albedo 1.0) comes out as
//
//     1/(1+1) = 0.5;   0.5^(1/2.2) = e^(ln 0.5 / 2.2) = e^(-0.693147/2.2)
//             = e^(-0.315067) = 0.740818 * 0.985046 = 0.729740
//     0.729740 * 255 = 186.08  ->  186
//
// and a zero channel as 0. That is LIT_FULL below. Unlit, the same channel is
// 255: raylib's default shader writes the colour as it is. So 186 against 255
// says whether a surface went through the lit shader at all.
//
// THE PICTURE. An orthographic camera at z = 5 looking down -z, fovy 4, onto a
// 64x64 target: world x in [-2, 2] maps onto pixels 0..64, 16 pixels per metre,
// with +x to the right and y = 0 on row 32. Every model is a 2 m x 2 m quad (or
// two 1 m halves) in the z = 0 plane facing +z, so it covers pixels 16..48.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/MeshData.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <Backends/RenderPass.hpp>
#include <Math/BriteMath.hpp>
#include <gtest/gtest.h>

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
// OpenGL 1.1, exported by opengl32.dll itself: the one way to ask whether a
// texture name is still alive without reaching into the backend.
extern "C" __declspec(dllimport) unsigned char __stdcall glIsTexture(unsigned int texture);
// And the rest of what reading a texture's state back takes, all OpenGL 1.1.
extern "C" __declspec(dllimport) void __stdcall glBindTexture(unsigned int target, unsigned int texture);
extern "C" __declspec(dllimport) void __stdcall glGetIntegerv(unsigned int pname, int* params);
extern "C" __declspec(dllimport) void __stdcall glGetFloatv(unsigned int pname, float* params);
extern "C" __declspec(dllimport) void __stdcall glGetTexParameteriv(unsigned int target, unsigned int pname,
                                                                    int* params);
extern "C" __declspec(dllimport) void __stdcall glGetTexParameterfv(unsigned int target, unsigned int pname,
                                                                    float* params);
extern "C" __declspec(dllimport) void __stdcall glGetTexLevelParameteriv(unsigned int target, int level,
                                                                         unsigned int pname, int* params);
#endif

namespace fs = std::filesystem;
using BRITE::Backends::Raylib::RaylibRenderBackend;

namespace {

constexpr int SIZE = 64;
constexpr int ROW = 32;
constexpr int LIT_FULL = 186; // worked in the header comment
constexpr int TOLERANCE = 2;  // a GPU's rounding to 8 bits, and nothing more

// ---------------------------------------------------------------------------
// A glTF writer, just big enough: quads in the z = 0 plane, each with its own
// material, optionally one base-colour texture from a PNG beside the file.
// ---------------------------------------------------------------------------

struct Quad {
    float x0, x1;            // the quad spans x0..x1 and y -1..1
    int material = -1;       // index into Materials, or -1 for none
    float uvScale = 1.0f;    // how many times a texture spans the whole picture's quad range
    bool facingAway = false; // wound and normalled to face -z: the camera sees its back
};

struct GltfMaterial {
    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool textured = false;
    const char* alphaMode = nullptr; // written only when set
    float alphaCutoff = -1.0f;       // written only when 0 or more
    bool doubleSided = false;        // written only when true
};

struct Scene {
    std::vector<Quad> Quads;
    std::vector<GltfMaterial> Materials;
};

class TempDir {
  public:
    TempDir() {
        std::random_device rd;
        m_path = fs::temp_directory_path() / ("brite_model_material_tests_" + std::to_string(rd()));
        fs::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    const fs::path& Path() const {
        return m_path;
    }

  private:
    fs::path m_path;
};

template <typename T> void Append(std::vector<unsigned char>& bin, const T& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    bin.insert(bin.end(), bytes, bytes + sizeof(T));
}

// Writes dir/model.gltf and dir/model.bin, and returns the .gltf's path. A
// textured material samples dir/texture.png, which the caller writes.
fs::path WriteGltf(const fs::path& dir, const Scene& scene) {
    std::vector<unsigned char> bin;
    std::string views, accessors, primitives;
    int index = 0;
    auto addView = [&](std::size_t offset, std::size_t length, int count, int componentType, const char* type) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}",
                      views.empty() ? "" : ",", offset, length);
        views += buf;
        std::snprintf(buf, sizeof(buf), "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%d,\"type\":\"%s\"}",
                      accessors.empty() ? "" : ",", index, componentType, count, type);
        accessors += buf;
        return index++;
    };

    for (const Quad& q : scene.Quads) {
        const float xs[4] = {q.x0, q.x1, q.x1, q.x0};
        const float ys[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        std::size_t start = bin.size();
        for (int i = 0; i < 4; ++i) {
            Append(bin, xs[i]);
            Append(bin, ys[i]);
            Append(bin, 0.0f);
        }
        const int position = addView(start, bin.size() - start, 4, 5126, "VEC3");
        start = bin.size();
        for (int i = 0; i < 4; ++i) {
            Append(bin, 0.0f);
            Append(bin, 0.0f);
            Append(bin, q.facingAway ? -1.0f : 1.0f);
        }
        const int normal = addView(start, bin.size() - start, 4, 5126, "VEC3");
        // u runs with world x across the whole picture's quad range, -1..1 -> 0..1,
        // times the quad's uvScale.
        start = bin.size();
        for (int i = 0; i < 4; ++i) {
            Append(bin, (xs[i] + 1.0f) * 0.5f * q.uvScale);
            Append(bin, (1.0f - ys[i]) * 0.5f * q.uvScale);
        }
        const int texcoord = addView(start, bin.size() - start, 4, 5126, "VEC2");
        start = bin.size();
        // Counter-clockwise seen from +z, or, facing away, from -z.
        const std::vector<unsigned short> order = q.facingAway ? std::vector<unsigned short>{0, 2, 1, 0, 3, 2}
                                                               : std::vector<unsigned short>{0, 1, 2, 0, 2, 3};
        for (unsigned short i : order)
            Append(bin, i);
        const int indices = addView(start, bin.size() - start, 6, 5123, "SCALAR");
        while (bin.size() % 4)
            bin.push_back(0);

        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "%s{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"TEXCOORD_0\":%d},\"indices\":%d",
                      primitives.empty() ? "" : ",", position, normal, texcoord, indices);
        primitives += buf;
        if (q.material >= 0)
            primitives += ",\"material\":" + std::to_string(q.material);
        primitives += "}";
    }

    std::string materials;
    bool anyTexture = false;
    for (const GltfMaterial& m : scene.Materials) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s{\"pbrMetallicRoughness\":{\"baseColorFactor\":[%g,%g,%g,%g]%s}",
                      materials.empty() ? "" : ",", m.rgba[0], m.rgba[1], m.rgba[2], m.rgba[3],
                      m.textured ? ",\"baseColorTexture\":{\"index\":0}" : "");
        materials += buf;
        if (m.alphaMode)
            materials += std::string(",\"alphaMode\":\"") + m.alphaMode + "\"";
        if (m.alphaCutoff >= 0.0f) {
            std::snprintf(buf, sizeof(buf), ",\"alphaCutoff\":%g", m.alphaCutoff);
            materials += buf;
        }
        if (m.doubleSided)
            materials += ",\"doubleSided\":true";
        materials += "}";
        anyTexture = anyTexture || m.textured;
    }

    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                       "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[" +
                       primitives + "]}],\"accessors\":[" + accessors + "],\"bufferViews\":[" + views +
                       "],\"buffers\":[{\"uri\":\"model.bin\",\"byteLength\":" + std::to_string(bin.size()) + "}]";
    if (!materials.empty())
        json += ",\"materials\":[" + materials + "]";
    if (anyTexture)
        json += ",\"images\":[{\"uri\":\"texture.png\"}],\"textures\":[{\"source\":0}]";
    json += "}";

    std::ofstream(dir / "model.bin", std::ios::binary).write(reinterpret_cast<const char*>(bin.data()), bin.size());
    std::ofstream(dir / "model.gltf") << json;
    return dir / "model.gltf";
}

// An 8x1 PNG: the left four texels one colour, the right four another.
void WriteHalvesPng(const fs::path& path, ::Color left, ::Color right) {
    ::Image image = ::GenImageColor(8, 1, right);
    for (int x = 0; x < 4; ++x)
        ::ImageDrawPixel(&image, x, 0, left);
    ::ExportImage(image, path.string().c_str());
    ::UnloadImage(image);
}

// A size x size checker of single texels: white where x + y is even, black
// where it is odd -- so any 2 x 2 block of it is half white and half black.
void WriteCheckerPng(const fs::path& path, int size) {
    ::Image image = ::GenImageChecked(size, size, 1, 1, {255, 255, 255, 255}, {0, 0, 0, 255});
    ::ExportImage(image, path.string().c_str());
    ::UnloadImage(image);
}

// A quad built in code, spanning x0..x1 and y -1..1 at depth z, facing +z,
// every vertex one colour.
BRITE::MeshData QuadMesh(float x0, float x1, BRITE::Color colour, float z = 0.0f) {
    BRITE::MeshData mesh;
    mesh.Positions = {{x0, -1.0f, z}, {x1, -1.0f, z}, {x1, 1.0f, z}, {x0, 1.0f, z}};
    mesh.Normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    mesh.Colors = {colour, colour, colour, colour};
    mesh.Indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

// ---------------------------------------------------------------------------

class ModelMaterialTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(SIZE, SIZE, "BRITE model material tests");
    }
    static void TearDownTestSuite() {
        ::CloseWindow();
    }

    void SetUp() override {
        m_target = m_backend.LoadRenderTexture(SIZE, SIZE);
    }
    void TearDown() override {
        m_backend.UnloadRenderTexture(m_target);
    }

    // Draws the model under the one light and returns the picture, top row first.
    std::vector<BRITE::Color> Draw(BRITE::ModelHandle model, const BRITE::PBRMaterial& material = {}) {
        BRITE::RenderPass pass;
        pass.AmbientColor = {255, 255, 255, 255};
        pass.AmbientIntensity = 1.0f;
        return DrawPass(pass, {{model, material}});
    }

    // Draws each model, at the origin, in one pass lit as `pass` says, and
    // returns the picture. The camera, target and clear colour are the file's.
    std::vector<BRITE::Color> DrawPass(BRITE::RenderPass pass,
                                       const std::vector<std::pair<BRITE::ModelHandle, BRITE::PBRMaterial>>& models) {
        BRITE::Camera3D camera{
            {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4.0f, BRITE::CameraProjection::Orthographic};
        pass.TargetFramebuffer = m_target;
        pass.ClearColor = {0, 0, 0, 255};
        pass.Camera3DPtr = &camera;

        for (const auto& [model, material] : models) {
            BRITE::ModelDrawCommand draw;
            draw.Model = model;
            draw.Material = material;
            draw.Position = {0.0f, 0.0f, 0.0f};
            draw.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            draw.Scale = {1.0f, 1.0f, 1.0f};
            pass.ModelCommands.push_back(draw);
        }
        m_backend.SubmitRenderPass(pass);

        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(m_target, width, height, pixels));
        EXPECT_EQ(width, SIZE);
        EXPECT_EQ(height, SIZE);
        return pixels;
    }

    RaylibRenderBackend m_backend;
    BRITE::TextureHandle m_target = BRITE::NullTextureHandle;
    TempDir m_dir;
};

// The pixel under world x on the middle row: pixel = (x + 2) * 16.
BRITE::Color At(const std::vector<BRITE::Color>& pixels, int column) {
    return pixels[static_cast<std::size_t>(ROW) * SIZE + column];
}

void ExpectColour(BRITE::Color actual, int r, int g, int b, const char* what) {
    EXPECT_NEAR(actual.r, r, TOLERANCE) << what;
    EXPECT_NEAR(actual.g, g, TOLERANCE) << what;
    EXPECT_NEAR(actual.b, b, TOLERANCE) << what;
}

// World x = -0.5 and +0.5: (1.5 * 16, 2.5 * 16) = pixels 24 and 40.
constexpr int LEFT_HALF = 24;
constexpr int RIGHT_HALF = 40;

// For the textured quad, u = (x + 1) / 2 and the texture is 8 texels wide, red
// on texels 0-3 and green on 4-7. Pixel 27's centre is x = 27.5/16 - 2 = -0.28125,
// u = 0.359375, texel coordinate 8u - 0.5 = 2.375: between texels 2 and 3, both
// red. Pixel 36's centre is x = +0.28125, u = 0.640625, 8u - 0.5 = 4.625: texels
// 4 and 5, both green. Were the UVs doubled, pixel 27 would read u = 0.71875
// (8u - 0.5 = 5.25, green) and pixel 36 u = 1.28125, wrapping to 0.28125
// (8u - 0.5 = 1.75, red): the two swap, so each sample tells.
constexpr int RED_TEXELS = 27;
constexpr int GREEN_TEXELS = 36;

} // namespace

// Two quads, two materials: red on the left, blue on the right. raylib puts a
// file's materials in slots 1 and 2 and keeps slot 0 for its own default, so a
// backend that lights slot 0 alone draws both of these unlit, at 255.
//
// Mutations: giving the lit shader only to the mesh on slot 0 -> 255, not 186;
// drawing every mesh with slot 0's material -> white (186, 186, 186) on both.
TEST_F(ModelMaterialTest, EveryMaterialOfALoadedModelIsLit) {
    Scene scene;
    scene.Materials = {{{1.0f, 0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f, 1.0f}}};
    scene.Quads = {{-1.0f, 0.0f, 0}, {0.0f, 1.0f, 1}};
    const BRITE::ModelHandle model = m_backend.LoadModel(WriteGltf(m_dir.Path(), scene).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);

    const auto pixels = Draw(model);
    ExpectColour(At(pixels, LEFT_HALF), LIT_FULL, 0, 0, "the red material, lit");
    ExpectColour(At(pixels, RIGHT_HALF), 0, 0, LIT_FULL, "the blue material, lit");
    m_backend.UnloadModel(model);
}

// One quad whose material carries the red|green texture: it is drawn, lit, and
// spans the quad once.
//
// Mutations: the doubled UVs restored in pbr.vs -> the samples swap; the
// "model brought an albedo" flag dropped -> the texture is not sampled and both
// read white (186, 186, 186).
TEST_F(ModelMaterialTest, AModelsOwnTextureIsLitAndSpansItsUVsOnce) {
    WriteHalvesPng(m_dir.Path() / "texture.png", {255, 0, 0, 255}, {0, 255, 0, 255});
    Scene scene;
    GltfMaterial textured;
    textured.textured = true;
    scene.Materials = {textured};
    scene.Quads = {{-1.0f, 1.0f, 0}};
    const BRITE::ModelHandle model = m_backend.LoadModel(WriteGltf(m_dir.Path(), scene).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);

    const auto pixels = Draw(model);
    ExpectColour(At(pixels, RED_TEXELS), LIT_FULL, 0, 0, "the texture's red half");
    ExpectColour(At(pixels, GREEN_TEXELS), 0, LIT_FULL, 0, "the texture's green half");
    m_backend.UnloadModel(model);
}

// A texture named by the draw command replaces the model's own -- for that draw
// only. The next draw, naming none, shows the model's texture again.
//
// Mutations: the command's albedo not applied -> the first draw shows red and
// green; the command's texture written into the model's own material instead of
// a copy -> the second draw is still blue.
TEST_F(ModelMaterialTest, ADrawCommandsTextureReplacesTheModelsOwnForThatDrawOnly) {
    WriteHalvesPng(m_dir.Path() / "texture.png", {255, 0, 0, 255}, {0, 255, 0, 255});
    WriteHalvesPng(m_dir.Path() / "blue.png", {0, 0, 255, 255}, {0, 0, 255, 255});
    Scene scene;
    GltfMaterial textured;
    textured.textured = true;
    scene.Materials = {textured};
    scene.Quads = {{-1.0f, 1.0f, 0}};
    const BRITE::ModelHandle model = m_backend.LoadModel(WriteGltf(m_dir.Path(), scene).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);
    const BRITE::TextureHandle blue = m_backend.LoadTexture((m_dir.Path() / "blue.png").string().c_str());

    BRITE::PBRMaterial overridden;
    overridden.AlbedoMap = blue;
    const auto withBlue = Draw(model, overridden);
    ExpectColour(At(withBlue, RED_TEXELS), 0, 0, LIT_FULL, "the command's blue, over the red half");
    ExpectColour(At(withBlue, GREEN_TEXELS), 0, 0, LIT_FULL, "the command's blue, over the green half");

    const auto after = Draw(model);
    ExpectColour(At(after, RED_TEXELS), LIT_FULL, 0, 0, "the model's own red, back again");
    ExpectColour(At(after, GREEN_TEXELS), 0, LIT_FULL, 0, "the model's own green, back again");

    m_backend.UnloadTexture(blue);
    m_backend.UnloadModel(model);
}

// A mesh built in code with texture coordinates shows a texture once across
// them. The quad spans x -1..1 and y -1..1 with u = (x + 1) / 2 and
// v = (1 - y) / 2, the same mapping the glTF quads above use, so the red|green
// texture named by the draw lands exactly as it does on a loaded model: red at
// pixel 27, green at pixel 36, each lit to 186 (worked beside RED_TEXELS).
//
// Mutations: the coordinates not uploaded -> every fragment samples (0, 0), red,
// so pixel 36 reads red; u and v swapped on upload -> the middle row's u is
// v = 0.5156, texel 4, green, so pixel 27 reads green.
TEST_F(ModelMaterialTest, AModelBuiltInCodeShowsATextureOnceAcrossItsTexCoords) {
    WriteHalvesPng(m_dir.Path() / "halves.png", {255, 0, 0, 255}, {0, 255, 0, 255});
    BRITE::MeshData mesh = QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255});
    for (const BRITE::Vector3& p : mesh.Positions)
        mesh.TexCoords.push_back({(p.x + 1.0f) * 0.5f, (1.0f - p.y) * 0.5f});
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
    ASSERT_NE(model, BRITE::NullModelHandle);
    const BRITE::TextureHandle halves = m_backend.LoadTexture((m_dir.Path() / "halves.png").string().c_str());

    BRITE::PBRMaterial textured;
    textured.AlbedoMap = halves;
    const auto pixels = Draw(model, textured);
    ExpectColour(At(pixels, RED_TEXELS), LIT_FULL, 0, 0, "the texture's red half");
    ExpectColour(At(pixels, GREEN_TEXELS), 0, LIT_FULL, 0, "the texture's green half");

    m_backend.UnloadTexture(halves);
    m_backend.UnloadModel(model);
}

// A model built in code has no material of its own: its colour is its vertex
// colours times the draw command's tint. Yellow (1, 1, 0) vertices under a cyan
// (0, 1, 1) tint multiply to green (0, 1, 0), so the lit result is (0, 186, 0).
//
// Mutation: the tint left out of the per-mesh colour -> (186, 186, 0).
TEST_F(ModelMaterialTest, AModelBuiltInCodeIsItsVertexColourTimesTheTintLit) {
    BRITE::MeshData mesh;
    mesh.Positions = {{-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}};
    mesh.Normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    mesh.Colors = {{255, 255, 0, 255}, {255, 255, 0, 255}, {255, 255, 0, 255}, {255, 255, 0, 255}};
    mesh.Indices = {0, 1, 2, 0, 2, 3};
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
    ASSERT_NE(model, BRITE::NullModelHandle);

    BRITE::PBRMaterial cyan;
    cyan.AlbedoTint = {0, 255, 255, 255};
    const auto pixels = Draw(model, cyan);
    ExpectColour(At(pixels, LEFT_HALF), 0, LIT_FULL, 0, "yellow vertices under a cyan tint");
    ExpectColour(At(pixels, RIGHT_HALF), 0, LIT_FULL, 0, "yellow vertices under a cyan tint");
    m_backend.UnloadModel(model);
}

// Unloading a model frees the texture its file brought. Counted as live GL
// texture names before loading, after loading, and after unloading: the model
// adds exactly one (its texture) and unloading must take exactly that one away.
//
// Mutation: the texture loop in UnloadModel removed -> one name still alive.
TEST_F(ModelMaterialTest, UnloadingAModelFreesTheTextureItsFileBrought) {
#if defined(_WIN32)
    auto liveTextures = [] {
        int count = 0;
        for (unsigned int id = 1; id < 4096; ++id)
            count += glIsTexture(id) ? 1 : 0;
        return count;
    };
    WriteHalvesPng(m_dir.Path() / "texture.png", {255, 0, 0, 255}, {0, 255, 0, 255});
    Scene scene;
    GltfMaterial textured;
    textured.textured = true;
    scene.Materials = {textured};
    scene.Quads = {{-1.0f, 1.0f, 0}};

    const int before = liveTextures();
    const BRITE::ModelHandle model = m_backend.LoadModel(WriteGltf(m_dir.Path(), scene).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);
    Draw(model);
    EXPECT_EQ(liveTextures(), before + 1) << "the file's one texture";
    m_backend.UnloadModel(model);
    EXPECT_EQ(liveTextures(), before) << "and nothing left of it";
#else
    GTEST_SKIP() << "counts texture names through opengl32's glIsTexture";
#endif
}

// ---------------------------------------------------------------------------
// Unlit materials. An unlit surface writes its albedo -- tint times vertex
// colour times albedo texture -- exactly as authored, so its expected pixels are
// the authored bytes themselves, with no light to work through.
// ---------------------------------------------------------------------------

// Under no light and no ambient a lit white quad is black -- the ambient term is
// 0 * albedo and there are no lights, so the colour is 0 before and after the
// tone map -- while an unlit one is white, 255.
//
// Mutations: the unlit uniform never set -> 0, not 255; the shader's unlit
// branch removed -> 0.
TEST_F(ModelMaterialTest, AnUnlitModelDrawsItsColourUnderNoLight) {
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255}));
    ASSERT_NE(model, BRITE::NullModelHandle);
    BRITE::RenderPass dark; // no lights; AmbientIntensity 0 by default

    const auto lit = DrawPass(dark, {{model, {}}});
    ExpectColour(At(lit, LEFT_HALF), 0, 0, 0, "lit, in the dark");

    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;
    const auto pixels = DrawPass(dark, {{model, unlit}});
    ExpectColour(At(pixels, LEFT_HALF), 255, 255, 255, "unlit, in the dark");
    ExpectColour(At(pixels, RIGHT_HALF), 255, 255, 255, "unlit, in the dark");
    m_backend.UnloadModel(model);
}

// An unlit surface is its tint times its vertex colour, whatever the light.
// Vertices (200, 200, 255) under a tint of (255, 128, 51):
//   red    1.0      * 200/255 = 0.78431 -> 200
//   green  128/255  * 200/255 = 0.50196 * 0.78431 = 0.39369 -> 100.39 -> 100
//   blue   51/255   * 1.0     = 0.2     -> 51
// in the dark, and again under a white ambient of 1 and a white sun of
// intensity 5 shining straight onto the quad. (Lit, the second would saturate
// toward 255 in every channel.)
//
// Mutations: the unlit colour tone mapped (albedo / (albedo + 1)) -> red 112;
// the ambient multiplied in -> black in the dark; the vertex colour left out ->
// (255, 128, 51); the tint left out -> (200, 200, 255).
TEST_F(ModelMaterialTest, AnUnlitModelsColourDoesNotChangeWithTheLightsOrTheAmbient) {
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {200, 200, 255, 255}));
    ASSERT_NE(model, BRITE::NullModelHandle);
    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;
    unlit.AlbedoTint = {255, 128, 51, 255};

    BRITE::RenderPass dark;
    ExpectColour(At(DrawPass(dark, {{model, unlit}}), LEFT_HALF), 200, 100, 51, "unlit, in the dark");

    BRITE::RenderPass bright;
    bright.AmbientColor = {255, 255, 255, 255};
    bright.AmbientIntensity = 1.0f;
    BRITE::Light sun;
    sun.Direction = {0.0f, 0.0f, -1.0f};
    sun.Intensity = 5.0f;
    bright.Lights.push_back(sun);
    ExpectColour(At(DrawPass(bright, {{model, unlit}}), LEFT_HALF), 200, 100, 51, "unlit, in full light");
    m_backend.UnloadModel(model);
}

// A loaded model's own texture is drawn unlit exactly as its texels are: the
// red|green texture reads (255, 0, 0) and (0, 255, 0), against 186 lit.
//
// Mutation: the texture left out of the unlit branch -> white on both.
TEST_F(ModelMaterialTest, AnUnlitModelsTextureIsDrawnAsAuthored) {
    WriteHalvesPng(m_dir.Path() / "texture.png", {255, 0, 0, 255}, {0, 255, 0, 255});
    Scene scene;
    GltfMaterial textured;
    textured.textured = true;
    scene.Materials = {textured};
    scene.Quads = {{-1.0f, 1.0f, 0}};
    const BRITE::ModelHandle model = m_backend.LoadModel(WriteGltf(m_dir.Path(), scene).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);

    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;
    const auto pixels = Draw(model, unlit);
    ExpectColour(At(pixels, RED_TEXELS), 255, 0, 0, "the texture's red half, unlit");
    ExpectColour(At(pixels, GREEN_TEXELS), 0, 255, 0, "the texture's green half, unlit");
    m_backend.UnloadModel(model);
}

// Unlit is per draw: an unlit quad on the left, drawn first, and a lit one on
// the right in the same pass, under the one light. The left reads 255 and the
// right 186 -- the lit one is not left unlit by the one before it.
//
// Mutation: the unlit uniform set only when a material is unlit -> the right
// reads 255.
TEST_F(ModelMaterialTest, AnUnlitMeshLeavesTheNextMeshLit) {
    const BRITE::ModelHandle left = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 0.0f, {255, 255, 255, 255}));
    const BRITE::ModelHandle right = m_backend.LoadModelFromMesh(QuadMesh(0.0f, 1.0f, {255, 255, 255, 255}));
    ASSERT_NE(left, BRITE::NullModelHandle);
    ASSERT_NE(right, BRITE::NullModelHandle);
    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;
    BRITE::RenderPass pass;
    pass.AmbientColor = {255, 255, 255, 255};
    pass.AmbientIntensity = 1.0f;

    const auto pixels = DrawPass(pass, {{left, unlit}, {right, {}}});
    ExpectColour(At(pixels, LEFT_HALF), 255, 255, 255, "unlit, drawn first");
    ExpectColour(At(pixels, RIGHT_HALF), LIT_FULL, LIT_FULL, LIT_FULL, "lit, drawn after it");
    m_backend.UnloadModel(left);
    m_backend.UnloadModel(right);
}

// ---------------------------------------------------------------------------
// Distance fog. Every case draws a white quad at z = 0, lit by the one light,
// and reads the centre pixel (32, 32). Its centre is x = 32.5/16 - 2 = 0.03125,
// y = 2 - 32.5/16 = -0.03125, so its distance from the camera at (0, 0, 5) is
//
//     sqrt(0.03125^2 + 0.03125^2 + 5^2) = sqrt(25.001953) = 5.000195
//
// -- 5 m, to well inside a GPU's rounding. Unfogged the pixel is the file's
// lit white, 0.729740 in every channel (186). The fog colour is (100, 200, 50),
// which is (0.392157, 0.784314, 0.196078), and the fade is applied to the
// finished colour, so a fully fogged pixel is exactly that colour.
// ---------------------------------------------------------------------------

constexpr int CENTRE = 32;

BRITE::RenderPass FoggedPass(float start, float end) {
    BRITE::RenderPass pass;
    pass.AmbientColor = {255, 255, 255, 255};
    pass.AmbientIntensity = 1.0f;
    pass.Fog.Enabled = true;
    pass.Fog.Tint = {100, 200, 50, 255};
    pass.Fog.Start = start;
    pass.Fog.End = end;
    return pass;
}

// Nearer than Start the surface is as drawn: start 6 m, end 10 m, and the quad
// at 5 m reads the lit white, 186.
//
// Mutation: the clamp's lower bound dropped -> the fade is (5 - 6) / 4 = -0.25
// and mix() runs backwards: 0.729740 + 0.25 * (0.729740 - 0.392157) = 0.814136,
// red 208.
TEST_F(ModelMaterialTest, FogLeavesASurfaceNearerThanItsStartAsDrawn) {
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255}));
    ASSERT_NE(model, BRITE::NullModelHandle);
    ExpectColour(At(DrawPass(FoggedPass(6.0f, 10.0f), {{model, {}}}), CENTRE), LIT_FULL, LIT_FULL, LIT_FULL,
                 "5 m, fog from 6 m");
    m_backend.UnloadModel(model);
}

// Beyond End the surface is the fog colour: start 1 m, end 4 m, and the quad at
// 5 m reads (100, 200, 50).
//
// Mutation: the clamp's upper bound dropped -> the fade is (5 - 1) / 3 = 1.333
// and runs past the fog colour: red 0.729740 + 1.333 * (0.392157 - 0.729740) =
// 0.279629, 71.
TEST_F(ModelMaterialTest, FogTurnsASurfaceBeyondItsEndToTheFogColour) {
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255}));
    ASSERT_NE(model, BRITE::NullModelHandle);
    ExpectColour(At(DrawPass(FoggedPass(1.0f, 4.0f), {{model, {}}}), CENTRE), 100, 200, 50, "5 m, fog to 4 m");
    m_backend.UnloadModel(model);
}

// Halfway between Start and End the surface is halfway to the fog colour. Start
// 3 m, end 7 m: the fade is (5.000195 - 3) / 4 = 0.500049, and each channel is
// lit + 0.500049 * (fog - lit):
//   red    0.729740 + 0.500049 * (0.392157 - 0.729740) = 0.560932 -> 143.04 -> 143
//   green  0.729740 + 0.500049 * (0.784314 - 0.729740) = 0.757030 -> 193.04 -> 193
//   blue   0.729740 + 0.500049 * (0.196078 - 0.729740) = 0.462883 -> 118.04 -> 118
//
// Mutations: the distance measured from the origin instead of the camera ->
// about 0 m, unfogged, 186; the two mixed in linear light instead of on the
// finished colour -> red 0.5 (the lit white, linear) and 0.392157^2.2 = 0.127530
// mix to 0.313765, which encodes as 0.313765^(1/2.2) = 0.590430, 151.
TEST_F(ModelMaterialTest, FogHalfwayBetweenItsStartAndEndIsHalfwayToTheFogColour) {
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255}));
    ASSERT_NE(model, BRITE::NullModelHandle);
    ExpectColour(At(DrawPass(FoggedPass(3.0f, 7.0f), {{model, {}}}), CENTRE), 143, 193, 118, "5 m, fog 3 m to 7 m");
    m_backend.UnloadModel(model);
}

// An End at or before Start is a hard edge at Start. Start 3 m, end 1 m: the
// quad at 5 m is beyond the start, fully fogged. Start 6 m, end 4 m: the quad is
// nearer than the start, as drawn -- though it lies between the two numbers.
//
// Mutation: the floor on the span dropped -> (5 - 3) / (1 - 3) = -1, clamped to
// 0, unfogged; and (5 - 6) / (4 - 6) = 0.5, half fogged.
TEST_F(ModelMaterialTest, FogWithItsEndBeforeItsStartIsAHardEdgeAtTheStart) {
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255}));
    ASSERT_NE(model, BRITE::NullModelHandle);
    ExpectColour(At(DrawPass(FoggedPass(3.0f, 1.0f), {{model, {}}}), CENTRE), 100, 200, 50, "5 m, fog from 3 m");
    ExpectColour(At(DrawPass(FoggedPass(6.0f, 4.0f), {{model, {}}}), CENTRE), LIT_FULL, LIT_FULL, LIT_FULL,
                 "5 m, fog from 6 m");
    m_backend.UnloadModel(model);
}

// Unlit materials are not fogged: an unlit white quad beyond the fog's end
// still reads 255.
//
// Mutation: the fog applied to the unlit branch as well -> (100, 200, 50).
TEST_F(ModelMaterialTest, FogLeavesAnUnlitSurfaceAsAuthored) {
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255}));
    ASSERT_NE(model, BRITE::NullModelHandle);
    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;
    ExpectColour(At(DrawPass(FoggedPass(1.0f, 4.0f), {{model, unlit}}), CENTRE), 255, 255, 255, "unlit, 5 m");
    m_backend.UnloadModel(model);
}

// Fog is off unless a pass asks for it -- and a pass that does leaves the next
// one clear. A pass with the fully fogging settings but Enabled false reads 186;
// so does a default pass drawn straight after a fogged one.
//
// Mutations: the shader ignoring fogEnabled -> the first reads the fog colour;
// the fog uniforms set only when a pass enables them -> the last reads it.
TEST_F(ModelMaterialTest, FogIsOffUnlessAPassAsksAndDoesNotOutliveThatPass) {
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255}));
    ASSERT_NE(model, BRITE::NullModelHandle);
    BRITE::RenderPass disabled = FoggedPass(1.0f, 4.0f);
    disabled.Fog.Enabled = false;
    ExpectColour(At(DrawPass(disabled, {{model, {}}}), CENTRE), LIT_FULL, LIT_FULL, LIT_FULL, "fog not enabled");

    ExpectColour(At(DrawPass(FoggedPass(1.0f, 4.0f), {{model, {}}}), CENTRE), 100, 200, 50, "a fogged pass");
    ExpectColour(At(Draw(model), CENTRE), LIT_FULL, LIT_FULL, LIT_FULL, "the default pass after it");
    m_backend.UnloadModel(model);
}

// ---------------------------------------------------------------------------
// Texture sampling. The pixel cases draw unlit, so a pixel is its texel's bytes
// with no light to work through.
//
// THE DISTANT CHECKER. A 2 x 2 checker repeated 48 times across the 2 m quad:
// 96 texels over 32 pixels, three texels to a pixel. At pixel column c the
// quad's u is 24 * (x + 1) with x = (c + 0.5)/16 - 2, so the texel coordinate is
//
//     s = 2u = 48 * ((c + 0.5)/16 - 1) = 3c - 46.5
//
// and the same in rows. Every sample sits on a texel CENTRE (k + 0.5), so the
// point and bilinear filters each return one whole texel, 0 or 255, and never a
// blend. Mipmaps do: three texels a pixel is a level of detail of log2(3) =
// 1.58, beyond the checker's last level, 1, which is one texel -- the mean of
// the four, (255 + 0 + 0 + 255)/4 = 127.5, stored as 127 or 128.
// ---------------------------------------------------------------------------

constexpr int CHECKER_REPEATS = 48;

// The quad's inside, clear of its edges: pixels 18..45 each way.
bool EveryInsidePixel(const std::vector<BRITE::Color>& pixels, const std::function<bool(int)>& test) {
    for (int row = 18; row <= 45; ++row)
        for (int column = 18; column <= 45; ++column)
            if (!test(pixels[static_cast<std::size_t>(row) * SIZE + column].r))
                return false;
    return true;
}
bool AnyInsidePixel(const std::vector<BRITE::Color>& pixels, int value) {
    return !EveryInsidePixel(pixels, [value](int r) { return r != value; });
}
bool IsAverage(int r) {
    return r >= 125 && r <= 130; // 127.5, and the GPU's rounding either side
}

// A texture's sampling state, read back from OpenGL itself.
struct GlSampling {
    int MinFilter = -1, MagFilter = -1, WrapS = -1, WrapT = -1;
    int Level1Width = -1; // 0 when the texture has no level 1: no mipmaps
    float Anisotropy = 1.0f;
};

#if defined(_WIN32)
constexpr unsigned int GL_TEXTURE_2D_ = 0x0DE1;
constexpr int GL_NEAREST_ = 0x2600, GL_LINEAR_ = 0x2601, GL_LINEAR_MIPMAP_LINEAR_ = 0x2703;
constexpr int GL_REPEAT_ = 0x2901, GL_CLAMP_TO_EDGE_ = 0x812F;

GlSampling ReadGlSampling(unsigned int id) {
    int previous = 0;
    glGetIntegerv(0x8069 /* GL_TEXTURE_BINDING_2D */, &previous);
    glBindTexture(GL_TEXTURE_2D_, id);
    GlSampling s;
    glGetTexParameteriv(GL_TEXTURE_2D_, 0x2801 /* GL_TEXTURE_MIN_FILTER */, &s.MinFilter);
    glGetTexParameteriv(GL_TEXTURE_2D_, 0x2800 /* GL_TEXTURE_MAG_FILTER */, &s.MagFilter);
    glGetTexParameteriv(GL_TEXTURE_2D_, 0x2802 /* GL_TEXTURE_WRAP_S */, &s.WrapS);
    glGetTexParameteriv(GL_TEXTURE_2D_, 0x2803 /* GL_TEXTURE_WRAP_T */, &s.WrapT);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_, 1, 0x1000 /* GL_TEXTURE_WIDTH */, &s.Level1Width);
    glGetTexParameterfv(GL_TEXTURE_2D_, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, &s.Anisotropy);
    glBindTexture(GL_TEXTURE_2D_, static_cast<unsigned int>(previous));
    return s;
}

// The most anisotropy the device allows; 0 when it has none to offer.
float MaxAnisotropy() {
    float most = 0.0f;
    glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY */, &most);
    return most;
}
#endif

// A texture loads sampled as it always has: the nearest texel both ways, no
// mipmaps, repeating, no anisotropy -- which is what TextureSampling's defaults
// name, so setting the defaults changes nothing.
//
// Mutation: LoadTexture applying trilinear sampling -> the filters read
// GL_LINEAR_MIPMAP_LINEAR and GL_LINEAR, and level 1 exists.
TEST_F(ModelMaterialTest, ATextureLoadsSampledAsItAlwaysHas) {
#if defined(_WIN32)
    WriteCheckerPng(m_dir.Path() / "checker.png", 8);
    const BRITE::TextureHandle texture = m_backend.LoadTexture((m_dir.Path() / "checker.png").string().c_str());
    ASSERT_NE(m_backend.NativeTextureId(texture), 0u);

    const GlSampling loaded = ReadGlSampling(m_backend.NativeTextureId(texture));
    EXPECT_EQ(loaded.MinFilter, GL_NEAREST_);
    EXPECT_EQ(loaded.MagFilter, GL_NEAREST_);
    EXPECT_EQ(loaded.WrapS, GL_REPEAT_);
    EXPECT_EQ(loaded.WrapT, GL_REPEAT_);
    EXPECT_EQ(loaded.Level1Width, 0) << "no mipmaps";
    EXPECT_FLOAT_EQ(loaded.Anisotropy, 1.0f);
    m_backend.UnloadTexture(texture);
#else
    GTEST_SKIP() << "reads texture state through opengl32";
#endif
}

// Each option reaches the texture in OpenGL. An 8 x 8 texture asked for
// trilinear filtering, anisotropy 4 and clamping has a level 1 of 8/2 = 4
// texels, filters GL_LINEAR_MIPMAP_LINEAR and GL_LINEAR, clamps both ways, and
// anisotropy 4 -- or the device's maximum, if that is lower. Asked for bilinear
// and repeat after that, it reads GL_LINEAR both ways and repeats; asked for
// point, GL_NEAREST both ways, even though it now has mipmaps.
//
// Mutations: the mipmaps not generated -> level 1 is 0 wide; the anisotropy not
// passed on -> 1; the wrap set on s alone -> t still repeats; the filters set
// through raylib's SetTextureFilter -> point reads GL_NEAREST_MIPMAP_NEAREST
// once the texture has mipmaps.
TEST_F(ModelMaterialTest, EachSamplingOptionReachesTheTexture) {
#if defined(_WIN32)
    WriteCheckerPng(m_dir.Path() / "checker.png", 8);
    const BRITE::TextureHandle texture = m_backend.LoadTexture((m_dir.Path() / "checker.png").string().c_str());
    const unsigned int id = m_backend.NativeTextureId(texture);
    ASSERT_NE(id, 0u);
    using BRITE::Backends::SamplingFilter;
    using BRITE::Backends::SamplingWrap;

    BRITE::Backends::TextureSampling far;
    far.Filter = SamplingFilter::Trilinear;
    far.Anisotropy = 4;
    far.Wrap = SamplingWrap::Clamp;
    ASSERT_TRUE(m_backend.SetTextureSampling(texture, far));
    const GlSampling trilinear = ReadGlSampling(id);
    EXPECT_EQ(trilinear.Level1Width, 4) << "mipmaps: level 1 is half of 8";
    EXPECT_EQ(trilinear.MinFilter, GL_LINEAR_MIPMAP_LINEAR_);
    EXPECT_EQ(trilinear.MagFilter, GL_LINEAR_);
    EXPECT_EQ(trilinear.WrapS, GL_CLAMP_TO_EDGE_);
    EXPECT_EQ(trilinear.WrapT, GL_CLAMP_TO_EDGE_);
    const float most = MaxAnisotropy();
    if (most >= 1.0f)
        EXPECT_FLOAT_EQ(trilinear.Anisotropy, std::min(4.0f, most));

    BRITE::Backends::TextureSampling smooth;
    smooth.Filter = SamplingFilter::Bilinear;
    ASSERT_TRUE(m_backend.SetTextureSampling(texture, smooth));
    const GlSampling bilinear = ReadGlSampling(id);
    EXPECT_EQ(bilinear.MinFilter, GL_LINEAR_);
    EXPECT_EQ(bilinear.MagFilter, GL_LINEAR_);
    EXPECT_EQ(bilinear.WrapS, GL_REPEAT_);
    EXPECT_EQ(bilinear.WrapT, GL_REPEAT_);
    EXPECT_FLOAT_EQ(bilinear.Anisotropy, 1.0f);

    ASSERT_TRUE(m_backend.SetTextureSampling(texture, {}));
    const GlSampling point = ReadGlSampling(id);
    EXPECT_EQ(point.MinFilter, GL_NEAREST_);
    EXPECT_EQ(point.MagFilter, GL_NEAREST_);
    m_backend.UnloadTexture(texture);
#else
    GTEST_SKIP() << "reads texture state through opengl32";
#endif
}

// A handle that names no plain texture is refused: the null handle, one never
// issued, and a render texture. So is a model handle that names no model.
//
// Mutations: SetTextureSampling returning true for a handle it cannot find ->
// the first two EXPECT_FALSEs fail; SetModelTextureSampling returning true for
// a model it cannot find -> the last fails.
TEST_F(ModelMaterialTest, SamplingAHandleThatNamesNoTextureIsRefused) {
    EXPECT_FALSE(m_backend.SetTextureSampling(BRITE::NullTextureHandle, {}));
    EXPECT_FALSE(m_backend.SetTextureSampling(987654, {}));
    EXPECT_FALSE(m_backend.SetTextureSampling(m_target, {})) << "the render texture";
    EXPECT_FALSE(m_backend.SetModelTextureSampling(987654, {}));
}

// The distant checker of the header above, drawn point-sampled and then
// trilinear. Point: every pixel is a whole texel, 0 or 255, and both appear.
// Trilinear: every pixel is the checker's mean, 127.5.
//
// Mutations: the mipmaps not generated -> a mipmapped filter on a texture with
// one level is incomplete and samples black, 0; trilinear mapped to the bilinear
// filter -> every sample sits on a texel centre and reads 0 or 255.
TEST_F(ModelMaterialTest, ATrilinearTextureAveragesADistantCheckerWherePointSamplingCannot) {
    WriteCheckerPng(m_dir.Path() / "checker.png", 2);
    BRITE::MeshData mesh = QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255});
    for (const BRITE::Vector3& p : mesh.Positions)
        mesh.TexCoords.push_back({(p.x + 1.0f) * 0.5f * CHECKER_REPEATS, (1.0f - p.y) * 0.5f * CHECKER_REPEATS});
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
    ASSERT_NE(model, BRITE::NullModelHandle);
    const BRITE::TextureHandle checker = m_backend.LoadTexture((m_dir.Path() / "checker.png").string().c_str());
    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;
    unlit.AlbedoMap = checker;

    const auto point = Draw(model, unlit);
    EXPECT_TRUE(EveryInsidePixel(point, [](int r) { return r == 0 || r == 255; })) << "point: whole texels";
    EXPECT_TRUE(AnyInsidePixel(point, 0) && AnyInsidePixel(point, 255)) << "point: both colours";

    BRITE::Backends::TextureSampling trilinear;
    trilinear.Filter = BRITE::Backends::SamplingFilter::Trilinear;
    ASSERT_TRUE(m_backend.SetTextureSampling(checker, trilinear));
    const auto averaged = Draw(model, unlit);
    EXPECT_TRUE(EveryInsidePixel(averaged, IsAverage)) << "trilinear: the mean, 127.5";

    m_backend.UnloadTexture(checker);
    m_backend.UnloadModel(model);
}

// Repeat against clamp, on the red|green texture with u = x + 1, running 0..2
// across the quad. Pixel 20 is x = 20.5/16 - 2 = -0.71875, u = 0.28125: texel
// 8 * 0.28125 = 2.25 -> 2, red, either way. Pixel 36 is x = 0.28125, u = 1.28125:
// repeating, the texture starts again at 0.28125, texel 2, red; clamped, u stays
// at the right edge, texel 7, green. A texture no one has set repeats.
//
// Mutations: the wrap never applied -> clamped reads red at 36; Clamp mapped to
// repeat -> the same.
TEST_F(ModelMaterialTest, ClampReadsTheEdgeTexelWhereRepeatReadsTheTextureAgain) {
    WriteHalvesPng(m_dir.Path() / "halves.png", {255, 0, 0, 255}, {0, 255, 0, 255});
    BRITE::MeshData mesh = QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255});
    for (const BRITE::Vector3& p : mesh.Positions)
        mesh.TexCoords.push_back({p.x + 1.0f, (1.0f - p.y) * 0.5f});
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
    ASSERT_NE(model, BRITE::NullModelHandle);
    const BRITE::TextureHandle halves = m_backend.LoadTexture((m_dir.Path() / "halves.png").string().c_str());
    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;
    unlit.AlbedoMap = halves;

    const auto asLoaded = Draw(model, unlit);
    ExpectColour(At(asLoaded, 20), 255, 0, 0, "u 0.28, as loaded");
    ExpectColour(At(asLoaded, 36), 255, 0, 0, "u 1.28, as loaded: repeating");

    BRITE::Backends::TextureSampling clamped;
    clamped.Wrap = BRITE::Backends::SamplingWrap::Clamp;
    ASSERT_TRUE(m_backend.SetTextureSampling(halves, clamped));
    const auto pixels = Draw(model, unlit);
    ExpectColour(At(pixels, 20), 255, 0, 0, "u 0.28, clamped");
    ExpectColour(At(pixels, 36), 0, 255, 0, "u 1.28, clamped to the edge texel");

    m_backend.UnloadTexture(halves);
    m_backend.UnloadModel(model);
}

// A loaded model's own texture takes the sampling asked of the model: the same
// distant checker, this time a glTF quad whose material brought it. Point as
// loaded, 0 or 255; after SetModelTextureSampling with trilinear, 127.5. A model
// built in code, with no texture of its own, is still a model: true.
//
// Mutation: SetModelTextureSampling finding nothing to apply (the ownership
// test inverted) -> the model's checker stays 0 or 255.
TEST_F(ModelMaterialTest, AModelsOwnTexturesTakeTheSamplingAskedOfTheModel) {
    WriteCheckerPng(m_dir.Path() / "texture.png", 2);
    Scene scene;
    GltfMaterial textured;
    textured.textured = true;
    scene.Materials = {textured};
    Quad quad{-1.0f, 1.0f, 0};
    quad.uvScale = CHECKER_REPEATS;
    scene.Quads = {quad};
    const BRITE::ModelHandle model = m_backend.LoadModel(WriteGltf(m_dir.Path(), scene).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);
    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;

    const auto point = Draw(model, unlit);
    EXPECT_TRUE(EveryInsidePixel(point, [](int r) { return r == 0 || r == 255; })) << "as loaded: whole texels";

    BRITE::Backends::TextureSampling trilinear;
    trilinear.Filter = BRITE::Backends::SamplingFilter::Trilinear;
    ASSERT_TRUE(m_backend.SetModelTextureSampling(model, trilinear));
    EXPECT_TRUE(EveryInsidePixel(Draw(model, unlit), IsAverage)) << "trilinear: the mean, 127.5";

    const BRITE::ModelHandle plain = m_backend.LoadModelFromMesh(QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255}));
    EXPECT_TRUE(m_backend.SetModelTextureSampling(plain, trilinear)) << "a model with no textures of its own";
    m_backend.UnloadModel(plain);
    m_backend.UnloadModel(model);
}

// ---------------------------------------------------------------------------
// glTF alpha and sidedness. The alpha cases use the red|green texture with the
// RED half at alpha 102 -- 102/255 = 0.4 -- and the green half opaque, on the
// quad whose texture spans it once: RED_TEXELS samples alpha 0.4, GREEN_TEXELS
// alpha 1.0. Black is the clear colour, so black means "not drawn".
// ---------------------------------------------------------------------------

namespace {
Scene HalfSeeThroughQuad(const char* alphaMode, float alphaCutoff) {
    Scene scene;
    GltfMaterial material;
    material.textured = true;
    material.alphaMode = alphaMode;
    material.alphaCutoff = alphaCutoff;
    scene.Materials = {material};
    scene.Quads = {{-1.0f, 1.0f, 0}};
    return scene;
}
constexpr ::Color SEE_THROUGH_RED = {255, 0, 0, 102};
constexpr ::Color SOLID_GREEN = {0, 255, 0, 255};
} // namespace

// A MASK material cut at 0.5: the red half's 0.4 is below it and is not drawn,
// black; the green half's 1.0 is drawn, lit, (0, 186, 0).
//
// Mutations: the discard removed -> the red half draws; the texture's alpha left
// out of the test -> 1.0 on both halves, nothing cut; the comparison turned
// (alpha > cutoff discarded) -> the green half is cut instead.
TEST_F(ModelMaterialTest, AMaskMaterialCutsOutTexelsBelowItsCutoff) {
    WriteHalvesPng(m_dir.Path() / "texture.png", SEE_THROUGH_RED, SOLID_GREEN);
    const BRITE::ModelHandle model =
        m_backend.LoadModel(WriteGltf(m_dir.Path(), HalfSeeThroughQuad("MASK", 0.5f)).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);

    const auto pixels = Draw(model);
    ExpectColour(At(pixels, RED_TEXELS), 0, 0, 0, "alpha 0.4 under a cutoff of 0.5: not drawn");
    ExpectColour(At(pixels, GREEN_TEXELS), 0, LIT_FULL, 0, "alpha 1.0: drawn");
    m_backend.UnloadModel(model);
}

// The cutoff is the file's own: at 0.3 the red half's 0.4 stays -- and a texel a
// mask keeps is drawn SOLID, (186, 0, 0), not blended at 0.4 over the black
// behind it, which would be 0.4 * 186 = 74.
//
// Mutations: the file's cutoff not read (0.5 always) -> the red half is cut,
// black; the kept fragment's alpha left at 0.4 -> 74.
TEST_F(ModelMaterialTest, AMaskKeepsTexelsAtOrAboveTheFilesCutoffSolid) {
    WriteHalvesPng(m_dir.Path() / "texture.png", SEE_THROUGH_RED, SOLID_GREEN);
    const BRITE::ModelHandle model =
        m_backend.LoadModel(WriteGltf(m_dir.Path(), HalfSeeThroughQuad("MASK", 0.3f)).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);

    const auto pixels = Draw(model);
    ExpectColour(At(pixels, RED_TEXELS), LIT_FULL, 0, 0, "alpha 0.4 over a cutoff of 0.3: drawn, solid");
    m_backend.UnloadModel(model);
}

// An OPAQUE material -- a file that says nothing of alpha -- ignores the
// texture's alpha, as glTF says: the red half draws solid, (186, 0, 0).
//
// Mutation: the texture's alpha multiplied in whatever the mode -> the red half
// blends at 0.4, 74.
TEST_F(ModelMaterialTest, AnOpaqueMaterialIgnoresItsTexturesAlpha) {
    WriteHalvesPng(m_dir.Path() / "texture.png", SEE_THROUGH_RED, SOLID_GREEN);
    const BRITE::ModelHandle model =
        m_backend.LoadModel(WriteGltf(m_dir.Path(), HalfSeeThroughQuad(nullptr, -1.0f)).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);

    const auto pixels = Draw(model);
    ExpectColour(At(pixels, RED_TEXELS), LIT_FULL, 0, 0, "opaque: the texture's alpha ignored");
    m_backend.UnloadModel(model);
}

// A draw can ask for the cut-out on a mesh built in code: masked at 0.5 the red
// half is not drawn. The next draw does not ask, and is translucent -- a tint
// alpha of 128 -- so it blends: the lit red, 0.729740, times 128/255 = 0.501961
// over black is 0.366301, 93. A mask left behind by the first draw would still
// make it solid, 186, even with nothing cut; only a translucent draw shows it.
//
// Mutations: the draw's AlphaMask ignored -> the red half draws the first time;
// the mask uniform set only when a draw asks -> the second draw reads 186.
TEST_F(ModelMaterialTest, ADrawCutsOutAMeshBuiltInCodeForThatDrawOnly) {
    WriteHalvesPng(m_dir.Path() / "halves.png", SEE_THROUGH_RED, SOLID_GREEN);
    BRITE::MeshData mesh = QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255});
    for (const BRITE::Vector3& p : mesh.Positions)
        mesh.TexCoords.push_back({(p.x + 1.0f) * 0.5f, (1.0f - p.y) * 0.5f});
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
    ASSERT_NE(model, BRITE::NullModelHandle);
    const BRITE::TextureHandle halves = m_backend.LoadTexture((m_dir.Path() / "halves.png").string().c_str());

    BRITE::PBRMaterial masked;
    masked.AlbedoMap = halves;
    masked.AlphaMask = true;
    const auto cut = Draw(model, masked);
    ExpectColour(At(cut, RED_TEXELS), 0, 0, 0, "the draw's mask: not drawn");
    ExpectColour(At(cut, GREEN_TEXELS), 0, LIT_FULL, 0, "the draw's mask: drawn");

    BRITE::PBRMaterial translucent;
    translucent.AlbedoMap = halves;
    translucent.AlbedoTint = {255, 255, 255, 128};
    ExpectColour(At(Draw(model, translucent), RED_TEXELS), 93, 0, 0, "the next draw: not masked, and blending");

    m_backend.UnloadTexture(halves);
    m_backend.UnloadModel(model);
}

// An unlit backdrop with a see-through sky is cut out too: the mask comes before
// the unlit colour is written. Unlit, the kept half is its texel, (0, 255, 0).
//
// Mutation: the cut-out placed after the unlit branch's early return -> the red
// half draws, (255, 0, 0).
TEST_F(ModelMaterialTest, AnUnlitMaskedSurfaceIsCutOutToo) {
    WriteHalvesPng(m_dir.Path() / "texture.png", SEE_THROUGH_RED, SOLID_GREEN);
    const BRITE::ModelHandle model =
        m_backend.LoadModel(WriteGltf(m_dir.Path(), HalfSeeThroughQuad("MASK", 0.5f)).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);
    BRITE::PBRMaterial unlit;
    unlit.Unlit = true;

    const auto pixels = Draw(model, unlit);
    ExpectColour(At(pixels, RED_TEXELS), 0, 0, 0, "unlit, below the cutoff: not drawn");
    ExpectColour(At(pixels, GREEN_TEXELS), 0, 255, 0, "unlit, kept: its texel");
    m_backend.UnloadModel(model);
}

// A quad seen from behind. The camera looks down -z; a quad facing away (its
// normal -z, wound counter-clockwise only when seen from -z) shows the camera
// its back. One-sided, it is culled: black. Double-sided, it is drawn -- and
// lit as its front would be. Under a sun travelling -z (from the camera toward
// the scene) and no ambient, a quad FACING the camera has N = +z, V = +z,
// L = +z; the back face with its normal turned to face the viewer has exactly
// the same N, V and L, so every term of the lighting is the same number, and
// the two pixels match. Unturned, its N is -z, N.L is -1, clamped to nothing,
// and it is black.
//
// Mutations: culling left on for a double-sided mesh -> black; the back face's
// normal not turned -> black where the front is lit.
TEST_F(ModelMaterialTest, ADoubleSidedQuadSeenFromBehindIsLitAsItsFrontIs) {
    BRITE::RenderPass sunlit;
    BRITE::Light sun;
    sun.Direction = {0.0f, 0.0f, -1.0f};
    sun.Intensity = 3.0f;
    sunlit.Lights.push_back(sun);

    Scene front;
    front.Materials = {GltfMaterial{}};
    front.Quads = {{-1.0f, 1.0f, 0}};
    const BRITE::ModelHandle facing = m_backend.LoadModel(WriteGltf(m_dir.Path(), front).string().c_str());
    ASSERT_NE(facing, BRITE::NullModelHandle);
    const BRITE::Color frontLit = At(DrawPass(sunlit, {{facing, {}}}), LEFT_HALF);
    EXPECT_GT(frontLit.r, 100) << "the front, lit";
    m_backend.UnloadModel(facing);

    Scene oneSided;
    oneSided.Materials = {GltfMaterial{}};
    Quad away{-1.0f, 1.0f, 0};
    away.facingAway = true;
    oneSided.Quads = {away};
    const BRITE::ModelHandle culled = m_backend.LoadModel(WriteGltf(m_dir.Path(), oneSided).string().c_str());
    ASSERT_NE(culled, BRITE::NullModelHandle);
    ExpectColour(At(DrawPass(sunlit, {{culled, {}}}), LEFT_HALF), 0, 0, 0, "one-sided, from behind: culled");
    m_backend.UnloadModel(culled);

    Scene twoSided = oneSided;
    twoSided.Materials[0].doubleSided = true;
    const BRITE::ModelHandle both = m_backend.LoadModel(WriteGltf(m_dir.Path(), twoSided).string().c_str());
    ASSERT_NE(both, BRITE::NullModelHandle);
    ExpectColour(At(DrawPass(sunlit, {{both, {}}}), LEFT_HALF), frontLit.r, frontLit.g, frontLit.b,
                 "double-sided, from behind: as the front");
    m_backend.UnloadModel(both);
}

// Double-sided is per mesh: a double-sided half and a one-sided half, both
// facing away, in one model and one draw, under the one light. The left is
// drawn, 186 -- the flat ambient does not care which way a normal points -- and
// the right, drawn after it, is still culled.
//
// Mutation: culling not turned back on after a double-sided mesh -> the right
// half draws, 186.
TEST_F(ModelMaterialTest, ADoubleSidedMeshLeavesTheNextOneCulled) {
    Scene scene;
    GltfMaterial both;
    both.doubleSided = true;
    scene.Materials = {both, GltfMaterial{}};
    Quad left{-1.0f, 0.0f, 0};
    left.facingAway = true;
    Quad right{0.0f, 1.0f, 1};
    right.facingAway = true;
    scene.Quads = {left, right};
    const BRITE::ModelHandle model = m_backend.LoadModel(WriteGltf(m_dir.Path(), scene).string().c_str());
    ASSERT_NE(model, BRITE::NullModelHandle);

    const auto pixels = Draw(model);
    ExpectColour(At(pixels, LEFT_HALF), LIT_FULL, LIT_FULL, LIT_FULL, "double-sided, from behind");
    ExpectColour(At(pixels, RIGHT_HALF), 0, 0, 0, "one-sided, from behind, drawn after it");
    m_backend.UnloadModel(model);
}

// A draw can make a mesh built in code double-sided: a quad wound to face away
// is culled as it is, and drawn when the draw asks, 186 under the one light.
//
// Mutation: the draw's DoubleSided ignored -> black.
TEST_F(ModelMaterialTest, ADrawMakesAMeshBuiltInCodeDoubleSided) {
    BRITE::MeshData mesh = QuadMesh(-1.0f, 1.0f, {255, 255, 255, 255});
    mesh.Indices = {0, 2, 1, 0, 3, 2};
    for (BRITE::Vector3& n : mesh.Normals)
        n = {0.0f, 0.0f, -1.0f};
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
    ASSERT_NE(model, BRITE::NullModelHandle);

    ExpectColour(At(Draw(model), LEFT_HALF), 0, 0, 0, "facing away: culled");
    BRITE::PBRMaterial both;
    both.DoubleSided = true;
    ExpectColour(At(Draw(model, both), LEFT_HALF), LIT_FULL, LIT_FULL, LIT_FULL, "the draw's double-sided");
    m_backend.UnloadModel(model);
}

// Which of a model's bound texture ids are its own, worked by hand: 0 is no
// texture and 1 is the placeholder, so of {0, 1, 7, 7, 9, 0, 1} the model owns
// 7 and 9, each once.
//
// Mutations: 0 not skipped -> {0, 7, 9}; the placeholder not skipped ->
// {1, 7, 9}, freeing a texture every other model shares; duplicates kept ->
// {7, 7, 9}, freeing 7 twice.
TEST(TexturesOwnedByModel, EachOwnTextureOnceAndNeitherZeroNorThePlaceholder) {
    const std::vector<unsigned int> owned = RaylibRenderBackend::TexturesOwnedByModel({0, 1, 7, 7, 9, 0, 1}, 1);
    EXPECT_EQ(owned, (std::vector<unsigned int>{7, 9}));
}
