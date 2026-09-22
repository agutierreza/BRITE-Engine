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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
// OpenGL 1.1, exported by opengl32.dll itself: the one way to ask whether a
// texture name is still alive without reaching into the backend.
extern "C" __declspec(dllimport) unsigned char __stdcall glIsTexture(unsigned int texture);
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
    float x0, x1;      // the quad spans x0..x1 and y -1..1
    int material = -1; // index into Materials, or -1 for none
};

struct GltfMaterial {
    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool textured = false;
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
            Append(bin, 1.0f);
        }
        const int normal = addView(start, bin.size() - start, 4, 5126, "VEC3");
        // u runs with world x across the whole picture's quad range, -1..1 -> 0..1.
        start = bin.size();
        for (int i = 0; i < 4; ++i) {
            Append(bin, (xs[i] + 1.0f) * 0.5f);
            Append(bin, (1.0f - ys[i]) * 0.5f);
        }
        const int texcoord = addView(start, bin.size() - start, 4, 5126, "VEC2");
        start = bin.size();
        for (unsigned short i : {0, 1, 2, 0, 2, 3})
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
        std::snprintf(buf, sizeof(buf), "%s{\"pbrMetallicRoughness\":{\"baseColorFactor\":[%g,%g,%g,%g]%s}}",
                      materials.empty() ? "" : ",", m.rgba[0], m.rgba[1], m.rgba[2], m.rgba[3],
                      m.textured ? ",\"baseColorTexture\":{\"index\":0}" : "");
        materials += buf;
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
        BRITE::Camera3D camera{
            {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4.0f, BRITE::CameraProjection::Orthographic};
        BRITE::RenderPass pass;
        pass.TargetFramebuffer = m_target;
        pass.ClearColor = {0, 0, 0, 255};
        pass.Camera3DPtr = &camera;
        pass.AmbientColor = {255, 255, 255, 255};
        pass.AmbientIntensity = 1.0f;

        BRITE::ModelDrawCommand draw;
        draw.Model = model;
        draw.Material = material;
        draw.Position = {0.0f, 0.0f, 0.0f};
        draw.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        draw.Scale = {1.0f, 1.0f, 1.0f};
        pass.ModelCommands.push_back(draw);
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
