// PBRMaterial::DepthWrite: a draw that is depth-tested but writes no depth of its own.
//
// A see-through layer over a surface -- a decal, a tint, a glow -- must not hide what is drawn after it, and several
// such layers at one height must blend over each other rather than fight for the same depth. These cases draw flat
// quads under an orthographic camera into a render texture, on the GPU under a hidden window, and read the pixels back.
//
// The picture: the camera at (0, 0, 5) looks down -z at the origin with +y up, and the orthographic view is 4 world
// units across, so world -2..2 maps onto 64 pixels, 16 a unit: column (x + 2) * 16, row (2 - y) * 16 from the top.
// Nearer the camera is larger z. Every quad is unlit, so a pixel is its tint as authored.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/MeshData.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <Backends/RenderPass.hpp>
#include <gtest/gtest.h>

#include <raylib.h>

#include <cstdlib>
#include <vector>

using BRITE::Backends::Raylib::RaylibRenderBackend;

namespace {

constexpr int SIZE = 64;

/// The pixel under world (x, y).
std::size_t At(float x, float y) {
    const int column = static_cast<int>((x + 2.0f) * 16.0f);
    const int row = static_cast<int>((2.0f - y) * 16.0f);
    return static_cast<std::size_t>(row * SIZE + column);
}

bool Near(BRITE::Color c, int r, int g, int b) {
    return std::abs(c.r - r) <= 2 && std::abs(c.g - g) <= 2 && std::abs(c.b - b) <= 2;
}

class DepthWriteTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(SIZE, SIZE, "BRITE depth write tests");
    }
    static void TearDownTestSuite() { ::CloseWindow(); }

    void TearDown() override {
        for (const BRITE::ModelHandle model : m_models)
            m_backend.UnloadModel(model);
    }

    /// A flat rectangle from (x0, y0) to (x1, y1) at depth z, facing the camera.
    BRITE::ModelHandle Quad(float x0, float y0, float x1, float y1, float z) {
        BRITE::MeshData mesh;
        mesh.Positions = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
        mesh.Normals.assign(4, {0.0f, 0.0f, 1.0f});
        mesh.Indices = {0, 1, 2, 0, 2, 3};
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
        EXPECT_NE(model, BRITE::NullModelHandle);
        m_models.push_back(model);
        return model;
    }

    /// The draw of `model` in `tint`, unlit and seen from both sides; `writesDepth` false for a see-through layer.
    static BRITE::ModelDrawCommand Draw(BRITE::ModelHandle model, BRITE::Color tint, bool writesDepth = true) {
        BRITE::ModelDrawCommand draw;
        draw.Model = model;
        draw.Material.Unlit = true;
        draw.Material.DoubleSided = true;
        draw.Material.AlbedoTint = tint;
        draw.Material.DepthWrite = writesDepth;
        draw.Position = {0.0f, 0.0f, 0.0f};
        draw.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        draw.Scale = {1.0f, 1.0f, 1.0f};
        return draw;
    }

    /// Submit `pass` into a fresh render texture under the camera above, and read it back.
    std::vector<BRITE::Color> Picture(BRITE::RenderPass& pass) {
        BRITE::Camera3D camera{
            {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4.0f, BRITE::CameraProjection::Orthographic};
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
        pass.TargetFramebuffer = target;
        pass.ClearColor = {0, 0, 0, 255};
        pass.Camera3DPtr = &camera;
        m_backend.SubmitRenderPass(pass);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        m_backend.UnloadRenderTexture(target);
        pass.Camera3DPtr = nullptr;
        return pixels;
    }

    RaylibRenderBackend m_backend;
    std::vector<BRITE::ModelHandle> m_models;
};

constexpr BRITE::Color RED_SURFACE{255, 0, 0, 255};
constexpr BRITE::Color GREEN_SHEET{0, 255, 0, 255};
constexpr BRITE::Color YELLOW_NEAR{255, 255, 0, 255};
constexpr BRITE::Color WHITE_SOLID{255, 255, 255, 255};
/// Blue at alpha 128: drawn over red it is 255 x (1 - 128/255) = 127 red and 255 x 128/255 = 128 blue.
constexpr BRITE::Color SEE_THROUGH_BLUE{0, 0, 255, 128};

} // namespace

// Four quads, drawn in this order:
//   a red surface over the whole picture at z = 0;
//   an opaque yellow column, x 1..2, at z = 0.75 -- the nearest thing;
//   a see-through blue layer over the top half, y 0..2, at z = 0.5, WRITING NO DEPTH;
//   an opaque green column, x -2..-1, at z = 0.25 -- BEHIND the blue layer.
// Read back:
//   at (0, 1) the blue blends over the red: (127, 0, 128) -- it still blends;
//   at (-1.5, 1) the green, drawn after the blue and behind it, is tested against what lay behind the blue -- the red
//     surface at 0 -- and passes, so the pixel is pure green: the blue left no depth to hide it;
//   at (1.5, 1) the yellow, nearer and drawn before, hides the blue: pure yellow -- the blue is still depth-TESTED;
//   at (0, -1), under nothing but the surface, red.
//
// Mutations: the backend ignoring DepthWrite (the blue writes depth) -> at (-1.5, 1) the green fails behind the blue
// and the pixel is the blend, (127, 0, 128); the depth TEST switched off for the draw instead of the writes -> at
// (1.5, 1) the blue blends over the yellow.
TEST_F(DepthWriteTest, ASeeThroughLayerThatWritesNoDepthBlendsHidesNothingAndIsStillHidden) {
    BRITE::RenderPass pass;
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.0f), RED_SURFACE));
    pass.ModelCommands.push_back(Draw(Quad(1.0f, -2.0f, 2.0f, 2.0f, 0.75f), YELLOW_NEAR));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, 0.0f, 2.0f, 2.0f, 0.5f), SEE_THROUGH_BLUE, false));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, -1.0f, 2.0f, 0.25f), GREEN_SHEET));
    const std::vector<BRITE::Color> pixels = Picture(pass);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(SIZE * SIZE));

    const BRITE::Color blended = pixels[At(0.0f, 1.0f)];
    EXPECT_TRUE(Near(blended, 127, 0, 128)) << int(blended.r) << " " << int(blended.g) << " " << int(blended.b);
    const BRITE::Color later = pixels[At(-1.5f, 1.0f)];
    EXPECT_TRUE(Near(later, 0, 255, 0)) << "drawn after the layer, behind it: " << int(later.r) << " " << int(later.g)
                                        << " " << int(later.b);
    const BRITE::Color nearer = pixels[At(1.5f, 1.0f)];
    EXPECT_TRUE(Near(nearer, 255, 255, 0)) << "nearer and drawn before: " << int(nearer.r) << " " << int(nearer.g)
                                           << " " << int(nearer.b);
    const BRITE::Color surface = pixels[At(0.0f, -1.0f)];
    EXPECT_TRUE(Near(surface, 255, 0, 0));
}

// Two see-through layers overlapping, the later a little BEHIND the earlier -- as two layers meant for one height land
// when the rasteriser rounds them a step apart, which is how a pair of depth-writing tints speckles. Writing no depth,
// they blend in the order drawn, whatever their depths: red first over the black clear at alpha 128 gives 255 x
// 128/255 = 128 red, then blue over it 128 x (1 - 128/255) = 63.8 red and 128 blue: (64, 0, 128), everywhere the two
// overlap. Every pixel of the overlap is checked, not one.
//
// Mutation: the backend ignoring DepthWrite -> the earlier layer writes its depth and the later fails behind it: the
// overlap stays (128, 0, 0).
TEST_F(DepthWriteTest, LayersThatWriteNoDepthBlendInTheOrderDrawnWhateverTheirDepths) {
    BRITE::RenderPass pass;
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 1.0f, 2.0f, 0.5f), BRITE::Color{255, 0, 0, 128}, false));
    pass.ModelCommands.push_back(Draw(Quad(-1.0f, -2.0f, 2.0f, 2.0f, 0.45f), SEE_THROUGH_BLUE, false));
    const std::vector<BRITE::Color> pixels = Picture(pass);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(SIZE * SIZE));

    // The overlap, x -1..1, inset a pixel from each edge.
    int wrong = 0;
    for (int row = 1; row < SIZE - 1; ++row) {
        for (int column = 17; column < 47; ++column) {
            const BRITE::Color c = pixels[static_cast<std::size_t>(row * SIZE + column)];
            wrong += Near(c, 64, 0, 128) ? 0 : 1;
        }
    }
    EXPECT_EQ(wrong, 0) << "pixels of the overlap that are not the later layer over the earlier";
}

// The writes come back after the draw that turned them off: for the next model, and for the next pass's primitives,
// which rlgl batches and draws as the 3D mode ends.
//   This pass: a see-through layer writing no depth; then an opaque white column x -1..1 at z = 0.6; then an opaque
//   green sheet over everything at z = 0.3, behind the white. At (0, -1) the white wrote its depth, so the green fails
//   there: white.
//   The next pass, primitives only: a white box at z = 0.6 over x -1..1, then a green box over everything at z = 0.3,
//   behind it. At (0, 0) the white box's depth hides the green: white.
//
// Mutation: the depth writes not switched back on after the see-through draw -> the white column writes no depth and
// the green covers it at (0, -1); and the next pass's boxes write none either, so the green box covers the white at
// (0, 0).
TEST_F(DepthWriteTest, TheWritesComeBackForTheNextModelAndTheNextPassesPrimitives) {
    BRITE::RenderPass models;
    models.ModelCommands.push_back(Draw(Quad(-2.0f, 0.0f, 2.0f, 2.0f, 0.5f), SEE_THROUGH_BLUE, false));
    models.ModelCommands.push_back(Draw(Quad(-1.0f, -2.0f, 1.0f, 2.0f, 0.6f), WHITE_SOLID));
    models.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.3f), GREEN_SHEET));
    const std::vector<BRITE::Color> modelPixels = Picture(models);
    ASSERT_EQ(modelPixels.size(), static_cast<std::size_t>(SIZE * SIZE));
    const BRITE::Color column = modelPixels[At(0.0f, -1.0f)];
    EXPECT_TRUE(Near(column, 255, 255, 255)) << "the next model: " << int(column.r) << " " << int(column.g) << " "
                                             << int(column.b);

    BRITE::RenderPass boxes;
    BRITE::Primitive3DDrawCommand front;
    front.Type = BRITE::Primitive3DType::Cube;
    front.Position = {0.0f, 0.0f, 0.6f};
    front.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    front.Scale = {1.0f, 1.0f, 1.0f};
    front.Size = {2.0f, 4.0f, 0.02f};
    front.Tint = WHITE_SOLID;
    BRITE::Primitive3DDrawCommand back = front;
    back.Position = {0.0f, 0.0f, 0.3f};
    back.Size = {4.0f, 4.0f, 0.02f};
    back.Tint = GREEN_SHEET;
    boxes.Primitive3DCommands = {front, back};
    const std::vector<BRITE::Color> boxPixels = Picture(boxes);
    ASSERT_EQ(boxPixels.size(), static_cast<std::size_t>(SIZE * SIZE));
    const BRITE::Color box = boxPixels[At(0.0f, 0.0f)];
    EXPECT_TRUE(Near(box, 255, 255, 255)) << "the next pass's primitives: " << int(box.r) << " " << int(box.g) << " "
                                          << int(box.b);
}

// Asked for nothing, a draw writes its depth: nothing that exists changes.
//
// Mutation: DepthWrite defaulting to false -> false.
TEST(DepthWriteDefault, ADrawWritesItsDepthUnlessAsked) {
    EXPECT_TRUE(BRITE::PBRMaterial{}.DepthWrite);
}
