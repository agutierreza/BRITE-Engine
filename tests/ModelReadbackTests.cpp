// RaylibRenderBackend::ReadModelMeshes: a model's geometry read back as MeshData.
// Each case writes a small glTF file whose every number is chosen so the read-back
// can be worked by hand, loads it under a hidden window, and reads it back.
//
// THE TWO-NODE MODEL, used by several cases:
//
//   material 0  baseColorFactor (1, 0.5, 0, 1)
//   material 1  baseColorFactor (0.5, 1, 1, 0.5)
//
//   raylib stores a factor f as the byte (unsigned char)(f * 255), truncated:
//   1 -> 255, 0.5 -> 127.5 -> 127, 0 -> 0. So material 0 is (255, 127, 0, 255)
//   and material 1 is (127, 255, 255, 127).
//
//   mesh 0  a unit quad, (-0.5,-0.5) (0.5,-0.5) (0.5,0.5) (-0.5,0.5) at z = 0,
//           normals +z, no vertex colours, indices 0 1 2 0 2 3, material 0
//   mesh 1  a 2 x 1 quad, (0,0) (2,0) (2,1) (0,1) at z = 0, normals +z,
//           vertex colours (128,255,255,255) on vertex 0 and white on the rest,
//           texture coordinates (0,0) (0.25,0) (0.25,0.5) (0,0.5),
//           indices 0 1 2 0 2 3, material 1
//
//   Only mesh 1 has texture coordinates, and a node's transform moves positions
//   and normals, never texture coordinates: they read back exactly as written.
//
//   node 0  mesh 0, translation (-1.5, 0, 0)
//   node 1  no mesh, translation (1, 0, 0), scale 0.5, parent of node 2
//   node 2  mesh 1, translation (0, 0.5, 0), rotation 90 degrees about +z
//
// Mesh 0 lands at x -2..-1, y -0.5..0.5. Mesh 1's world transform is node 1's
// times node 2's: v -> T1 * S1 * T2 * R2 * v. The rotation takes (x, y) to
// (-y, x), so its corners go
//
//   (0,0) -R-> (0,0)  -T2-> (0,0.5)  -S1-> (0,0.25)    -T1-> (1, 0.25)
//   (2,0) -R-> (0,2)  -T2-> (0,2.5)  -S1-> (0,1.25)    -T1-> (1, 1.25)
//   (2,1) -R-> (-1,2) -T2-> (-1,2.5) -S1-> (-0.5,1.25) -T1-> (0.5, 1.25)
//   (0,1) -R-> (-1,0) -T2-> (-1,0.5) -S1-> (-0.5,0.25) -T1-> (0.5, 0.25)
//
// and it covers x 0.5..1, y 0.25..1.25. Its normals go through the
// inverse-transpose of the linear part 0.5 * R, which is 2 * R: +z comes out as
// (0, 0, 2). raylib does not renormalise it, and the read-back does.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/IRenderBackend.hpp>
#include <Backends/MeshData.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <Backends/RenderPass.hpp>
#include <Math/BriteMath.hpp>
#include <gtest/gtest.h>

#include <raylib.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using BRITE::Backends::Raylib::RaylibRenderBackend;

namespace {

constexpr int SIZE = 64;
constexpr int LIT_FULL = 186; // a saturated channel under the one light: ModelMaterialTests.cpp
constexpr int TOLERANCE = 2;  // a GPU's rounding to 8 bits, and nothing more
constexpr float EPSILON = 1e-5f;

// ---------------------------------------------------------------------------
// A glTF writer, just big enough: one primitive per mesh, each attribute
// optional, nodes with translation, rotation, scale and children.
// ---------------------------------------------------------------------------

struct GltfMesh {
    std::vector<BRITE::Vector3> Positions;
    std::vector<BRITE::Vector3> Normals;   // empty: no NORMAL attribute
    std::vector<BRITE::Color> Colors;      // empty: no COLOR_0 attribute
    std::vector<BRITE::Vector2> TexCoords; // empty: no TEXCOORD_0 attribute
    std::vector<unsigned short> Indices;   // empty: drawn without indices
    int Material = -1;
};

struct GltfNode {
    int Mesh = -1;
    float Translation[3] = {0.0f, 0.0f, 0.0f};
    float Rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
    float Scale[3] = {1.0f, 1.0f, 1.0f};
    std::vector<int> Children;
};

struct GltfFile {
    std::vector<GltfMesh> Meshes;
    std::vector<std::vector<float>> Materials; // each a baseColorFactor
    std::vector<GltfNode> Nodes;
};

class TempDir {
  public:
    TempDir() {
        std::random_device rd;
        m_path = fs::temp_directory_path() / ("brite_model_readback_tests_" + std::to_string(rd()));
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

std::string Floats(const float* values, int count) {
    std::string out = "[";
    for (int i = 0; i < count; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s%.9g", i ? "," : "", values[i]);
        out += buf;
    }
    return out + "]";
}

// Writes dir/model.gltf and dir/model.bin, and returns the .gltf's path.
fs::path WriteGltf(const fs::path& dir, const GltfFile& file) {
    std::vector<unsigned char> bin;
    std::string views, accessors, meshes;
    int index = 0;
    auto addView = [&](std::size_t offset, int count, int componentType, const char* type, bool normalized) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}",
                      views.empty() ? "" : ",", offset, bin.size() - offset);
        views += buf;
        std::snprintf(buf, sizeof(buf), "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%d,\"type\":\"%s\"%s}",
                      accessors.empty() ? "" : ",", index, componentType, count, type,
                      normalized ? ",\"normalized\":true" : "");
        accessors += buf;
        while (bin.size() % 4)
            bin.push_back(0);
        return index++;
    };

    for (const GltfMesh& mesh : file.Meshes) {
        const int count = static_cast<int>(mesh.Positions.size());
        std::string attributes;
        std::size_t start = bin.size();
        for (const auto& p : mesh.Positions) {
            Append(bin, p.x);
            Append(bin, p.y);
            Append(bin, p.z);
        }
        attributes += "\"POSITION\":" + std::to_string(addView(start, count, 5126, "VEC3", false));
        if (!mesh.Normals.empty()) {
            start = bin.size();
            for (const auto& n : mesh.Normals) {
                Append(bin, n.x);
                Append(bin, n.y);
                Append(bin, n.z);
            }
            attributes += ",\"NORMAL\":" + std::to_string(addView(start, count, 5126, "VEC3", false));
        }
        if (!mesh.Colors.empty()) {
            start = bin.size();
            for (const auto& c : mesh.Colors) {
                Append(bin, c.r);
                Append(bin, c.g);
                Append(bin, c.b);
                Append(bin, c.a);
            }
            attributes += ",\"COLOR_0\":" + std::to_string(addView(start, count, 5121, "VEC4", true));
        }
        if (!mesh.TexCoords.empty()) {
            start = bin.size();
            for (const auto& t : mesh.TexCoords) {
                Append(bin, t.x);
                Append(bin, t.y);
            }
            attributes += ",\"TEXCOORD_0\":" + std::to_string(addView(start, count, 5126, "VEC2", false));
        }
        std::string primitive = "{\"attributes\":{" + attributes + "}";
        if (!mesh.Indices.empty()) {
            start = bin.size();
            for (unsigned short i : mesh.Indices)
                Append(bin, i);
            primitive += ",\"indices\":" +
                         std::to_string(addView(start, static_cast<int>(mesh.Indices.size()), 5123, "SCALAR", false));
        }
        if (mesh.Material >= 0)
            primitive += ",\"material\":" + std::to_string(mesh.Material);
        primitive += "}";
        meshes += (meshes.empty() ? "" : ",") + std::string("{\"primitives\":[") + primitive + "]}";
    }

    std::string nodes, roots;
    std::vector<bool> isChild(file.Nodes.size(), false);
    for (const GltfNode& node : file.Nodes)
        for (int child : node.Children)
            isChild[static_cast<std::size_t>(child)] = true;
    for (std::size_t n = 0; n < file.Nodes.size(); ++n) {
        const GltfNode& node = file.Nodes[n];
        std::string json = "{\"translation\":" + Floats(node.Translation, 3) +
                           ",\"rotation\":" + Floats(node.Rotation, 4) + ",\"scale\":" + Floats(node.Scale, 3);
        if (node.Mesh >= 0)
            json += ",\"mesh\":" + std::to_string(node.Mesh);
        if (!node.Children.empty()) {
            json += ",\"children\":[";
            for (std::size_t c = 0; c < node.Children.size(); ++c)
                json += (c ? "," : "") + std::to_string(node.Children[c]);
            json += "]";
        }
        nodes += (nodes.empty() ? "" : ",") + json + "}";
        if (!isChild[n])
            roots += (roots.empty() ? "" : ",") + std::to_string(n);
    }

    std::string materials;
    for (const auto& factor : file.Materials)
        materials += (materials.empty() ? "" : ",") + std::string("{\"pbrMetallicRoughness\":{\"baseColorFactor\":") +
                     Floats(factor.data(), 4) + "}}";

    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[" + roots +
                       "]}],\"nodes\":[" + nodes + "],\"meshes\":[" + meshes + "],\"accessors\":[" + accessors +
                       "],\"bufferViews\":[" + views +
                       "],\"buffers\":[{\"uri\":\"model.bin\",\"byteLength\":" + std::to_string(bin.size()) + "}]";
    if (!materials.empty())
        json += ",\"materials\":[" + materials + "]";
    json += "}";

    std::ofstream(dir / "model.bin", std::ios::binary).write(reinterpret_cast<const char*>(bin.data()), bin.size());
    std::ofstream(dir / "model.gltf") << json;
    return dir / "model.gltf";
}

const std::vector<BRITE::Vector3> FACING_Z = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
const std::vector<unsigned short> QUAD = {0, 1, 2, 0, 2, 3};

// The two-node model of the header comment.
GltfFile TwoNodeModel() {
    GltfFile file;
    file.Materials = {{1.0f, 0.5f, 0.0f, 1.0f}, {0.5f, 1.0f, 1.0f, 0.5f}};

    GltfMesh unitQuad;
    unitQuad.Positions = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f}};
    unitQuad.Normals = FACING_Z;
    unitQuad.Indices = QUAD;
    unitQuad.Material = 0;

    GltfMesh longQuad;
    longQuad.Positions = {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    longQuad.Normals = FACING_Z;
    longQuad.Colors = {{128, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}};
    longQuad.TexCoords = {{0.0f, 0.0f}, {0.25f, 0.0f}, {0.25f, 0.5f}, {0.0f, 0.5f}};
    longQuad.Indices = QUAD;
    longQuad.Material = 1;
    file.Meshes = {unitQuad, longQuad};

    GltfNode left;
    left.Mesh = 0;
    left.Translation[0] = -1.5f;
    GltfNode parent;
    parent.Translation[0] = 1.0f;
    parent.Scale[0] = parent.Scale[1] = parent.Scale[2] = 0.5f;
    parent.Children = {2};
    GltfNode child;
    child.Mesh = 1;
    child.Translation[1] = 0.5f;
    // 90 degrees about +z: (0, 0, sin 45, cos 45).
    child.Rotation[2] = 0.70710678f;
    child.Rotation[3] = 0.70710678f;
    file.Nodes = {left, parent, child};
    return file;
}

void ExpectVector(const BRITE::Vector3& actual, float x, float y, float z, const std::string& what) {
    EXPECT_NEAR(actual.x, x, EPSILON) << what;
    EXPECT_NEAR(actual.y, y, EPSILON) << what;
    EXPECT_NEAR(actual.z, z, EPSILON) << what;
}

void ExpectColour(const BRITE::Color& actual, int r, int g, int b, int a, const std::string& what) {
    EXPECT_EQ(actual.r, r) << what;
    EXPECT_EQ(actual.g, g) << what;
    EXPECT_EQ(actual.b, b) << what;
    EXPECT_EQ(actual.a, a) << what;
}

// ---------------------------------------------------------------------------

class ModelReadbackTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(SIZE, SIZE, "BRITE model readback tests");
    }
    static void TearDownTestSuite() {
        ::CloseWindow();
    }

    BRITE::ModelHandle Load(const GltfFile& file) {
        return m_backend.LoadModel(WriteGltf(m_dir.Path(), file).string().c_str());
    }

    RaylibRenderBackend m_backend;
    TempDir m_dir;
};

} // namespace

// The two-node model read back, every number worked in the header comment.
//
// Mutations: the normals copied without renormalising -> mesh 1's read (0, 0, 2);
// the material's colour not folded in -> mesh 0 reads white and mesh 1's vertex 0
// (128, 255, 255, 255); the vertex colour ignored -> mesh 1's vertex 0 reads
// (127, 255, 255, 127); the meshes read in reverse order -> every position fails;
// texture coordinates not read back -> mesh 1 has none; invented for a mesh with
// none -> mesh 0 has four; u and v swapped -> mesh 1's vertex 1 reads (0, 0.25).
TEST_F(ModelReadbackTest, ATwoNodeModelReadsBackWithItsNodeTransformsAndMaterialColours) {
    const BRITE::ModelHandle model = Load(TwoNodeModel());
    ASSERT_NE(model, BRITE::NullModelHandle);

    std::vector<BRITE::MeshData> meshes;
    ASSERT_TRUE(m_backend.ReadModelMeshes(model, meshes));
    ASSERT_EQ(meshes.size(), 2u) << "one MeshData per mesh, and node 1 has none";

    const BRITE::MeshData& left = meshes[0];
    ASSERT_EQ(left.Positions.size(), 4u);
    ASSERT_EQ(left.Normals.size(), 4u);
    ASSERT_EQ(left.Colors.size(), 4u);
    EXPECT_EQ(left.Indices, (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
    EXPECT_TRUE(left.TexCoords.empty()) << "mesh 0's file gave it no texture coordinates";
    // The unit quad moved 1.5 m to -x.
    ExpectVector(left.Positions[0], -2.0f, -0.5f, 0.0f, "mesh 0, vertex 0");
    ExpectVector(left.Positions[1], -1.0f, -0.5f, 0.0f, "mesh 0, vertex 1");
    ExpectVector(left.Positions[2], -1.0f, 0.5f, 0.0f, "mesh 0, vertex 2");
    ExpectVector(left.Positions[3], -2.0f, 0.5f, 0.0f, "mesh 0, vertex 3");
    for (std::size_t i = 0; i < 4; ++i) {
        ExpectVector(left.Normals[i], 0.0f, 0.0f, 1.0f, "mesh 0, normal " + std::to_string(i));
        // No vertex colours, so white times material 0: (255, 127, 0, 255).
        ExpectColour(left.Colors[i], 255, 127, 0, 255, "mesh 0, colour " + std::to_string(i));
    }

    const BRITE::MeshData& right = meshes[1];
    ASSERT_EQ(right.Positions.size(), 4u);
    ASSERT_EQ(right.Normals.size(), 4u);
    ASSERT_EQ(right.Colors.size(), 4u);
    EXPECT_EQ(right.Indices, (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
    // As written, untouched by the nodes' transforms.
    ASSERT_EQ(right.TexCoords.size(), 4u);
    const float us[4] = {0.0f, 0.25f, 0.25f, 0.0f}, vs[4] = {0.0f, 0.0f, 0.5f, 0.5f};
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(right.TexCoords[i].x, us[i]) << "mesh 1, u " << i;
        EXPECT_EQ(right.TexCoords[i].y, vs[i]) << "mesh 1, v " << i;
    }
    // Through node 1 and node 2, as the header works them.
    ExpectVector(right.Positions[0], 1.0f, 0.25f, 0.0f, "mesh 1, vertex 0");
    ExpectVector(right.Positions[1], 1.0f, 1.25f, 0.0f, "mesh 1, vertex 1");
    ExpectVector(right.Positions[2], 0.5f, 1.25f, 0.0f, "mesh 1, vertex 2");
    ExpectVector(right.Positions[3], 0.5f, 0.25f, 0.0f, "mesh 1, vertex 3");
    // (0, 0, 2) as raylib leaves it, (0, 0, 1) once renormalised.
    for (std::size_t i = 0; i < 4; ++i)
        ExpectVector(right.Normals[i], 0.0f, 0.0f, 1.0f, "mesh 1, normal " + std::to_string(i));
    // Vertex 0 is (128, 255, 255, 255) times material 1's (127, 255, 255, 127):
    //   red    128 * 127 / 255 = 16256 / 255 = 63.75 -> 64
    //   green  255 * 255 / 255 = 255;  blue the same
    //   alpha  255 * 127 / 255 = 127
    // The other vertices are white, so they read material 1 itself.
    ExpectColour(right.Colors[0], 64, 255, 255, 127, "mesh 1, vertex 0's colour times material 1");
    for (std::size_t i = 1; i < 4; ++i)
        ExpectColour(right.Colors[i], 127, 255, 255, 127, "mesh 1, colour " + std::to_string(i));

    m_backend.UnloadModel(model);
}

// The read-back meshes, loaded through LoadModelFromMesh and drawn with no tint,
// draw the picture the model draws. Both are drawn under the one light of
// ModelMaterialTests.cpp, onto the same 64x64 orthographic view: world x and y
// in -2..2, 16 pixels per metre, +y up. Mesh 0 covers columns 0..16, rows
// 24..40; mesh 1 columns 40..48, rows 12..28 (row = (2 - y) * 16).
//
// Mutations: the material's colour not folded in -> mesh 0 draws white where the
// model draws orange; the vertex colour ignored -> mesh 1's corner differs; the
// meshes read in reverse order -> each draws in the other's colour; every
// position read 0.25 m high -> both meshes move four rows.
TEST_F(ModelReadbackTest, TheMeshesReadBackDrawThePictureTheModelDraws) {
    const BRITE::ModelHandle model = Load(TwoNodeModel());
    ASSERT_NE(model, BRITE::NullModelHandle);
    std::vector<BRITE::MeshData> meshes;
    ASSERT_TRUE(m_backend.ReadModelMeshes(model, meshes));
    std::vector<BRITE::ModelHandle> baked;
    for (const BRITE::MeshData& mesh : meshes) {
        baked.push_back(m_backend.LoadModelFromMesh(mesh));
        ASSERT_NE(baked.back(), BRITE::NullModelHandle);
    }

    const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
    auto draw = [&](const std::vector<BRITE::ModelHandle>& models) {
        BRITE::Camera3D camera{
            {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4.0f, BRITE::CameraProjection::Orthographic};
        BRITE::RenderPass pass;
        pass.TargetFramebuffer = target;
        pass.ClearColor = {0, 0, 0, 255};
        pass.Camera3DPtr = &camera;
        pass.AmbientColor = {255, 255, 255, 255};
        pass.AmbientIntensity = 1.0f;
        for (BRITE::ModelHandle m : models) {
            BRITE::ModelDrawCommand command;
            command.Model = m;
            command.Position = {0.0f, 0.0f, 0.0f};
            command.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            command.Scale = {1.0f, 1.0f, 1.0f};
            pass.ModelCommands.push_back(command);
        }
        m_backend.SubmitRenderPass(pass);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        EXPECT_EQ(width * height, SIZE * SIZE);
        return pixels;
    };
    const std::vector<BRITE::Color> original = draw({model});
    const std::vector<BRITE::Color> readBack = draw(baked);
    ASSERT_EQ(original.size(), readBack.size());

    // Not two empty pictures: mesh 0's centre, (-1.5, 0), is pixel (8, 32), and
    // its red channel is a saturated one under the one light, 186.
    EXPECT_NEAR(original[32 * SIZE + 8].r, LIT_FULL, TOLERANCE) << "the model drew mesh 0";
    // Mesh 1's centre, (0.75, 0.75), is pixel (44, 20); its green is saturated.
    EXPECT_GT(original[20 * SIZE + 44].g, 0) << "the model drew mesh 1";

    int differing = 0;
    for (std::size_t i = 0; i < original.size(); ++i) {
        const BRITE::Color a = original[i], b = readBack[i];
        const bool same = std::abs(a.r - b.r) <= TOLERANCE && std::abs(a.g - b.g) <= TOLERANCE &&
                          std::abs(a.b - b.b) <= TOLERANCE && std::abs(a.a - b.a) <= TOLERANCE;
        if (!same && differing++ < 5)
            ADD_FAILURE() << "pixel (" << i % SIZE << ", " << i / SIZE << "): the model drew (" << int(a.r) << ", "
                          << int(a.g) << ", " << int(a.b) << "), the read-back (" << int(b.r) << ", " << int(b.g)
                          << ", " << int(b.b) << ")";
    }
    EXPECT_EQ(differing, 0) << "pixels that differ by more than " << TOLERANCE;

    m_backend.UnloadRenderTexture(target);
    for (BRITE::ModelHandle m : baked)
        m_backend.UnloadModel(m);
    m_backend.UnloadModel(model);
}

// A mesh built in code reads back as it went in. Its material is raylib's
// default, white, and x * 255 / 255 is x, so the colours come back unchanged; a
// unit normal renormalises to itself, (0.6, 0.8, 0) being 3-4-5.
//
// Mutations: an index read one place along -> the indices differ; normals
// read from the positions array -> the normals differ; a colour channel
// swapped -> the colours differ; texture coordinates not uploaded by
// LoadModelFromMesh -> none come back.
TEST_F(ModelReadbackTest, AMeshBuiltInCodeReadsBackAsItWentIn) {
    BRITE::MeshData mesh;
    mesh.Positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.5f}};
    mesh.Normals = {{0.0f, 0.0f, 1.0f}, {0.6f, 0.8f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    mesh.Colors = {{10, 20, 30, 255}, {40, 50, 60, 200}, {70, 80, 90, 100}, {255, 0, 128, 0}};
    mesh.TexCoords = {{0.0f, 0.0f}, {3.5f, 0.0f}, {0.0f, -2.0f}, {3.5f, -2.0f}};
    mesh.Indices = {0, 1, 2, 2, 1, 3};
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
    ASSERT_NE(model, BRITE::NullModelHandle);

    std::vector<BRITE::MeshData> meshes;
    ASSERT_TRUE(m_backend.ReadModelMeshes(model, meshes));
    ASSERT_EQ(meshes.size(), 1u);
    const BRITE::MeshData& back = meshes[0];
    ASSERT_EQ(back.Positions.size(), mesh.Positions.size());
    ASSERT_EQ(back.Normals.size(), mesh.Normals.size());
    ASSERT_EQ(back.Colors.size(), mesh.Colors.size());
    ASSERT_EQ(back.TexCoords.size(), mesh.TexCoords.size());
    for (std::size_t i = 0; i < mesh.Positions.size(); ++i) {
        const std::string which = "vertex " + std::to_string(i);
        ExpectVector(back.Positions[i], mesh.Positions[i].x, mesh.Positions[i].y, mesh.Positions[i].z, which);
        ExpectVector(back.Normals[i], mesh.Normals[i].x, mesh.Normals[i].y, mesh.Normals[i].z, which);
        ExpectColour(back.Colors[i], mesh.Colors[i].r, mesh.Colors[i].g, mesh.Colors[i].b, mesh.Colors[i].a, which);
        EXPECT_EQ(back.TexCoords[i].x, mesh.TexCoords[i].x) << which;
        EXPECT_EQ(back.TexCoords[i].y, mesh.TexCoords[i].y) << which;
    }
    EXPECT_EQ(back.Indices, mesh.Indices);
    EXPECT_EQ(BRITE::CheckMeshData(back, RaylibRenderBackend::MaxVerticesPerMesh), BRITE::MeshDataProblem::None);
    m_backend.UnloadModel(model);
}

// A mesh with no vertex colours reads back with one per vertex: white, times a
// white material, is white -- not an empty list, which the promise excludes.
//
// Mutation: Colors left empty when the mesh has none -> size 0, not 3.
TEST_F(ModelReadbackTest, AMeshWithoutColoursReadsBackWhite) {
    BRITE::MeshData mesh;
    mesh.Positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    mesh.Normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    mesh.Indices = {0, 1, 2};
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
    ASSERT_NE(model, BRITE::NullModelHandle);

    std::vector<BRITE::MeshData> meshes;
    ASSERT_TRUE(m_backend.ReadModelMeshes(model, meshes));
    ASSERT_EQ(meshes.size(), 1u);
    ASSERT_EQ(meshes[0].Colors.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i)
        ExpectColour(meshes[0].Colors[i], 255, 255, 255, 255, "vertex " + std::to_string(i));
    m_backend.UnloadModel(model);
}

// A glTF primitive without indices is drawn three vertices to a triangle, in
// order, so six vertices read back as indices 0..5.
//
// Mutations: Indices left empty for such a mesh -> size 0; filled with zeros ->
// 0 0 0 0 0 0.
TEST_F(ModelReadbackTest, AMeshWithoutIndicesReadsBackIndexedInOrder) {
    GltfFile file;
    GltfMesh twoTriangles;
    twoTriangles.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    twoTriangles.Normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    file.Meshes = {twoTriangles};
    GltfNode node;
    node.Mesh = 0;
    file.Nodes = {node};
    const BRITE::ModelHandle model = Load(file);
    ASSERT_NE(model, BRITE::NullModelHandle);

    std::vector<BRITE::MeshData> meshes;
    ASSERT_TRUE(m_backend.ReadModelMeshes(model, meshes));
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Indices, (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5}));
    EXPECT_EQ(BRITE::CheckMeshData(meshes[0], RaylibRenderBackend::MaxVerticesPerMesh), BRITE::MeshDataProblem::None);
    m_backend.UnloadModel(model);
}

// A glTF primitive without normals reads back with none, rather than with normals
// invented for it -- and CheckMeshData refuses it, for exactly that reason, until
// the caller supplies them.
//
// Mutation: Normals filled with zeros when the mesh has none -> size 3, not 0,
// and CheckMeshData passes it.
TEST_F(ModelReadbackTest, AMeshWithoutNormalsReadsBackWithNone) {
    GltfFile file;
    GltfMesh triangle;
    triangle.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    triangle.Indices = {0, 1, 2};
    file.Meshes = {triangle};
    GltfNode node;
    node.Mesh = 0;
    file.Nodes = {node};
    const BRITE::ModelHandle model = Load(file);
    ASSERT_NE(model, BRITE::NullModelHandle);

    std::vector<BRITE::MeshData> meshes;
    ASSERT_TRUE(m_backend.ReadModelMeshes(model, meshes));
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
    EXPECT_TRUE(meshes[0].Normals.empty());
    EXPECT_EQ(BRITE::CheckMeshData(meshes[0], RaylibRenderBackend::MaxVerticesPerMesh),
              BRITE::MeshDataProblem::NormalCountMismatch);
    m_backend.UnloadModel(model);
}

// A handle that names no model -- never issued, the null handle, or one already
// unloaded -- reads back nothing and says so, and a caller's list is emptied
// rather than left holding what it held.
//
// Mutations: true returned for an unknown handle -> the EXPECT_FALSEs fail; the
// list not cleared first -> it still holds the placeholder mesh.
TEST_F(ModelReadbackTest, AHandleThatNamesNoModelReadsBackNothing) {
    BRITE::MeshData placeholder;
    placeholder.Positions = {{0, 0, 0}};
    std::vector<BRITE::MeshData> meshes = {placeholder};
    EXPECT_FALSE(m_backend.ReadModelMeshes(BRITE::NullModelHandle, meshes));
    EXPECT_TRUE(meshes.empty()) << "the null handle";

    meshes = {placeholder};
    EXPECT_FALSE(m_backend.ReadModelMeshes(123456, meshes));
    EXPECT_TRUE(meshes.empty()) << "a handle never issued";

    BRITE::MeshData triangle;
    triangle.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    triangle.Normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    triangle.Indices = {0, 1, 2};
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(triangle);
    ASSERT_NE(model, BRITE::NullModelHandle);
    m_backend.UnloadModel(model);
    meshes = {placeholder};
    EXPECT_FALSE(m_backend.ReadModelMeshes(model, meshes));
    EXPECT_TRUE(meshes.empty()) << "a handle already unloaded";
}

// ModulateChannel is the shader's a * b in 8 bits, rounded to the nearest:
//   128 * 127 / 255 = 63.75  -> 64   (truncated: 63)
//   255 * 127 / 255 = 127    -> 127
//   200 * 200 / 255 = 156.86 -> 157  (truncated: 156)
//     0 * 255 / 255 = 0      -> 0
//   255 * 255 / 255 = 255    -> 255
//
// Mutation: the + 127 dropped (truncation) -> 63 and 156.
TEST(ModelReadbackColour, OneChannelTimesAnotherRoundsToTheNearest) {
    EXPECT_EQ(RaylibRenderBackend::ModulateChannel(128, 127), 64);
    EXPECT_EQ(RaylibRenderBackend::ModulateChannel(255, 127), 127);
    EXPECT_EQ(RaylibRenderBackend::ModulateChannel(200, 200), 157);
    EXPECT_EQ(RaylibRenderBackend::ModulateChannel(0, 255), 0);
    EXPECT_EQ(RaylibRenderBackend::ModulateChannel(255, 255), 255);
}

namespace {
// A backend written before ReadModelMeshes existed: it overrides every pure
// method and nothing else, so it inherits the interface's default.
class OlderBackend : public BRITE::Backends::IRenderBackend {
  public:
    void SubmitRenderPass(const BRITE::RenderPass&) override {}
    BRITE::TextureHandle LoadRenderTexture(int, int) override {
        return BRITE::NullTextureHandle;
    }
    void UnloadRenderTexture(BRITE::TextureHandle) override {}
    bool ReadRenderTexture(BRITE::TextureHandle, int&, int&, std::vector<BRITE::Color>&) override {
        return false;
    }
    BRITE::TextureHandle LoadTexture(const char*) override {
        return BRITE::NullTextureHandle;
    }
    void UnloadTexture(BRITE::TextureHandle) override {}
    BRITE::ModelHandle LoadModel(const char*) override {
        return 1;
    }
    BRITE::ModelHandle LoadModelFromMesh(const BRITE::MeshData&) override {
        return 1;
    }
    void UnloadModel(BRITE::ModelHandle) override {}
    BRITE::ShaderHandle LoadShader(const char*, const char*) override {
        return BRITE::NullShaderHandle;
    }
    BRITE::ShaderHandle LoadShaderFromMemory(const char*, const char*) override {
        return BRITE::NullShaderHandle;
    }
    void UnloadShader(BRITE::ShaderHandle) override {}
    BRITE::EnvironmentMap LoadEnvironmentMap(const char*) override {
        return {};
    }
    void UnloadEnvironmentMap(BRITE::EnvironmentMap) override {}
    int GetShaderLocation(BRITE::ShaderHandle, const char*) override {
        return -1;
    }
    void SetShaderValue(BRITE::ShaderHandle, int, const void*, BRITE::Backends::ShaderUniformDataType) override {}
};
} // namespace

// A backend that cannot read geometry back says so, rather than claiming a model
// with no meshes -- even for a handle it issued.
//
// Mutations: the default returns true -> the EXPECT_FALSE fails; it does not
// clear the list -> the placeholder survives. (Making it pure again stops this
// file compiling, which is the other half of the point.)
TEST(ModelReadbackDefault, ABackendThatCannotReadBackSaysSo) {
    OlderBackend backend;
    BRITE::MeshData placeholder;
    placeholder.Positions = {{0, 0, 0}};
    std::vector<BRITE::MeshData> meshes = {placeholder};
    EXPECT_FALSE(backend.ReadModelMeshes(backend.LoadModel("any"), meshes));
    EXPECT_TRUE(meshes.empty());
}
