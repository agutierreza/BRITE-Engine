// Motion blur (RenderPass.hpp, MotionBlurSettings): its geometry on the CPU (MotionBlur.hpp), then the pass on the GPU
// under a hidden window (HiddenWindowScene.hpp).
//
// THE GL CASES' VIEW is HiddenWindowScene's orthographic camera: at (0, 0, 5) looking down -z, 4 world units high on a
// 64 x 64 target, so one world unit is 64 / 4 = 16 pixels, and a camera moved by d along x moves every still point by
// -16 d pixels on the target. The streaks are read back from the pass's motion target: x and y in pixels, +y up, 1 in
// blue for an exempt pixel.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/MotionBlur.hpp>

#include "HiddenWindowScene.hpp"

#include <cmath>
#include <vector>

using HiddenWindow::At;
using HiddenWindow::AtLinear;
using HiddenWindow::Within;

namespace {

namespace MB = BRITE::MotionBlur;

// Whether two pictures are the same, byte for byte.
bool Same(const std::vector<BRITE::Color>& a, const std::vector<BRITE::Color>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b || a[i].a != b[i].a)
            return false;
    return true;
}

BRITE::Camera3D Perspective(BRITE::Vector3 position, BRITE::Vector3 target, float fovy) {
    return {position, target, {0.0f, 1.0f, 0.0f}, fovy, BRITE::CameraProjection::Perspective};
}

} // namespace

// ---------------------------------------------------------------------------
// The geometry, by hand
// ---------------------------------------------------------------------------

// A camera at (0, 0, 10) looking at the origin with a 90-degree view, on a 100 x 100 target, steps 1 to the right
// (camera and target both). Its frame: forward (0, 0, -1), right = forward x up = (0*0 - (-1)*1, (-1)*0 - 0*0, 0) =
// (1, 0, 0), up (0, 1, 0). The origin, 10 along the view, where the view is tan(45) x 10 = 10 high each way:
//   before, from (0, 0, 10): x = 0, so ndc 0;
//   now, from (1, 0, 10): x = -1, so ndc -1 / 10 = -0.1.
// Its motion: (-0.1 - 0) x 0.5 x 100 = -5 pixels -- 5 to the left as the camera goes right.
// Mutation: PixelMotion's half dropped (x 100 alone) reads -10.
TEST(MotionBlurMaths, APointMovesAgainstAPerspectiveCameraByItsDistance) {
    const BRITE::Camera3D before = Perspective({0, 0, 10}, {0, 0, 0}, 90.0f);
    const BRITE::Camera3D now = Perspective({1, 0, 10}, {1, 0, 0}, 90.0f);
    BRITE::Vector2 was, is;
    ASSERT_TRUE(MB::Project(before, 1.0f, {0, 0, 0}, was));
    ASSERT_TRUE(MB::Project(now, 1.0f, {0, 0, 0}, is));
    EXPECT_NEAR(was.x, 0.0f, 1e-6f);
    EXPECT_NEAR(is.x, -0.1f, 1e-6f);
    const BRITE::Vector2 motion = MB::PixelMotion(was, is, 100, 100);
    EXPECT_NEAR(motion.x, -5.0f, 1e-4f);
    EXPECT_NEAR(motion.y, 0.0f, 1e-4f);
    // And the point back through the camera: drawn at ndc -0.1, 10 along the view, it is the origin.
    const BRITE::Vector3 back = MB::Unproject(now, 1.0f, is, 10.0f);
    EXPECT_NEAR(back.x, 0.0f, 1e-5f);
    EXPECT_NEAR(back.z, 0.0f, 1e-5f);
}

// The orthographic view of the GL cases, 4 high on 64 x 64, its camera stepped 0.25 along x: the origin goes from ndc
// 0 to -0.25 / (2 x 1) = -0.125, so (-0.125) x 0.5 x 64 = -4 pixels.
TEST(MotionBlurMaths, AnOrthographicViewMovesEveryPointAlike) {
    BRITE::Camera3D before = HiddenWindow::Camera();
    BRITE::Camera3D now = before;
    now.position.x += 0.25f;
    now.target.x += 0.25f;
    for (const BRITE::Vector3 point : {BRITE::Vector3{0, 0, 0}, BRITE::Vector3{1.5f, -1, -3}}) {
        BRITE::Vector2 was, is;
        ASSERT_TRUE(MB::Project(before, 1.0f, point, was));
        ASSERT_TRUE(MB::Project(now, 1.0f, point, is));
        EXPECT_NEAR(MB::PixelMotion(was, is, 64, 64).x, -4.0f, 1e-4f);
    }
}

// A streak is the motion times the shutter, shortened along its own direction to the maximum: (30, 40) is 50 long; at
// shutter 1 and a maximum of 10 it is (30, 40) x 10 / 50 = (6, 8); at shutter 0.5 and a maximum of 64, (15, 20); at
// shutter 0.1, (3, 4), under any maximum of 5 or more.
// Mutation: the clamp dropped reads (30, 40) for the first; the shutter ignored reads (30, 40) for the second.
TEST(MotionBlurMaths, AStreakIsTheShutterOfTheMotionHeldToTheMaximum) {
    BRITE::Vector2 s = MB::Streak({30, 40}, 1.0f, 10.0f);
    EXPECT_NEAR(s.x, 6.0f, 1e-5f);
    EXPECT_NEAR(s.y, 8.0f, 1e-5f);
    s = MB::Streak({30, 40}, 0.5f, 64.0f);
    EXPECT_NEAR(s.x, 15.0f, 1e-5f);
    EXPECT_NEAR(s.y, 20.0f, 1e-5f);
    s = MB::Streak({30, 40}, 0.1f, 5.0f);
    EXPECT_NEAR(s.x, 3.0f, 1e-5f);
    EXPECT_NEAR(s.y, 4.0f, 1e-5f);
}

// The holds: a shutter above 1 is 1 and below 0 is 0; a maximum below 1 is 1 and above 64 is 64. (30, 40) at shutter
// 2 is (30, 40) held to 64, so (30, 40); at shutter -1, (0, 0); with a maximum of 0.25, held to 1: (0.6, 0.8); with
// a maximum of 1000, held to 64, (300, 400) -- 500 long -- is (38.4, 51.2).
TEST(MotionBlurMaths, TheShutterAndTheMaximumAreHeld) {
    BRITE::Vector2 s = MB::Streak({30, 40}, 2.0f, 64.0f);
    EXPECT_NEAR(s.x, 30.0f, 1e-5f);
    s = MB::Streak({30, 40}, -1.0f, 64.0f);
    EXPECT_NEAR(s.x, 0.0f, 1e-6f);
    EXPECT_NEAR(s.y, 0.0f, 1e-6f);
    s = MB::Streak({30, 40}, 1.0f, 0.25f);
    EXPECT_NEAR(s.x, 0.6f, 1e-5f);
    EXPECT_NEAR(s.y, 0.8f, 1e-5f);
    s = MB::Streak({300, 400}, 1.0f, 1000.0f);
    EXPECT_NEAR(s.x, 38.4f, 1e-4f);
    EXPECT_NEAR(s.y, 51.2f, 1e-4f);
}

// A cut. The 90-degree camera of the first case on 100 x 100, stepped 1: every cut point lies 10 along the view, so
// each moves by the same -5 pixels (a step square to the view moves the plane it looks at as one): the camera's motion
// is 5. Stepped 100, -500: beyond the default CutLength of 300, a cut. Turned to face the other way, the points it
// looks at lie behind last frame's camera: a cut. A change of projection: a cut. And a frame with no previous camera
// is no cut, and one the caller marks is.
// Mutation: IsCut never comparing CutLength lets the 100 step through.
TEST(MotionBlurMaths, AJumpATurnAndAChangeOfProjectionAreCuts) {
    const BRITE::Camera3D before = Perspective({0, 0, 10}, {0, 0, 0}, 90.0f);
    EXPECT_NEAR(MB::CameraMotion(before, before, 100, 100), 0.0f, 1e-4f);
    EXPECT_NEAR(MB::CameraMotion(before, Perspective({1, 0, 10}, {1, 0, 0}, 90.0f), 100, 100), 5.0f, 1e-3f);

    BRITE::MotionBlurSettings settings;
    settings.Enabled = true;
    EXPECT_FALSE(MB::IsCut(settings, before, 100, 100)); // no previous camera
    settings.PreviousCamera = before;
    EXPECT_FALSE(MB::IsCut(settings, Perspective({1, 0, 10}, {1, 0, 0}, 90.0f), 100, 100));
    EXPECT_TRUE(MB::IsCut(settings, Perspective({100, 0, 10}, {100, 0, 0}, 90.0f), 100, 100));
    EXPECT_TRUE(MB::IsCut(settings, Perspective({0, 0, 10}, {0, 0, 20}, 90.0f), 100, 100));
    BRITE::Camera3D ortho = before;
    ortho.projection = BRITE::CameraProjection::Orthographic;
    EXPECT_TRUE(MB::IsCut(settings, ortho, 100, 100));
    settings.Cut = true;
    EXPECT_TRUE(MB::IsCut(settings, before, 100, 100));
}

// ---------------------------------------------------------------------------
// The pass, on the GPU
// ---------------------------------------------------------------------------

namespace {

class MotionBlurPass : public HiddenWindow::Scene {
  protected:
    static constexpr int SIZE = HiddenWindow::SIZE;

    /// A pass of `draws` with motion blur on -- shutter 1, the longest streak 32, one sample a pixel -- whose camera
    /// moved by `cameraStep` along x since last frame.
    BRITE::RenderPass Pass(std::vector<BRITE::ModelDrawCommand> draws, float cameraStep, bool hdr = false) {
        BRITE::RenderPass pass;
        pass.Post.Hdr = hdr;
        pass.Post.Samples = 1;
        pass.Post.MotionBlur.Enabled = true;
        pass.Post.MotionBlur.Shutter = 1.0f;
        pass.Post.MotionBlur.MaxLength = 32.0f;
        BRITE::Camera3D before = HiddenWindow::Camera();
        before.position.x -= cameraStep;
        before.target.x -= cameraStep;
        pass.Post.MotionBlur.PreviousCamera = before;
        pass.ModelCommands = std::move(draws);
        return pass;
    }

    /// A black field behind everything, at z -1.
    BRITE::ModelDrawCommand Field(BRITE::Color colour = {0, 0, 0, 255}) {
        return Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, -1.0f), colour);
    }

    /// The last pass's streaks.
    std::vector<float> Motion() {
        int width = 0, height = 0;
        std::vector<float> rgba;
        EXPECT_TRUE(m_backend.ReadLastMotionVectors(width, height, rgba));
        EXPECT_EQ(width, SIZE);
        return rgba;
    }
    float MotionX(const std::vector<float>& m, float x, float y) {
        return m[AtLinear(x, y)];
    }
};

} // namespace

// THE CAMERA'S MOTION. The camera stepped 0.25 along x: every still point moves -16 x 0.25 = -4 pixels (above, and
// AnOrthographicViewMovesEveryPointAlike), at the near quad and the far field alike.
// Mutation: the reprojection set to the identity (last frame's view-projection ignored) reads 0.
TEST_F(MotionBlurPass, TheCameraMovesEveryStillPixelByItsStep) {
    BRITE::RenderPass pass = Pass({Field(), Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {200, 40, 40, 255})}, 0.25f);
    Picture(pass);
    const std::vector<float> m = Motion();
    for (const auto [x, y] : {std::pair{0.0f, 0.0f}, {-1.5f, 1.5f}, {1.2f, -1.7f}}) {
        EXPECT_NEAR(m[AtLinear(x, y)], -4.0f, 0.05f) << x << ", " << y;
        EXPECT_NEAR(m[AtLinear(x, y) + 1], 0.0f, 0.05f);
        EXPECT_EQ(m[AtLinear(x, y) + 2], 0.0f);
    }
}

// A perspective camera, so the camera's motion comes back through the depth: at (0, 0, 5) looking at the origin with a
// 90-degree view, the field at z 0 is 5 along it and tan(45) x 5 = 5 high each way. Stepped 0.5 along x, a point of the
// field moves -0.5 / 5 = -0.1 in ndc, -0.1 x 0.5 x 64 = -3.2 pixels; a quad at z 2.5, 2.5 along, moves -0.5 / 2.5 =
// -0.2, -6.4 pixels -- the nearer, the faster.
// Mutation: the depth read as 0 (the near plane) gives every pixel the near plane's motion.
TEST_F(MotionBlurPass, APerspectiveCameraMovesNearerPixelsFaster) {
    BRITE::RenderPass pass;
    pass.Post.Samples = 1;
    pass.Post.MotionBlur.Enabled = true;
    pass.Post.MotionBlur.Shutter = 1.0f;
    pass.Post.MotionBlur.PreviousCamera = Perspective({-0.5f, 0, 5}, {-0.5f, 0, 0}, 90.0f);
    pass.ModelCommands = {Unlit(Quad(-20, -20, 20, 20, 0.0f), {0, 0, 0, 255}),
                          Unlit(Quad(0.5f, -0.5f, 1.5f, 0.5f, 2.5f), {200, 200, 200, 255})};
    BRITE::Camera3D camera = Perspective({0, 0, 5}, {0, 0, 0}, 90.0f);
    const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
    pass.TargetFramebuffer = target;
    pass.Camera3DPtr = &camera;
    m_backend.SubmitRenderPass(pass);
    m_backend.UnloadRenderTexture(target);
    const std::vector<float> m = Motion();
    // Pixel (column, row from the bottom): the field at column 8, row 32; the quad, which spans ndc x 0.5/2.5 = 0.2 to
    // 1.5/2.5 = 0.6, columns 38 to 51, at column 44.
    auto at = [&](int column, int row) { return m[static_cast<std::size_t>(row * SIZE + column) * 4]; };
    EXPECT_NEAR(at(8, 32), -3.2f, 0.05f);
    EXPECT_NEAR(at(44, 32), -6.4f, 0.05f);
}

// THE SHUTTER: the same step at shutter 0.5 smears half of it, -2 pixels.
// Mutation: the shutter's scale dropped from motion_resolve.fs reads -4.
TEST_F(MotionBlurPass, TheShutterSmearsItsShareOfTheMotion) {
    BRITE::RenderPass pass = Pass({Field()}, 0.25f);
    pass.Post.MotionBlur.Shutter = 0.5f;
    Picture(pass);
    EXPECT_NEAR(MotionX(Motion(), 0.0f, 0.0f), -2.0f, 0.05f);
}

// THE LONGEST STREAK: the step's -4 pixels held to a MaxLength of 3, -3.
// Mutation: the clamp dropped from motion_resolve.fs reads -4.
TEST_F(MotionBlurPass, AStreakIsHeldToTheLongest) {
    BRITE::RenderPass pass = Pass({Field()}, 0.25f);
    pass.Post.MotionBlur.MaxLength = 3.0f;
    Picture(pass);
    EXPECT_NEAR(MotionX(Motion(), 0.0f, 0.0f), -3.0f, 0.05f);
}

// A STILL VIEW: last frame's camera is this one, and only a small quad in the top right corner moved (+8 pixels), so
// the pass is blurred. Every pixel of the left half has a streak of zero -- and its blurred picture is the scene
// exactly: no streak of its own reaches anything, and the corner's streak, which its tiles' neighbours carry, weighs
// nothing at a pixel whose own streak and whose taps' streaks are zero, so each is copied as it is.
// Mutation: the reprojection's inverse dropped (last frame's view-projection alone) gives the still view a motion.
// (Removing the gather's early copy of such a pixel is an EQUIVALENT mutant, run and survived: with every streak under
// half a pixel each tap lands on the pixel itself and is skipped, so the copy is a saving, not a rule.)
TEST_F(MotionBlurPass, AStillViewHasNoMotionAndIsNotBlurred) {
    BRITE::ModelDrawCommand corner = Unlit(Quad(1.5f, 1.5f, 1.9f, 1.9f, 0.0f), {240, 240, 240, 255});
    corner.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass = Pass(
        {Field({20, 30, 60, 255}), Unlit(Quad(-1.5f, -0.5f, -0.5f, 0.5f, 0.0f), {240, 40, 40, 255}), corner}, 0.0f);
    Picture(pass);
    const std::vector<float> m = Motion();
    int width = 0, height = 0;
    std::vector<float> blurred;
    ASSERT_TRUE(m_backend.ReadLastMotionBlurred(width, height, blurred));
    const std::vector<float> scene = LinearScene();
    ASSERT_EQ(blurred.size(), scene.size());
    for (int row = 0; row < SIZE; ++row)
        for (int column = 0; column < SIZE / 2; ++column) {
            const std::size_t i = static_cast<std::size_t>(row * SIZE + column) * 4;
            ASSERT_NEAR(m[i], 0.0f, 0.01f) << column << ", " << row;
            ASSERT_NEAR(m[i + 1], 0.0f, 0.01f);
            for (std::size_t c = 0; c < 4; ++c)
                ASSERT_EQ(blurred[i + c], scene[i + c]) << column << ", " << row;
        }
}

// TURNED ON WITH NOTHING MOVING the pass draws exactly as with motion blur off -- with Hdr off and no bloom, where
// blurring would take it through the chain (and its own 4-sample target), and allocates nothing: last frame's camera
// is this one, one draw's Previous is where it stands, and the other has none. And the same with no PreviousCamera.
// Mutation: a camera taken as moved whenever there is a PreviousCamera sends the first through the chain.
TEST_F(MotionBlurPass, TurnedOnWithNothingMovingItDrawsAsOff) {
    BRITE::ModelDrawCommand placed = Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 40, 40, 255});
    placed.Rotation = {0.0f, 0.0f, 0.1305262f, 0.9914449f}; // 15 degrees about z: edges that anti-aliasing changes
    BRITE::RenderPass off;
    off.ModelCommands = {Field({20, 30, 60, 255}), placed};
    const std::vector<BRITE::Color> before = Picture(off);

    placed.Previous = BRITE::Placement{placed.Position, placed.Rotation, placed.Scale};
    BRITE::RenderPass on = Pass({Field({20, 30, 60, 255}), placed}, 0.0f);
    on.Post.Samples = 4;
    EXPECT_TRUE(Same(Picture(on), before));
    on.Post.MotionBlur.PreviousCamera.reset();
    EXPECT_TRUE(Same(Picture(on), before));
    int width = 0, height = 0;
    std::vector<float> rgba;
    EXPECT_FALSE(m_backend.ReadLastMotionVectors(width, height, rgba));
    EXPECT_EQ(m_backend.MotionBlurTargetSetsHeld(), 0);
    EXPECT_EQ(m_backend.HdrTargetSetsHeld(), 0);
}

// WITHOUT A PREVIOUS CAMERA a moving draw's motion is seen through THIS frame's camera at both ends: the quad that
// stood 0.5 to the left moved +8 pixels, and the field, which says nothing, none.
// Mutation: last frame's view-projection, with no PreviousCamera, made from an empty camera instead of this one.
TEST_F(MotionBlurPass, WithoutAPreviousCameraADrawMovesThroughThisFramesView) {
    BRITE::ModelDrawCommand quad = Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 240, 240, 255});
    quad.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass = Pass({Field(), quad}, 0.0f);
    pass.Post.MotionBlur.PreviousCamera.reset();
    Picture(pass);
    const std::vector<float> m = Motion();
    EXPECT_NEAR(MotionX(m, 0.0f, 0.0f), 8.0f, 0.05f);
    EXPECT_NEAR(m[AtLinear(0.0f, 0.0f) + 1], 0.0f, 0.05f);
    EXPECT_NEAR(MotionX(m, -1.5f, 1.5f), 0.0f, 0.01f);
}

// TWO SURFACES CLOSE IN DEPTH: a moving quad (x -1 to 1) 0.008 behind a still one (x -0.5 to 0.5), 5 and 5.008 from
// the camera, 0.16% apart, under a camera stepped 0.25 (-4 pixels). Where the still one stands it keeps the camera's
// -4; the moving one's motion, +8 of its own and -4 of the camera's, +4, shows only around it. The velocity draw's
// slack is four steps of the 24-bit depth buffer -- on this view, whose depth runs linearly over the 3999.95 between
// the near and far planes, 4 x 3999.95 / 2^24 = 0.00095 -- plus twice the fragment's slope across a pixel, zero for a
// quad facing the camera.
// Mutation: the slack as 0.2% of the distance (0.01 here) lets the moving quad's +4 through the still one.
TEST_F(MotionBlurPass, AMovingDrawJustBehindAStillOneDoesNotShowThroughIt) {
    BRITE::ModelDrawCommand behind = Unlit(Quad(-1.0f, -1.0f, 1.0f, 1.0f, -0.008f), {240, 240, 240, 255});
    behind.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass =
        Pass({Field(), Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {40, 200, 40, 255}), behind}, 0.25f);
    Picture(pass);
    const std::vector<float> m = Motion();
    EXPECT_NEAR(MotionX(m, 0.0f, 0.0f), -4.0f, 0.05f);
    EXPECT_NEAR(MotionX(m, 0.3f, -0.3f), -4.0f, 0.05f);
    EXPECT_NEAR(MotionX(m, 0.75f, 0.0f), 4.0f, 0.05f);
}

// A FAR MOVER FINDS ITSELF: a quad 1000 along a 90-degree perspective view, from the camera at the origin looking down
// -z, moved 100 along x. At 1000 the view is tan(45) x 1000 = 1000 each way, so it moved 100 / 1000 = 0.1 in ndc, 0.1
// x 0.5 x 64 = +3.2 pixels. There a depth step is about 1000^2 / (0.05 x 2^24) = 1.19, and the slack, four of them,
// follows it: the quad's own depth, computed again for its motion, still matches what the scene stored.
// Mutation: last frame's camera made from an empty one reads no motion. (The slack without its depth steps does not
// turn it red: here the mover's depth and the scene's agree exactly, and it is the near cases the steps keep green.)
TEST_F(MotionBlurPass, AFarMovingDrawFindsItsOwnDepth) {
    BRITE::ModelDrawCommand far = Unlit(Quad(-800.0f, -800.0f, 800.0f, 800.0f, -1000.0f), {240, 240, 240, 255});
    far.Previous = BRITE::Placement{{-100.0f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass;
    pass.Post.Samples = 1;
    pass.Post.MotionBlur.Enabled = true;
    pass.Post.MotionBlur.Shutter = 1.0f;
    pass.ModelCommands = {far};
    BRITE::Camera3D camera = Perspective({0, 0, 0}, {0, 0, -1}, 90.0f);
    const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
    pass.TargetFramebuffer = target;
    pass.Camera3DPtr = &camera;
    m_backend.SubmitRenderPass(pass);
    m_backend.UnloadRenderTexture(target);
    const std::vector<float> m = Motion();
    // The quad reaches ndc +-0.8 (800 / 1000), columns and rows 6.4 to 57.6: every pixel from 8 to 55 is on it.
    int found = 0;
    for (int row = 8; row < SIZE - 8; ++row)
        for (int column = 8; column < SIZE - 8; ++column)
            found += std::fabs(m[static_cast<std::size_t>(row * SIZE + column) * 4] - 3.2f) < 0.05f ? 1 : 0;
    EXPECT_EQ(found, (SIZE - 16) * (SIZE - 16));
}

// AN OBJECT'S MOTION, under a still camera: a quad that stood 0.5 to the left last frame has moved 0.5 x 16 = +8
// pixels; the field behind it, which says nothing, has not moved.
// Mutation: the velocity draw's last-frame placement taken as this frame's reads 0 on the quad.
TEST_F(MotionBlurPass, AMovingDrawCarriesItsOwnMotion) {
    BRITE::ModelDrawCommand quad = Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 240, 240, 255});
    quad.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass = Pass({Field(), quad}, 0.0f);
    Picture(pass);
    const std::vector<float> m = Motion();
    EXPECT_NEAR(MotionX(m, 0.0f, 0.0f), 8.0f, 0.05f);
    EXPECT_NEAR(MotionX(m, -1.5f, 1.5f), 0.0f, 0.01f);
}

// ... and its streak reaches past its own edge: a white quad from x -0.5 to 0.5 (columns 24 to 39) moving +8 pixels
// smears 4 pixels each way of where it is drawn. The field 2 pixels beyond its right edge (column 41) takes some of
// its light; 8 pixels beyond (column 47), past any reach, none.
// Mutation: the gather searching the pixel's own streak instead of its neighbours' longest leaves column 41 black.
TEST_F(MotionBlurPass, AMovingDrawSmearsOverWhatIsBehindIt) {
    BRITE::ModelDrawCommand quad = Unlit(Quad(-0.5f, -1.0f, 0.5f, 1.0f, 0.0f), {255, 255, 255, 255});
    quad.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass = Pass({Field(), quad}, 0.0f);
    const std::vector<BRITE::Color> picture = Picture(pass);
    auto at = [&](int column) { return picture[static_cast<std::size_t>(32 * SIZE + column)]; };
    EXPECT_GT(at(41).r, 20);
    EXPECT_LT(at(41).r, 250);
    EXPECT_EQ(at(47).r, 0);
    EXPECT_GT(at(32).r, 200); // the middle of the quad stays bright
}

// ... from anywhere in its tile: a small quad from x 0.1 to 0.4, y 0.1 to 0.4 -- pixel columns 34 to 37, rows 26 to 29
// from the top -- lies inside one 32-pixel tile and touches no tile's first row or column, so only a tile that looks
// at every one of its pixels finds its +8. The field 2 pixels beyond its right edge (column 39, row 28) takes its
// light. Mutation: each tile reading one texel of each row and column finds no streak, and column 39 stays black.
TEST_F(MotionBlurPass, ASmallMovingDrawIsFoundAnywhereInItsTile) {
    BRITE::ModelDrawCommand quad = Unlit(Quad(0.1f, 0.1f, 0.4f, 0.4f, 0.0f), {255, 255, 255, 255});
    quad.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass = Pass({Field(), quad}, 0.0f);
    const std::vector<BRITE::Color> picture = Picture(pass);
    EXPECT_GT(picture[static_cast<std::size_t>(28 * SIZE + 39)].r, 20);
    EXPECT_EQ(picture[static_cast<std::size_t>(28 * SIZE + 45)].r, 0);
}

// A TRANSLUCENT MESH writes no motion of its own: a half-transparent quad that moved, lying on the still field (at its
// very depth, where the depth alone cannot tell them apart), takes the field's (none).
// Mutation: translucent meshes drawn into the velocity target reads +8.
TEST_F(MotionBlurPass, ATranslucentMeshTakesTheMotionBehindIt) {
    BRITE::ModelDrawCommand glass = Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, -1.0f), {240, 240, 240, 128});
    glass.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass = Pass({Field(), glass}, 0.0f);
    Picture(pass);
    EXPECT_NEAR(MotionX(Motion(), 0.0f, 0.0f), 0.0f, 0.01f);
}

// THE OPT-OUT: a small bright quad with MotionBlurred off, under a camera pan of -4 pixels, is marked exempt, keeps its
// own colour exactly, and is gathered into no neighbour: the field 2 pixels to its side stays the field's colour.
// Mutation: MotionBlurred ignored (no exempt draw) streaks the quad: its middle is no longer exactly its colour, and
// the field beside it takes its light.
TEST_F(MotionBlurPass, AnOptedOutDrawIsNeitherSmearedNorSmearedIntoOthers) {
    BRITE::ModelDrawCommand light = Unlit(Quad(-0.25f, -0.25f, 0.25f, 0.25f, 0.0f), {255, 255, 200, 255});
    light.MotionBlurred = false;
    BRITE::RenderPass pass = Pass({Field({10, 10, 30, 255}), light}, 0.25f);
    const std::vector<BRITE::Color> picture = Picture(pass);
    const std::vector<float> m = Motion();
    EXPECT_EQ(m[AtLinear(0.0f, 0.0f) + 2], 1.0f);
    EXPECT_TRUE(Within(picture[At(0.0f, 0.0f)], 255, 255, 200, 0));
    // Columns 28 to 35 hold the light; columns 26 and 37 are 2 pixels off its sides, within the 4-pixel streak's reach
    // of 2 each way.
    EXPECT_TRUE(Within(picture[static_cast<std::size_t>(32 * SIZE + 26)], 10, 10, 30, 1));
    EXPECT_TRUE(Within(picture[static_cast<std::size_t>(32 * SIZE + 37)], 10, 10, 30, 1));
}

// ... and its edge with it: the pixels one beyond it, which a draw covers only in part where its edge falls inside a
// pixel, are exempt too; two beyond, not. The light spans columns 28 to 35 of row 32 from the top, row 31 from the
// bottom as the motion target is read.
// Mutation: the one-pixel widening removed from motion_resolve.fs leaves columns 27 and 36 unmarked.
TEST_F(MotionBlurPass, AnOptedOutDrawsEdgeIsExemptToo) {
    BRITE::ModelDrawCommand light = Unlit(Quad(-0.25f, -0.25f, 0.25f, 0.25f, 0.0f), {255, 255, 200, 255});
    light.MotionBlurred = false;
    BRITE::RenderPass pass = Pass({Field({10, 10, 30, 255}), light}, 0.25f);
    Picture(pass);
    const std::vector<float> m = Motion();
    auto exempt = [&](int column) { return m[static_cast<std::size_t>(31 * SIZE + column) * 4 + 2]; };
    EXPECT_EQ(exempt(27), 1.0f);
    EXPECT_EQ(exempt(36), 1.0f);
    EXPECT_EQ(exempt(26), 0.0f);
    EXPECT_EQ(exempt(37), 0.0f);
}

// ... nor smeared into a draw that passes IN FRONT of it: a dark quad at z 0.5 from x 0.3 to 1 (columns 37 to 47; the
// light ends at column 35) moving +8 pixels, so it smears 4 each way, over the still light at z 0. The quad's edge
// pixel, column 37, gathers along its streak to column 33, over the light, and takes the field behind its edge -- never
// the light: its red stays at or below the quad's own 40.
// Mutation: the gather not skipping an exempt tap gives column 37 the light's red.
TEST_F(MotionBlurPass, AnOptedOutDrawIsNotSmearedIntoADrawPassingInFrontOfIt) {
    BRITE::ModelDrawCommand light = Unlit(Quad(-0.25f, -0.25f, 0.25f, 0.25f, 0.0f), {255, 255, 200, 255});
    light.MotionBlurred = false;
    BRITE::ModelDrawCommand passing = Unlit(Quad(0.3f, -1.0f, 1.0f, 1.0f, 0.5f), {40, 40, 40, 255});
    passing.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass pass = Pass({Field({10, 10, 30, 255}), light, passing}, 0.0f);
    const std::vector<BRITE::Color> picture = Picture(pass);
    EXPECT_NEAR(MotionX(Motion(), 0.6f, 0.0f), 8.0f, 0.05f);
    EXPECT_LE(picture[static_cast<std::size_t>(32 * SIZE + 37)].r, 40);
    EXPECT_TRUE(Within(picture[At(0.0f, 0.0f)], 255, 255, 200, 0));
}

// A CUT draws the frame exactly as with motion blur off: the caller's Cut, and a jump beyond CutLength (the step's 4
// pixels against a CutLength of 3). Neither draws a blur.
// Mutation: WantsMotionBlur ignoring IsCut draws the blur on both.
TEST_F(MotionBlurPass, ACutDrawsTheFrameUnblurred) {
    const std::vector<BRITE::ModelDrawCommand> draws = {
        Field({20, 30, 60, 255}), Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 40, 40, 255})};
    BRITE::RenderPass blurred = Pass(draws, 0.25f);
    Picture(blurred); // a blurred pass first, so a read-back after the cut cannot be this one's
    BRITE::RenderPass off = Pass(draws, 0.25f);
    off.Post.MotionBlur.Enabled = false;
    const std::vector<BRITE::Color> unblurred = Picture(off);

    BRITE::RenderPass cut = Pass(draws, 0.25f);
    cut.Post.MotionBlur.Cut = true;
    EXPECT_TRUE(Same(Picture(cut), unblurred));
    int width = 0, height = 0;
    std::vector<float> rgba;
    EXPECT_FALSE(m_backend.ReadLastMotionVectors(width, height, rgba));

    BRITE::RenderPass jump = Pass(draws, 0.25f);
    jump.Post.MotionBlur.CutLength = 3.0f;
    EXPECT_TRUE(Same(Picture(jump), unblurred));
    EXPECT_FALSE(m_backend.ReadLastMotionVectors(width, height, rgba));
}

// OFF costs nothing: a pass with everything set but Enabled makes no motion target and no chain at all, and draws what
// a pass that never touched motion blur draws.
// Mutation: WantsMotionBlur ignoring Enabled makes a set of targets.
TEST_F(MotionBlurPass, OffAllocatesNothingAndDrawsAsBefore) {
    BRITE::ModelDrawCommand quad = Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 40, 40, 255});
    BRITE::RenderPass untouched;
    untouched.ModelCommands = {Field({20, 30, 60, 255}), quad};
    const std::vector<BRITE::Color> before = Picture(untouched);

    quad.Previous = BRITE::Placement{{-0.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::RenderPass off = Pass({Field({20, 30, 60, 255}), quad}, 0.25f);
    off.Post.MotionBlur.Enabled = false;
    off.Post.Samples = 4;
    EXPECT_TRUE(Same(Picture(off), before));
    EXPECT_EQ(m_backend.MotionBlurTargetSetsHeld(), 0);
    EXPECT_EQ(m_backend.HdrTargetSetsHeld(), 0);
}

// THE 2D DRAWS ARE NEVER BLURRED: a rectangle over a panning world is drawn after the blur, exactly its colour to its
// edge.
TEST_F(MotionBlurPass, TheTwoDimensionalDrawsAreNotBlurred) {
    BRITE::RenderPass pass =
        Pass({Field({20, 30, 60, 255}), Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 40, 40, 255})}, 0.25f);
    pass.RectCommands.push_back({{0, 0, 16, 64}, {0, 0}, 0.0f, {250, 250, 10, 255}, true});
    const std::vector<BRITE::Color> picture = Picture(pass);
    for (int row = 0; row < SIZE; ++row)
        ASSERT_TRUE(Within(picture[static_cast<std::size_t>(row * SIZE + 15)], 250, 250, 10, 0)) << row;
    EXPECT_FALSE(Within(picture[static_cast<std::size_t>(32 * SIZE + 24)], 240, 40, 40, 0)); // the quad's edge smears
}

// A DEVICE THAT REFUSES THE BLUR'S SHADERS draws the pass as if motion blur were off -- never black.
// Mutation: SubmitRenderPass ignoring MotionReady's refusal draws through the chain with no programs.
TEST_F(MotionBlurPass, ADeviceThatRefusesItDrawsWithoutIt) {
    const std::vector<BRITE::ModelDrawCommand> draws = {
        Field({20, 30, 60, 255}), Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 40, 40, 255})};
    BRITE::RenderPass off = Pass(draws, 0.25f);
    off.Post.MotionBlur.Enabled = false;
    const std::vector<BRITE::Color> unblurred = Picture(off);
    m_backend.RefuseMotionBlurForTest();
    BRITE::RenderPass on = Pass(draws, 0.25f);
    EXPECT_TRUE(Same(Picture(on), unblurred));
    int width = 0, height = 0;
    std::vector<float> rgba;
    EXPECT_FALSE(m_backend.ReadLastMotionVectors(width, height, rgba));
}

// A DEVICE THAT CANNOT MAKE ITS TARGETS: the pass, with Hdr off and no bloom, is drawn exactly as with motion blur
// off -- never through the chain unblurred -- the size is not asked again, and nothing is held.
// Mutation: the targets made only once the scene is drawn (readiness decided after routing) sends the pass through
// the chain: its 4-sample target makes a set the HDR chain holds.
TEST_F(MotionBlurPass, ADeviceThatCannotMakeItsTargetsDrawsWithoutIt) {
    const std::vector<BRITE::ModelDrawCommand> draws = {
        Field({20, 30, 60, 255}), Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 40, 40, 255})};
    BRITE::RenderPass off = Pass(draws, 0.25f);
    off.Post.MotionBlur.Enabled = false;
    const std::vector<BRITE::Color> unblurred = Picture(off);
    m_backend.RefuseMotionBlurTargetsForTest();
    for (int frame = 0; frame < 2; ++frame) {
        BRITE::RenderPass on = Pass(draws, 0.25f);
        on.Post.Samples = 4;
        EXPECT_TRUE(Same(Picture(on), unblurred)) << frame;
    }
    int width = 0, height = 0;
    std::vector<float> rgba;
    EXPECT_FALSE(m_backend.ReadLastMotionVectors(width, height, rgba));
    EXPECT_EQ(m_backend.MotionBlurTargetSetsHeld(), 0);
    EXPECT_EQ(m_backend.HdrTargetSetsHeld(), 0);
    // Refused once at this size, it was not asked again on the second frame.
    // Mutation: the refusal not remembered tries again every frame, 2.
    EXPECT_EQ(m_backend.MotionBlurTargetSetsMade(), 1);
}

// THE TARGETS ARE KEPT: three passes at one size make one set, and a change of MaxLength alone -- the gather's tile --
// remakes only the tile's three targets, not the set. And they ARE remade: at 16 the tiles are 4 x 4 of 16 pixels, and
// the quad's right edge (column 39) lies in the third column of tiles, which a 2 x 2 set of 32-pixel tiles kept from
// before does not hold. Under the camera's -4 (2 pixels each way), the field 1 pixel beyond either edge -- columns 23
// and 40 -- takes the red quad's light.
// Mutation: a change of tile freeing the whole set makes 2; the tile's three not remade (the 32-pixel ones kept, the
// tile changed) leaves column 40 black.
TEST_F(MotionBlurPass, TheTargetsAreKeptAcrossPassesAndAChangeOfTile) {
    const std::vector<BRITE::ModelDrawCommand> draws = {
        Field(), Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 40, 40, 255})};
    std::vector<BRITE::Color> picture;
    for (const float longest : {32.0f, 32.0f, 16.0f}) {
        BRITE::RenderPass pass = Pass(draws, 0.25f);
        pass.Post.MotionBlur.MaxLength = longest;
        picture = Picture(pass);
        EXPECT_NEAR(MotionX(Motion(), 0.0f, 0.0f), -4.0f, 0.05f) << longest;
    }
    EXPECT_EQ(m_backend.MotionBlurTargetSetsMade(), 1);
    EXPECT_EQ(m_backend.MotionBlurTargetSetsHeld(), 1);
    EXPECT_GT(picture[static_cast<std::size_t>(32 * SIZE + 23)].r, 20);
    EXPECT_GT(picture[static_cast<std::size_t>(32 * SIZE + 40)].r, 20);
}

// THE TARGETS ARE FREED WHEN IDLE, and made again at a new size. One blurred pass, then IdlePasses + 1 passes without
// motion blur: the set was last used on the first pass, so on the last it has stood unused for IdlePasses + 1 passes,
// more than IdlePasses, and is freed. Then a blurred pass on a 32 x 32 target makes a second set.
// Mutation: the idle set not freed in ForgetIdle leaves one held.
TEST_F(MotionBlurPass, TheTargetsAreFreedWhenIdleAndMadeAgainAtANewSize) {
    const std::vector<BRITE::ModelDrawCommand> draws = {
        Field(), Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {240, 40, 40, 255})};
    BRITE::RenderPass blurred = Pass(draws, 0.25f);
    Picture(blurred);
    EXPECT_EQ(m_backend.MotionBlurTargetSetsHeld(), 1);
    const BRITE::TextureHandle small = m_backend.LoadRenderTexture(32, 32);
    const std::uint64_t idle = BRITE::Backends::Raylib::RaylibRenderBackend::HdrIdlePasses();
    for (std::uint64_t i = 0; i < idle + 1; ++i) {
        BRITE::RenderPass plain; // no 3D camera, no blur: a pass that only counts
        plain.TargetFramebuffer = small;
        m_backend.SubmitRenderPass(plain);
    }
    EXPECT_EQ(m_backend.MotionBlurTargetSetsHeld(), 0);

    BRITE::RenderPass again = Pass(draws, 0.25f);
    BRITE::Camera3D camera = HiddenWindow::Camera();
    again.TargetFramebuffer = small;
    again.Camera3DPtr = &camera;
    m_backend.SubmitRenderPass(again);
    int width = 0, height = 0;
    std::vector<float> rgba;
    EXPECT_TRUE(m_backend.ReadLastMotionVectors(width, height, rgba));
    EXPECT_EQ(width, 32);
    EXPECT_EQ(m_backend.MotionBlurTargetSetsMade(), 2);
    EXPECT_EQ(m_backend.MotionBlurTargetSetsHeld(), 1);
    m_backend.UnloadRenderTexture(small);
}

// THE PATHS IT TAKES THROUGH THE CHAIN. Under HDR: the same -4 pixels, and the blur smears linear light, before the
// tone map: a quad of emission 4 (light past white) streaks as light.
TEST_F(MotionBlurPass, UnderHdrItBlursLinearLight) {
    BRITE::ModelDrawCommand bright = Unlit(Quad(-0.5f, -1.0f, 0.5f, 1.0f, 0.0f), {0, 0, 0, 255});
    bright.Material.Emission = {255, 255, 255, 255};
    bright.Material.EmissionStrength = 4.0f;
    BRITE::RenderPass pass = Pass({Field(), bright}, 0.25f, true);
    Picture(pass);
    EXPECT_NEAR(MotionX(Motion(), 0.0f, 0.0f), -4.0f, 0.05f);
    int width = 0, height = 0;
    std::vector<float> blurred;
    ASSERT_TRUE(m_backend.ReadLastMotionBlurred(width, height, blurred));
    // The quad's middle keeps its light; a pixel of the field 1 beyond its edge (column 40) takes some of it.
    EXPECT_NEAR(blurred[static_cast<std::size_t>(32 * SIZE + 32) * 4], 4.0f, 0.05f);
    EXPECT_GT(blurred[static_cast<std::size_t>(32 * SIZE + 40) * 4], 0.1f);
}

// With Hdr off and the display-referred bloom on, the blur runs on the display colours before the bloom.
TEST_F(MotionBlurPass, WithHdrOffAndBloomItBlursTheDisplayColours) {
    BRITE::RenderPass pass = Pass({Field(), Unlit(Quad(-0.5f, -1.0f, 0.5f, 1.0f, 0.0f), {255, 255, 255, 255})}, 0.25f);
    pass.Post.Bloom.Enabled = true;
    pass.Post.Bloom.Threshold = 0.8f;
    Picture(pass);
    EXPECT_NEAR(MotionX(Motion(), 0.0f, 0.0f), -4.0f, 0.05f);
    int width = 0, height = 0;
    std::vector<float> blurred;
    ASSERT_TRUE(m_backend.ReadLastMotionBlurred(width, height, blurred));
    EXPECT_NEAR(blurred[static_cast<std::size_t>(32 * SIZE + 32) * 4], 1.0f, 0.01f);
    EXPECT_GT(blurred[static_cast<std::size_t>(32 * SIZE + 40) * 4], 0.05f);
}

// An instanced draw, per copy: of two copies under a still camera, the one that moved 0.5 carries +8 pixels, the one
// with no Previous none.
// Mutation: every copy given the first copy's last-frame placement moves the still one too.
TEST_F(MotionBlurPass, AnInstancedDrawCarriesEachCopysMotion) {
    BRITE::InstancedModelDrawCommand copies;
    copies.Model = Quad(-0.25f, -0.25f, 0.25f, 0.25f, 0.0f);
    copies.Material.Unlit = true;
    copies.Material.DoubleSided = true;
    BRITE::ModelInstance moving;
    moving.Position = {-1.0f, 0.0f, 0.0f};
    moving.Previous = BRITE::Placement{{-1.5f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}};
    BRITE::ModelInstance still;
    still.Position = {1.0f, 0.0f, 0.0f};
    copies.Instances = {moving, still};
    BRITE::RenderPass pass = Pass({Field()}, 0.0f);
    pass.InstancedModelCommands = {copies};
    Picture(pass);
    const std::vector<float> m = Motion();
    EXPECT_NEAR(MotionX(m, -1.0f, 0.0f), 8.0f, 0.05f);
    EXPECT_NEAR(MotionX(m, 1.0f, 0.0f), 0.0f, 0.01f);
}

// AN INSTANCED DRAW CAN OPT OUT, every copy: two small copies with MotionBlurred off under the camera's pan are marked
// exempt where each stands -- each copy's placement uploaded once and read as now and as then -- and the field between
// them keeps the camera's -4. The copies are 0.5 wide at x -1 and 1: columns 12 to 19 and 44 to 51.
// Mutation: the one-matrix upload read with a 32-float stride marks the second copy nowhere it stands.
TEST_F(MotionBlurPass, AnInstancedDrawCanOptOut) {
    BRITE::InstancedModelDrawCommand lights;
    lights.Model = Quad(-0.25f, -0.25f, 0.25f, 0.25f, 0.0f);
    lights.Material.Unlit = true;
    lights.Material.DoubleSided = true;
    lights.MotionBlurred = false;
    BRITE::ModelInstance left, right;
    left.Position = {-1.0f, 0.0f, 0.0f};
    right.Position = {1.0f, 0.0f, 0.0f};
    lights.Instances = {left, right};
    BRITE::RenderPass pass = Pass({Field()}, 0.25f);
    pass.InstancedModelCommands = {lights};
    Picture(pass);
    const std::vector<float> m = Motion();
    EXPECT_EQ(m[AtLinear(-1.0f, 0.0f) + 2], 1.0f);
    EXPECT_EQ(m[AtLinear(1.0f, 0.0f) + 2], 1.0f);
    EXPECT_EQ(m[AtLinear(0.0f, 0.0f) + 2], 0.0f);
    EXPECT_NEAR(MotionX(m, 0.0f, 0.0f), -4.0f, 0.05f);
}
