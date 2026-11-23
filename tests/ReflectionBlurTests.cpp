// The planar reflection's blur (RenderPass.hpp, ReflectionBlurSettings): its plan on the CPU (ReflectionBlur.hpp), then
// the blur on the GPU under a hidden window (HiddenWindowScene.hpp).
//
// THE GL CASES' SCENE is ReflectionTests' lake: a perspective camera at (0, 2, 6) looking at the origin, 45 degrees,
// over black water reflecting in the plane y = 0, a red unlit cube floating above it, the sky blue. Drawn into a 256 x
// 256 target at Resolution 1, the reflection is 256 x 256 texels, so a Softness s spreads s x 256 texels across. What
// the blur does is read back from the reflection itself (ReadLastPlanarReflection), by the red channel alone -- only
// the cube is red -- as its second moments: a blur is a convolution, so the variance of what is blurred is the variance
// of the sharp picture PLUS the kernel's, exactly, and the blur's spread is the square root of the difference. Both
// pictures are drawn, so the cube's own size never needs working out.
//
// A pass gathers 3 standard deviations either side and no further (ReflectionBlur.hpp): a Gaussian cut off at c
// standard deviations keeps a share 1 - 2 c phi(c) / (2 Phi(c) - 1) of its variance (phi, Phi the normal density and
// distribution), worked beside each case where it matters.
//
// Beside each case is the mutation that turns it red.

#include <Backends/ReflectionBlur.hpp>

#include "HiddenWindowScene.hpp"

#include <cmath>
#include <limits>
#include <vector>

namespace {

namespace RB = BRITE::ReflectionBlur;

bool Same(const std::vector<BRITE::Color>& a, const std::vector<BRITE::Color>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b || a[i].a != b[i].a)
            return false;
    return true;
}

BRITE::ReflectionBlurSettings On(float softness, float stretch) {
    BRITE::ReflectionBlurSettings blur;
    blur.Enabled = true;
    blur.Softness = softness;
    blur.Stretch = stretch;
    return blur;
}

// The red channel's total and its spread each way, in texels: the square roots of its second moments about its centre.
struct Spread {
    double total = 0.0;
    double across = 0.0;
    double down = 0.0;
};
Spread RedSpread(const std::vector<float>& rgba, int width, int height) {
    double sum = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const double r = rgba[static_cast<std::size_t>(y * width + x) * 4];
            sum += r;
            sx += r * x;
            sy += r * y;
            sxx += r * x * x;
            syy += r * y * y;
        }
    Spread s;
    s.total = sum;
    if (sum > 0.0) {
        const double mx = sx / sum, my = sy / sum;
        s.across = std::sqrt(std::max(sxx / sum - mx * mx, 0.0));
        s.down = std::sqrt(std::max(syy / sum - my * my, 0.0));
    }
    return s;
}

// What a blur added: the spread of the blurred picture less the sharp one's, in quadrature.
double Added(double blurred, double sharp) {
    return std::sqrt(std::max(blurred * blurred - sharp * sharp, 0.0));
}

class ReflectionBlurPass : public HiddenWindow::Scene {
  protected:
    static constexpr int BIG = 256;

    BRITE::ModelHandle Water() {
        BRITE::MeshData water;
        water.Positions = {{-10, 0, 10}, {10, 0, 10}, {10, 0, -10}, {-10, 0, -10}};
        water.Normals.assign(4, {0, 1, 0});
        water.Indices = {0, 1, 2, 0, 2, 3};
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(water);
        m_models.push_back(model);
        return model;
    }
    // The lake with a red cube `half` across each way at (0, 1, -3), under HDR or not, its reflection at `resolution`.
    BRITE::RenderPass Lake(bool hdr, float half = 0.1f, float resolution = 1.0f) {
        if (m_water == BRITE::NullModelHandle)
            m_water = Water();
        BRITE::RenderPass pass;
        pass.Post.Hdr = hdr;
        pass.Post.Samples = 1;
        pass.ClearColor = {0, 0, 255, 255};
        pass.Reflections.Planar = true;
        pass.Reflections.Resolution = resolution;
        BRITE::ModelDrawCommand water = Unlit(m_water, {0, 0, 0, 255});
        water.Material.Unlit = false;
        water.Material.DoubleSided = false;
        water.Material.Reflection = BRITE::ReflectionMode::Planar;
        water.Material.ReflectionF0 = 0.02f;
        water.Material.ReflectionDistortion = 0.0f;
        pass.ModelCommands = {water, Unlit(Box(0.0f, 1.0f, -3.0f, half), {255, 0, 0, 255})};
        return pass;
    }
    // `pass` drawn into a fresh `size` x `size` target, read back; its reflection into `reflection`.
    std::vector<BRITE::Color> Draw(BRITE::RenderPass& pass, std::vector<float>* reflection = nullptr, int size = BIG) {
        BRITE::Camera3D camera{{0, 2, 6}, {0, 0, 0}, {0, 1, 0}, 45.0f, BRITE::CameraProjection::Perspective};
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(size, size);
        pass.TargetFramebuffer = target;
        pass.Camera3DPtr = &camera;
        m_backend.SubmitRenderPass(pass);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        if (reflection != nullptr) {
            EXPECT_TRUE(m_backend.ReadLastPlanarReflection(m_width, m_height, *reflection));
        }
        m_backend.UnloadRenderTexture(target);
        pass.Camera3DPtr = nullptr;
        return pixels;
    }
    BRITE::ModelHandle m_water = BRITE::NullModelHandle;
    int m_width = 0, m_height = 0;
};

} // namespace

// ---------------------------------------------------------------------------
// The plan, by hand
// ---------------------------------------------------------------------------

// A 1920 x 1080 reflection, Softness 0.004, Stretch 6: across 0.004 x 1080 = 4.32 texels, down 4.32 x 6 = 25.92.
//   Across, 4.32 <= 16: the reflection itself, level 0. Down, 25.92 > 16 but <= 32: level 1, which adds 2^2 / 4 = 1 to
//   each axis (ReflectionBlur.hpp). The levels together add 0 + 1 = 1.
//   The across pass: sqrt(4.32^2 - 1) = sqrt(18.6624 - 1) = 4.202666 texels, 3 x that = 12.608 -> 13 taps, a step of
//   1 / 1920 = 0.000520833.
//   The down pass: sqrt(25.92^2 - 1) = sqrt(671.8464 - 1) = 25.900703 of the reflection's texels, at level 1 (540 high)
//   x 540/1080 = 12.950351, 3 x that = 38.851 -> 39 taps, a step of 1 / 540 = 0.00185185.
// Mutations: Softness taken of the width -> across 7.68, red; Stretch ignored -> down 4.32, red; the levels'
//   variance not taken off -> 4.32 and 12.96, red; the down pass's spread left in the reflection's texels -> 25.9, red.
TEST(ReflectionBlurMaths, ThePlanIsTheSoftnessOfTheHeightAndItsStretch) {
    const RB::Plan plan = RB::PlanFor(On(0.004f, 6.0f), 1920, 1080);
    ASSERT_TRUE(plan.Blurs);
    EXPECT_NEAR(plan.Across, 4.32f, 1e-4f);
    EXPECT_NEAR(plan.Down, 25.92f, 1e-4f);
    EXPECT_EQ(plan.AcrossPass.Level, 0);
    EXPECT_NEAR(plan.AcrossPass.Spread, 4.202666f, 1e-4f);
    EXPECT_EQ(plan.AcrossPass.Taps, 13);
    EXPECT_NEAR(plan.AcrossPass.Step, 0.000520833f, 1e-8f);
    EXPECT_EQ(plan.DownPass.Level, 1);
    EXPECT_NEAR(plan.DownPass.Spread, 12.950351f, 1e-3f);
    EXPECT_EQ(plan.DownPass.Taps, 39);
    EXPECT_NEAR(plan.DownPass.Step, 0.00185185f, 1e-7f);
}

// What a level adds: 2^L squared over 4 -- level 1 adds 1, level 2 adds 4, level 3 adds 16 -- and the reflection
// itself nothing. And the level a spread is read from: the finest at which it is no more than 16 of the level's texels
// -- 16 at level 0, 16.01 and 32 at 1, 32.1 at 2 -- and no coarser than the reflection's shorter side holds: a spread
// of 100 on a side of 4 stops at level 2 (4 texels a level-2 texel; level 3 would want 8). Mutations: the bilinear
// read's (2n^2 + 1) / 12 dropped -> (n^2 - 1) / 12, 0.25 at level 1, red; "<=" for "<" in the
//   level's test -> 16 read at level 1, red; the side's hold dropped -> level 3, red.
TEST(ReflectionBlurMaths, ALevelIsTheFinestThatHoldsItsSpread) {
    EXPECT_EQ(RB::LevelVariance(0), 0.0f);
    EXPECT_NEAR(RB::LevelVariance(1), 1.0f, 1e-6f);
    EXPECT_NEAR(RB::LevelVariance(2), 4.0f, 1e-6f);
    EXPECT_NEAR(RB::LevelVariance(3), 16.0f, 1e-6f);
    EXPECT_EQ(RB::LevelFor(16.0f, 1080), 0);
    EXPECT_EQ(RB::LevelFor(16.01f, 1080), 1);
    EXPECT_EQ(RB::LevelFor(32.0f, 1080), 1);
    EXPECT_EQ(RB::LevelFor(32.1f, 1080), 2);
    EXPECT_EQ(RB::LevelFor(100.0f, 4), 2);
}

// The holds, on a reflection 100 high. Softness 0.5 is held to 0.01: across 1. Stretch 50 is held to 12: down 12 (with
// Softness 0.01); 0.2, or not a number, to 1. A Softness that asks less than one texel across -- 0.001 x 100 = 0.1 --
// is the floor, 1 across, and its Stretch 3 makes 3 down: a reflection drawn small reads soft, not as blocks. A
// Softness of 0, below it or not a number, and a blur not Enabled, blur nothing. Mutations: the Softness not held ->
// across 50, red; the Stretch not held -> 50 down, red; the floor dropped -> 0.1
//   across, red; Enabled ignored -> a blur, red.
TEST(ReflectionBlurMaths, TheHoldsAndTheFloor) {
    EXPECT_NEAR(RB::PlanFor(On(0.5f, 1.0f), 100, 100).Across, 1.0f, 1e-5f);
    EXPECT_NEAR(RB::PlanFor(On(0.01f, 50.0f), 100, 100).Down, 12.0f, 1e-4f);
    EXPECT_NEAR(RB::PlanFor(On(0.01f, 0.2f), 100, 100).Down, 1.0f, 1e-5f);
    EXPECT_NEAR(RB::PlanFor(On(0.01f, std::numeric_limits<float>::quiet_NaN()), 100, 100).Down, 1.0f, 1e-5f);
    const RB::Plan small = RB::PlanFor(On(0.001f, 3.0f), 100, 100);
    ASSERT_TRUE(small.Blurs);
    EXPECT_NEAR(small.Across, 1.0f, 1e-6f);
    EXPECT_NEAR(small.Down, 3.0f, 1e-5f);
    for (const float softness : {0.0f, -0.01f, std::numeric_limits<float>::quiet_NaN()})
        EXPECT_FALSE(RB::PlanFor(On(softness, 4.0f), 100, 100).Blurs) << softness;
    BRITE::ReflectionBlurSettings off = On(0.004f, 4.0f);
    off.Enabled = false;
    EXPECT_FALSE(RB::PlanFor(off, 100, 100).Blurs);
    EXPECT_FALSE(BRITE::ReflectionBlurSettings{}.Enabled) << "off by default";
}

// ---------------------------------------------------------------------------
// The blur, on the GPU
// ---------------------------------------------------------------------------

// THE BLUR SPREADS A LIGHT AS PLANNED, and further down than across. Softness 0.01 on the 256-high reflection: across
// 2.56 texels, Stretch 4: down 10.24, both read at level 0. The passes gather 3 x 2.56 = 7.68 -> 8 taps (c = 8 / 2.56
// = 3.125: phi = 0.003022, a share 1 - 2 x 3.125 x 0.003022 / 0.99822 = 0.98108 of the variance, the spread x 0.99050)
// and 3 x 10.24 = 30.72 -> 31 (c = 3.0273: phi = 0.004087, 1 - 6.0547 x 0.004087 / 0.99753 = 0.97519, x 0.98752). So
// the blur adds 2.56 x 0.9905 = 2.536 across and 10.24 x 0.98752 = 10.112 down. 4%: the Gaussian is sampled at whole
// texels, and the half-float target rounds.
// Mutations: the passes' axes swapped (down across, across down) -> 10.1 across and 2.5 down, red; Softness doubled ->
//   5.1 and 20, red.
TEST_F(ReflectionBlurPass, ItSpreadsALightAsPlannedFurtherDownThanAcross) {
    BRITE::RenderPass sharpPass = Lake(true);
    std::vector<float> sharp, blurred;
    Draw(sharpPass, &sharp);
    EXPECT_FALSE(m_backend.LastPlanarReflectionBlurred());
    BRITE::RenderPass pass = Lake(true);
    pass.Reflections.Blur = On(0.01f, 4.0f);
    Draw(pass, &blurred);
    ASSERT_TRUE(m_backend.LastPlanarReflectionBlurred());
    ASSERT_EQ(m_width, BIG);
    const Spread s = RedSpread(sharp, BIG, BIG), b = RedSpread(blurred, BIG, BIG);
    ASSERT_GT(s.total, 0.0);
    EXPECT_NEAR(Added(b.across, s.across), 2.536, 2.536 * 0.04);
    EXPECT_NEAR(Added(b.down, s.down), 10.112, 10.112 * 0.04);
    // And the next pass that does not blur reads as sharp again. Mutation: the pass's blurred mark never cleared.
    BRITE::RenderPass again = Lake(true);
    Draw(again);
    EXPECT_FALSE(m_backend.LastPlanarReflectionBlurred());
}

// A LONG STREAK READ FROM A COARSER LEVEL KEEPS ITS STRETCH. Softness 0.0055: across 0.0055 x 256 = 1.408, Stretch 12:
// down 16.896 -- beyond 16, so the down pass reads level 1, which spreads BOTH axes by 1 (2^2 / 4). Each pass gathers
// what its axis lacks: across sqrt(1.408^2 - 1) = sqrt(0.982464) = 0.991193 at level 0, 3 x that -> 3 taps (c = 3.0267:
// phi = 0.004094, a share 1 - 6.0533 x 0.004094 / 0.99753 = 0.97516); down sqrt(16.896^2 - 1) = 16.866379, at level 1
// 8.433190 of its texels, 26 taps (c = 3.0831: a share 0.97871).
//   Across, the blur adds 0.991193^2 x 0.97516 + 1 = 0.958061 + 1 = 1.958061: 1.399307.
//   Down, 16.866379^2 x 0.97871 + 1 = 284.475 x 0.97871 + 1 = 278.4185 + 1 = 279.4185: 16.7158.
// 6%. Mutation: what the levels add not taken off -> across 1.408^2 x 0.98 + 1 = 2.943: 1.716, 23% wide, red.
TEST_F(ReflectionBlurPass, ALongStreakReadFromACoarserLevelKeepsItsStretch) {
    BRITE::RenderPass sharpPass = Lake(true);
    std::vector<float> sharp, blurred;
    Draw(sharpPass, &sharp);
    BRITE::RenderPass pass = Lake(true);
    pass.Reflections.Blur = On(0.0055f, 12.0f);
    ASSERT_EQ(RB::PlanFor(pass.Reflections.Blur, BIG, BIG).DownPass.Level, 1);
    Draw(pass, &blurred);
    const Spread s = RedSpread(sharp, BIG, BIG), b = RedSpread(blurred, BIG, BIG);
    EXPECT_NEAR(Added(b.across, s.across), 1.399307, 1.399307 * 0.06);
    EXPECT_NEAR(Added(b.down, s.down), 16.7158, 16.7158 * 0.06);
}

// UNDER HDR A LIGHT PAST WHITE STREAKS AS LIGHT. The cube is red 7.241658 of light in the float reflection (white's,
// ToneCurve): the blur moves light and keeps all of it, so the blurred reflection's red sums to the sharp one's -- the
// weights are normalised by their sum and the cube is far from the edges -- within 1%, and where it is brightest it is
// still beyond white.
// Mutations: the scratch target made in 8 bits whatever the reflection's format -> the across pass held at 1, the sum
//   a seventh, red; the shader holding light to 1 -> the same, red.
TEST_F(ReflectionBlurPass, UnderHdrALightPastWhiteStreaksAsLight) {
    BRITE::RenderPass sharpPass = Lake(true, 0.3f);
    std::vector<float> sharp, blurred;
    Draw(sharpPass, &sharp);
    BRITE::RenderPass pass = Lake(true, 0.3f);
    pass.Reflections.Blur = On(0.004f, 3.0f);
    Draw(pass, &blurred);
    const Spread s = RedSpread(sharp, BIG, BIG), b = RedSpread(blurred, BIG, BIG);
    EXPECT_NEAR(b.total, s.total, s.total * 0.01);
    float brightest = 0.0f;
    for (std::size_t i = 0; i < blurred.size(); i += 4)
        brightest = std::max(brightest, blurred[i]);
    EXPECT_GT(brightest, 1.5f);
}

// WITH HDR OFF IT BLURS THE COLOURS THE SCREEN SHOWS: the reflection is an 8-bit target, the cube's red 1, and the blur
// spreads it as planned. Softness 0.005: 1.28 across, Stretch 2: 2.56 down, 4 and 8 taps (c = 3.125, x 0.99050). The
// blur adds 1.2678 across and 2.5357 down; 8%, for the 8 bits' rounding of the faint ends. Mutation: the blur run only
// for a float reflection -> nothing added, red.
TEST_F(ReflectionBlurPass, WithHdrOffItBlursTheColoursTheScreenShows) {
    BRITE::RenderPass sharpPass = Lake(false, 0.2f);
    std::vector<float> sharp, blurred;
    Draw(sharpPass, &sharp);
    BRITE::RenderPass pass = Lake(false, 0.2f);
    pass.Reflections.Blur = On(0.005f, 2.0f);
    Draw(pass, &blurred);
    ASSERT_TRUE(m_backend.LastPlanarReflectionBlurred());
    const Spread s = RedSpread(sharp, BIG, BIG), b = RedSpread(blurred, BIG, BIG);
    EXPECT_NEAR(Added(b.across, s.across), 1.2678, 1.2678 * 0.08);
    EXPECT_NEAR(Added(b.down, s.down), 2.5357, 2.5357 * 0.08);
}

// A REFLECTION DRAWN SMALL READS SOFT, NOT AS BLOCKS. Resolution 0.125 of the 256 target: a reflection 32 x 32. A
// Softness of 0.001 asks 0.032 of a texel across -- nothing -- and gets the floor, 1 texel; Stretch 3, 3 down. Each
// gathers 3 x its spread, c = 3 exactly: phi(3) = 0.004432, a share 1 - 6 x 0.004432 / 0.99730 = 0.97334, the spread x
// 0.98658. So the blur adds 0.98658 across and 2.9597 down. The cube is 0.4 across each way, about 3 texels of this
// reflection, so a texel more or less of its edge moves its sharp spread: 10%. Mutation: the floor dropped -> 0.032
// across, 0.096 down: nothing added, red.
TEST_F(ReflectionBlurPass, AReflectionDrawnSmallReadsSoftNotAsBlocks) {
    BRITE::RenderPass sharpPass = Lake(true, 0.4f, 0.125f);
    std::vector<float> sharp, blurred;
    Draw(sharpPass, &sharp);
    BRITE::RenderPass pass = Lake(true, 0.4f, 0.125f);
    pass.Reflections.Blur = On(0.001f, 3.0f);
    Draw(pass, &blurred);
    ASSERT_EQ(m_width, 32);
    const Spread s = RedSpread(sharp, 32, 32), b = RedSpread(blurred, 32, 32);
    EXPECT_NEAR(Added(b.across, s.across), 0.98658, 0.098658);
    EXPECT_NEAR(Added(b.down, s.down), 2.9597, 0.29597);
}

// OFF COSTS NOTHING: a pass whose blur has every field set but Enabled -- and one Enabled with a Softness of 0 -- draws
// the picture and the reflection a pass that never touched the blur draws, byte for byte, makes no target and never
// asks for the blur's program.
// Mutation: the plan ignoring Enabled -> the program asked, a target made, the picture blurred, red.
TEST_F(ReflectionBlurPass, OffCostsNothingAndDrawsAsBefore) {
    for (const bool hdr : {true, false}) {
        BRITE::RenderPass untouched = Lake(hdr);
        std::vector<float> before, after;
        const std::vector<BRITE::Color> picture = Draw(untouched, &before);
        BRITE::RenderPass off = Lake(hdr);
        off.Reflections.Blur = On(0.01f, 12.0f);
        off.Reflections.Blur.Enabled = false;
        EXPECT_TRUE(Same(Draw(off, &after), picture)) << hdr;
        EXPECT_EQ(after, before) << hdr;
        BRITE::RenderPass none = Lake(hdr);
        none.Reflections.Blur = On(0.0f, 12.0f);
        EXPECT_TRUE(Same(Draw(none, &after), picture)) << hdr;
        EXPECT_EQ(after, before) << hdr;
    }
    EXPECT_FALSE(m_backend.LastPlanarReflectionBlurred());
    EXPECT_FALSE(m_backend.ReflectionBlurProgramAsked());
    EXPECT_EQ(m_backend.ReflectionBlurTargetsMade(), 0);
    EXPECT_EQ(m_backend.ReflectionBlurTargetsHeld(), 0);
}

// A DEVICE THAT REFUSES THE BLUR'S SHADER draws the sharp reflection -- the picture a pass without the blur draws,
// never a black one -- and holds no target for it. Mutation: the program's refusal ignored -> the passes drawn through
// program 0: a reflection of nothing, red.
TEST_F(ReflectionBlurPass, ADeviceThatRefusesItsShaderDrawsTheSharpReflection) {
    BRITE::RenderPass sharpPass = Lake(true);
    std::vector<float> sharp, refused;
    const std::vector<BRITE::Color> picture = Draw(sharpPass, &sharp);
    m_backend.RefuseReflectionBlurForTest();
    BRITE::RenderPass pass = Lake(true);
    pass.Reflections.Blur = On(0.01f, 4.0f);
    EXPECT_TRUE(Same(Draw(pass, &refused), picture));
    EXPECT_EQ(refused, sharp);
    EXPECT_FALSE(m_backend.LastPlanarReflectionBlurred());
    EXPECT_EQ(m_backend.ReflectionBlurTargetsHeld(), 0);
}

// A DEVICE THAT CANNOT MAKE ITS TARGET draws the sharp reflection, frame after frame, and is not asked again at that
// size. Mutations: the refusal ignored -> the passes drawn into no scratch, red; the refused size not remembered -> 2
// made.
TEST_F(ReflectionBlurPass, ADeviceThatCannotMakeItsTargetDrawsTheSharpReflection) {
    BRITE::RenderPass sharpPass = Lake(true);
    std::vector<float> sharp, refused;
    const std::vector<BRITE::Color> picture = Draw(sharpPass, &sharp);
    m_backend.RefuseReflectionBlurTargetsForTest();
    for (int frame = 0; frame < 2; ++frame) {
        BRITE::RenderPass pass = Lake(true);
        pass.Reflections.Blur = On(0.01f, 4.0f);
        EXPECT_TRUE(Same(Draw(pass, &refused), picture)) << frame;
        EXPECT_EQ(refused, sharp) << frame;
        EXPECT_FALSE(m_backend.LastPlanarReflectionBlurred()) << frame;
    }
    EXPECT_EQ(m_backend.ReflectionBlurTargetsMade(), 1);
    EXPECT_EQ(m_backend.ReflectionBlurTargetsHeld(), 0);
}

// ITS TARGET IS KEPT WITH THE REFLECTION'S: three blurred passes at one size make one scratch target and hold it; a
// reflection at a second size holds a second; and once the first size stands idle for longer than the chain's sets
// may (each planar pass counts two), its scratch is freed with its reflection.
// Mutation: the scratch not kept, made again on every blur -> 3 made, red.
TEST_F(ReflectionBlurPass, ItsTargetIsKeptWithTheReflectionsAndFreedWithIt) {
    for (int frame = 0; frame < 3; ++frame) {
        BRITE::RenderPass pass = Lake(true);
        pass.Reflections.Blur = On(0.004f, 4.0f);
        Draw(pass);
    }
    EXPECT_EQ(m_backend.ReflectionBlurTargetsMade(), 1);
    EXPECT_EQ(m_backend.ReflectionBlurTargetsHeld(), 1);
    const std::uint64_t rounds = BRITE::Backends::Raylib::RaylibRenderBackend::HdrIdlePasses() + 10;
    for (std::uint64_t round = 0; round < rounds; ++round) {
        BRITE::RenderPass pass = Lake(true, 0.1f, 0.5f);
        pass.Reflections.Blur = On(0.004f, 4.0f);
        Draw(pass, nullptr, 64);
    }
    EXPECT_EQ(m_backend.ReflectionBlurTargetsMade(), 2);
    EXPECT_EQ(m_backend.ReflectionBlurTargetsHeld(), 1) << "the first size's, idle, is freed with its reflection";
}

// THE REFLECTION IS READ AT ITS OWN SIZE AGAIN AFTER A PASS THAT READ A COARSER LEVEL OF IT. The lit shader reads the
// reflection with texture(), its level from the read's own derivatives, so a reflection left filtering through mipmaps
// would show a distorted or grazing read the stale levels the blur made. A 1700 x 1700 target at Resolution 1: a
// reflection 1700 high, and a Softness of 0.01 spreads 17 texels across -- beyond 16, so the ACROSS pass, which reads
// the reflection itself, reads its level 1 (17 <= 16 x 2). After the pass the reflection's minifying filter is
// GL_LINEAR (0x2601), the filter it was made with, as before any blur. Mutation: the filter not put back after a pass
// that read a coarser level -> GL_LINEAR_MIPMAP_NEAREST (0x2701), red.
TEST_F(ReflectionBlurPass, TheReflectionIsReadAtItsOwnSizeAgain) {
    constexpr int TALL = 1700;
    constexpr int GL_LINEAR_FILTER = 0x2601;
    BRITE::RenderPass sharpPass = Lake(true);
    Draw(sharpPass, nullptr, TALL);
    EXPECT_EQ(m_backend.LastPlanarReflectionMinFilter(), GL_LINEAR_FILTER) << "as made";
    BRITE::RenderPass pass = Lake(true);
    pass.Reflections.Blur = On(0.01f, 4.0f);
    ASSERT_EQ(RB::PlanFor(pass.Reflections.Blur, TALL, TALL).AcrossPass.Level, 1);
    Draw(pass, nullptr, TALL);
    ASSERT_TRUE(m_backend.LastPlanarReflectionBlurred());
    EXPECT_EQ(m_backend.LastPlanarReflectionMinFilter(), GL_LINEAR_FILTER);
}
