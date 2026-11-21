// The order a pass is drawn in (RenderPass.hpp): opaque draws first, then translucent ones, farthest first.
//
// raylib draws a model the moment it is asked and holds primitives in a batch drawn later, so models used to reach the
// screen before every primitive whatever the pass listed. A translucent model then blended with the clear colour, and
// the primitive behind it failed the depth test against the depth the model had written. These cases draw flat quads
// and thin boxes under an orthographic camera into a render texture, on the GPU under a hidden window, and read the
// pixels back.
//
// The picture is DepthWriteTests': the camera at (0, 0, 5) looks down -z at the origin with +y up, and the view is 4
// world units across, so world -2..2 maps onto 64 pixels, 16 a unit: column (x + 2) * 16, row (2 - y) * 16 from the
// top. Nearer the camera is larger z. Every model is unlit, so a pixel is its colour as authored.
//
// THE BLEND. raylib blends src * a + dst * (1 - a) with a the fragment's alpha, in the 8-bit target, and the unlit
// shader writes the authored sRGB colour with alpha = tint alpha / 255 and no conversion. An alpha of 128 is 0.50196,
// so a channel c drawn over d is c * 0.50196 + d * 0.49804, each step rounded to 8 bits; the cases allow 2 for the
// rounding. Over the black clear colour a channel of 255 comes out 128.
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

std::string Show(BRITE::Color c) {
    return std::to_string(int(c.r)) + " " + std::to_string(int(c.g)) + " " + std::to_string(int(c.b));
}

constexpr BRITE::Color SOLID_RED{255, 0, 0, 255};
constexpr BRITE::Color SOLID_GREEN{0, 255, 0, 255};
constexpr BRITE::Color SOLID_BLUE{0, 0, 255, 255};
constexpr BRITE::Color SEE_THROUGH_RED{255, 0, 0, 128};
constexpr BRITE::Color SEE_THROUGH_GREEN{0, 255, 0, 128};
constexpr BRITE::Color SEE_THROUGH_BLUE{0, 0, 255, 128};

class TranslucentDrawOrderTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(SIZE, SIZE, "BRITE translucent draw order tests");
    }
    static void TearDownTestSuite() {
        ::CloseWindow();
    }

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
        return Load(mesh);
    }

    /// Two full-width sheets in ONE mesh, the first at `zFirst` in `colourFirst`, the second at `zSecond` in
    /// `colourSecond`, listed in that order.
    BRITE::ModelHandle TwoSheets(float zFirst, BRITE::Color colourFirst, float zSecond, BRITE::Color colourSecond) {
        BRITE::MeshData mesh;
        mesh.Positions = {{-2, -2, zFirst},  {2, -2, zFirst},  {2, 2, zFirst},  {-2, 2, zFirst},
                          {-2, -2, zSecond}, {2, -2, zSecond}, {2, 2, zSecond}, {-2, 2, zSecond}};
        mesh.Normals.assign(8, {0.0f, 0.0f, 1.0f});
        mesh.Colors = {colourFirst,  colourFirst,  colourFirst,  colourFirst,
                       colourSecond, colourSecond, colourSecond, colourSecond};
        mesh.Indices = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
        return Load(mesh);
    }

    BRITE::ModelHandle Load(const BRITE::MeshData& mesh) {
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
        EXPECT_NE(model, BRITE::NullModelHandle);
        m_models.push_back(model);
        return model;
    }

    /// The draw of `model` in `tint`, unlit and seen from both sides; `writesDepth` false for a layer.
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

    /// A box centred at (x, y, z), `width` by `height` and 0.02 deep, in `tint`.
    static BRITE::Primitive3DDrawCommand Box(float x, float y, float z, float width, float height, BRITE::Color tint) {
        BRITE::Primitive3DDrawCommand box;
        box.Type = BRITE::Primitive3DType::Cube;
        box.Position = {x, y, z};
        box.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        box.Scale = {1.0f, 1.0f, 1.0f};
        box.Size = {width, height, 0.02f};
        box.Tint = tint;
        return box;
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
        EXPECT_EQ(pixels.size(), static_cast<std::size_t>(SIZE * SIZE));
        return pixels;
    }

    RaylibRenderBackend m_backend;
    std::vector<BRITE::ModelHandle> m_models;
};

} // namespace

// The issue's own case. A red box covers the picture at z = 0; a see-through blue sheet covers the top half at z = 0.5,
// IN FRONT of it. Blue over red at alpha 128:
//   red   = 255 x (1 - 128/255) = 255 x 0.49804 = 127;   blue  = 255 x 128/255 = 128;   green = 0
// so (127, 0, 128) at (0, 1), the box showing through. Under the lower half there is only the box: red, (255, 0, 0).
// What used to happen: the sheet is drawn at once, over the black clear colour, (0, 0, 128), and writes its depth; the
// box, drawn when the batch is flushed, fails the depth test behind it and the pixel stays (0, 0, 128).
//
// Mutations: the primitive batch not flushed before the translucent draws -> (0, 0, 128); the translucent meshes
// drawn with the opaque ones, as before -> (0, 0, 128).
TEST_F(TranslucentDrawOrderTest, ATranslucentModelInFrontOfAPrimitiveShowsThePrimitiveThroughIt) {
    BRITE::RenderPass pass;
    pass.Primitive3DCommands.push_back(Box(0.0f, 0.0f, 0.0f, 4.0f, 4.0f, SOLID_RED));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, 0.0f, 2.0f, 2.0f, 0.5f), SEE_THROUGH_BLUE));
    const std::vector<BRITE::Color> pixels = Picture(pass);

    const BRITE::Color through = pixels[At(0.0f, 1.0f)];
    EXPECT_TRUE(Near(through, 127, 0, 128)) << Show(through);
    const BRITE::Color below = pixels[At(0.0f, -1.0f)];
    EXPECT_TRUE(Near(below, 255, 0, 0)) << Show(below);
}

// The other way round: a see-through green box in front of an opaque red model, blending with it. Green over red at
// alpha 128: red = 255 x 0.49804 = 127, green = 128 -> (127, 128, 0); under the lower half only the model: red.
//
// This one passed before the change too -- a primitive was always drawn after every model -- and guards that the
// translucent stage does not lose the model or the blend. It has no mutation of its own.
TEST_F(TranslucentDrawOrderTest, ATranslucentPrimitiveInFrontOfAModelBlendsWithTheModel) {
    BRITE::RenderPass pass;
    pass.Primitive3DCommands.push_back(Box(0.0f, 1.0f, 0.5f, 4.0f, 2.0f, SEE_THROUGH_GREEN));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.0f), SOLID_RED));
    const std::vector<BRITE::Color> pixels = Picture(pass);

    const BRITE::Color through = pixels[At(0.0f, 1.0f)];
    EXPECT_TRUE(Near(through, 127, 128, 0)) << Show(through);
    const BRITE::Color below = pixels[At(0.0f, -1.0f)];
    EXPECT_TRUE(Near(below, 255, 0, 0)) << Show(below);
}

// Two see-through sheets overlapping, listed NEAR first -- the wrong order. The far one is red at z = 0.2, the near one
// blue at z = 0.6. Drawn farthest first: red over the black clear colour is (128, 0, 0); blue over that is
//   red = 128 x 0.49804 = 63.75 -> 64;   blue = 255 x 0.50196 = 128    -> (64, 0, 128).
// Drawn in the order listed it would be blue first, (0, 0, 128), then red over it: red = 255 x 0.50196 = 128,
// blue = 128 x 0.49804 = 63.75 -> (128, 0, 64).
//
// Mutations: the sort turned round (nearest first) -> (128, 0, 64); no sort, the pass's order -> (128, 0, 64).
TEST_F(TranslucentDrawOrderTest, TwoTranslucentModelsListedNearestFirstAreDrawnFarthestFirst) {
    BRITE::RenderPass pass;
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.6f), SEE_THROUGH_BLUE));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.2f), SEE_THROUGH_RED));
    const std::vector<BRITE::Color> pixels = Picture(pass);

    const BRITE::Color both = pixels[At(0.0f, 0.0f)];
    EXPECT_TRUE(Near(both, 64, 0, 128)) << Show(both);
}

// A translucent primitive sorts among the translucent models, not before or after them all: far red model at z = 0.2,
// a see-through green box at z = 0.4, near blue model at z = 0.6, all over the whole picture. Farthest first:
//   red  over black   (128, 0, 0)
//   green over that   red = 128 x 0.49804 = 63.75 -> 64, green = 128                  -> (64, 128, 0)
//   blue over that    red = 63.75 x 0.49804 = 31.75 -> 32, green = 128 x 0.49804 = 63.75 -> 64, blue = 128
// so (32, 64, 128). The box drawn before every model, over the black clear colour, is (0, 128, 0); red over it is
// red = 255 x 0.50196 = 128, green = 128 x 0.49804 = 63.75 -> 64; blue over that is (64, 32, 128): a different pixel.
//
// Mutation: translucent primitives drawn before the translucent models (not in the sort) -> (64, 32, 128).
TEST_F(TranslucentDrawOrderTest, ATranslucentPrimitiveIsSortedAmongTheTranslucentModels) {
    BRITE::RenderPass pass;
    pass.Primitive3DCommands.push_back(Box(0.0f, 0.0f, 0.4f, 4.0f, 4.0f, SEE_THROUGH_GREEN));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.6f), SEE_THROUGH_BLUE));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.2f), SEE_THROUGH_RED));
    const std::vector<BRITE::Color> pixels = Picture(pass);

    const BRITE::Color all = pixels[At(0.0f, 0.0f)];
    EXPECT_TRUE(Near(all, 32, 64, 128)) << Show(all);
}

// A translucent draw writes no depth, so what is drawn after it and lies behind it is still drawn. ONE mesh, which is
// never split, holds a see-through blue sheet at z = 0.8 and then a see-through green one at z = 0.2, behind it, over
// an opaque red model at z = 0. The mesh draws in its own order: blue over red is (127, 0, 128); green over that is
//   red = 127 x 0.49804 = 63.25 -> 63, green = 128, blue = 128 x 0.49804 = 63.75 -> 64    -> (63, 128, 64).
// Were the blue sheet to write its depth the green, behind it, would fail the test and the pixel stay (127, 0, 128).
//
// Mutation: translucent meshes writing depth -> (127, 0, 128).
TEST_F(TranslucentDrawOrderTest, ATranslucentDrawWritesNoDepth) {
    BRITE::RenderPass pass;
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.0f), SOLID_RED));
    // The sheets' colours come from the vertices; the draw's tint, white at alpha 128, makes the mesh translucent.
    pass.ModelCommands.push_back(
        Draw(TwoSheets(0.8f, SOLID_BLUE, 0.2f, SOLID_GREEN), BRITE::Color{255, 255, 255, 128}));
    const std::vector<BRITE::Color> pixels = Picture(pass);

    const BRITE::Color both = pixels[At(0.0f, 0.0f)];
    EXPECT_TRUE(Near(both, 63, 128, 64)) << Show(both);
}

// A layer -- a draw that sets DepthWrite off -- keeps the order it was submitted in, after the sorted draws. A blue
// layer at z = 0.2 is listed FIRST, a see-through red model at z = 0.5 second. The sorted red draws first: (128, 0, 0);
// the layer after it: red = 128 x 0.49804 = 64, blue = 128 -> (64, 0, 128). A layer sorted with the others would be the
// farther, drawn first: (0, 0, 128), then red over it: (128, 0, 64).
//
// Mutation: layers sorted with the rest -> (128, 0, 64).
TEST_F(TranslucentDrawOrderTest, ALayerIsDrawnAfterTheSortedDrawsInTheOrderSubmitted) {
    BRITE::RenderPass pass;
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.2f), SEE_THROUGH_BLUE, false));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.5f), SEE_THROUGH_RED));
    const std::vector<BRITE::Color> pixels = Picture(pass);

    const BRITE::Color both = pixels[At(0.0f, 0.0f)];
    EXPECT_TRUE(Near(both, 64, 0, 128)) << Show(both);
}

// An opaque scene is drawn as it always was: the depth test decides, whatever the order. A green model covers the
// picture at z = 0.5. A red box in front of it at z = 0.75 over the right half; a blue box behind it at z = 0.25 over
// the left half. The red shows on the right, the blue is hidden on the left, and the green shows where nothing else is.
//
// Mutation: opaque draws writing no depth -> the blue box, drawn after the model, covers it on the left.
TEST_F(TranslucentDrawOrderTest, AnOpaqueSceneIsDrawnAsItAlwaysWas) {
    BRITE::RenderPass pass;
    pass.Primitive3DCommands.push_back(Box(1.0f, 0.0f, 0.75f, 2.0f, 4.0f, SOLID_RED));
    pass.Primitive3DCommands.push_back(Box(-1.0f, 0.0f, 0.25f, 2.0f, 4.0f, SOLID_BLUE));
    pass.ModelCommands.push_back(Draw(Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.5f), SOLID_GREEN));
    const std::vector<BRITE::Color> pixels = Picture(pass);

    EXPECT_TRUE(Near(pixels[At(1.0f, 0.0f)], 255, 0, 0)) << Show(pixels[At(1.0f, 0.0f)]);
    EXPECT_TRUE(Near(pixels[At(-1.0f, 0.0f)], 0, 255, 0)) << Show(pixels[At(-1.0f, 0.0f)]);
}
