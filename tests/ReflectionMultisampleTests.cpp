// The planar reflection's multisampling (RenderPass.hpp, ReflectionSettings::Samples), drawn on the GPU under a hidden
// window (HiddenWindowScene.hpp).
//
// THE SCENE is the reflection blur's lake: a perspective camera at (0, 2, 6) looking at the origin, 45 degrees, over
// black water reflecting in the plane y = 0, the sky blue -- and above it a red unlit box, half a unit each way at
// (0, 1, -3), turned 30 degrees about the view's axis so every edge of it crosses the reflection's texels on a slant.
// Drawn into a 256 x 256 target at Resolution 1, the reflection is 256 x 256 texels. Only the box is red, so the red
// channel of the reflection (ReadLastPlanarReflection) is the box's coverage of each texel times its one colour.
//
// WHAT MULTISAMPLING DOES, by hand: a texel drawn with one sample is either inside the box or not, so its red is
// exactly 0 or exactly the box's red -- never between. A texel drawn with n samples and resolved holds the box's red
// times the share of its samples the box covers, k / n for k = 0..n, so a texel whose samples an edge splits reads a
// share strictly between. The box is a unit across, about 9.1 units from the mirrored camera (0, -2, 6) -- the view
// there 2 x 9.1 x tan(22.5 deg) = 7.5 units over 256 texels, 34 texels a unit -- so its outline is about 4 x 34 = 136
// texels long, and a 30-degree edge crosses cos 30 + sin 30 = 1.37 texels per texel of its length: about 185 texels
// crossed. With 4 samples a crossed texel reads part way only when the edge splits its samples, which it does not when
// it only clips a corner of the texel beyond every sample. MEASURED on this device: 91 texels part way, at 4 samples,
// under HDR and without. The case asks for more than 50 -- what it guards is none against many. And the box's total red
// is the same either way to within the edges' rounding: a resolve averages the samples, it does not add or take away
// coverage -- each crossed texel's error is at most half its red, 185 x 0.5 = 93 texels' worth against the box's some
// 34 x 34 = 1,156, an 8% bound that is reached only if every error had one sign; they fall both ways, and 3% holds.
//
// Beside each case is the mutation that turns it red.

#include "HiddenWindowScene.hpp"

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool Same(const std::vector<BRITE::Color>& a, const std::vector<BRITE::Color>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b || a[i].a != b[i].a)
            return false;
    return true;
}

// The red channel of a reflection: its total, its largest texel, and how many texels lie strictly between 2% and 98%
// of that largest -- the texels an edge crosses part way.
struct Red {
    double total = 0.0;
    float most = 0.0f;
    int between = 0;
};
Red RedOf(const std::vector<float>& rgba) {
    Red red;
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        red.most = std::max(red.most, rgba[i]);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        red.total += rgba[i];
        if (rgba[i] > 0.02f * red.most && rgba[i] < 0.98f * red.most)
            ++red.between;
    }
    return red;
}

// Every line logged through the default logger while it lives, counted by what it says.
class LogCapture {
  public:
    LogCapture() : m_sink(std::make_shared<spdlog::sinks::ostream_sink_mt>(m_lines)) {
        spdlog::default_logger()->sinks().push_back(m_sink);
    }
    ~LogCapture() {
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    int Count(const std::string& text) {
        m_sink->flush();
        std::istringstream lines(m_lines.str());
        int count = 0;
        for (std::string line; std::getline(lines, line);)
            if (line.find(text) != std::string::npos)
                ++count;
        return count;
    }

  private:
    std::ostringstream m_lines; // before the sink, which writes into it
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
};

// The device's most samples capped for as long as it lives (RaylibRenderBackend::CapMostSamplesForTest), and lifted.
struct MostSamplesCap {
    explicit MostSamplesCap(int most) {
        BRITE::Backends::Raylib::RaylibRenderBackend::CapMostSamplesForTest(most);
    }
    ~MostSamplesCap() {
        BRITE::Backends::Raylib::RaylibRenderBackend::CapMostSamplesForTest(0);
    }
};

// What the backend logs when it cannot multisample a reflection.
constexpr const char* CannotMultisample = "planar reflection cannot be multisampled";

class ReflectionMultisample : public HiddenWindow::Scene {
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
    // The lake and its tilted box, under HDR or not, the reflection drawn with `samples` samples a texel.
    BRITE::RenderPass Lake(bool hdr, int samples) {
        if (m_water == BRITE::NullModelHandle) {
            m_water = Water();
            m_box = Box(0.0f, 0.0f, 0.0f, 0.5f);
        }
        BRITE::RenderPass pass;
        pass.Post.Hdr = hdr;
        pass.Post.Samples = 1;
        pass.ClearColor = {0, 0, 255, 255};
        pass.Reflections.Planar = true;
        pass.Reflections.Resolution = 1.0f;
        pass.Reflections.Samples = samples;
        BRITE::ModelDrawCommand water = Unlit(m_water, {0, 0, 0, 255});
        water.Material.Unlit = false;
        water.Material.DoubleSided = false;
        water.Material.Reflection = BRITE::ReflectionMode::Planar;
        water.Material.ReflectionF0 = 0.02f;
        water.Material.ReflectionDistortion = 0.0f;
        BRITE::ModelDrawCommand box = Unlit(m_box, {255, 0, 0, 255});
        box.Position = {0.0f, 1.0f, -3.0f};
        // 30 degrees about z: (0, 0, sin 15 deg, cos 15 deg).
        box.Rotation = {0.0f, 0.0f, 0.25881904f, 0.96592583f};
        pass.ModelCommands = {water, box};
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
            int rw = 0, rh = 0;
            EXPECT_TRUE(m_backend.ReadLastPlanarReflection(rw, rh, *reflection));
        }
        m_backend.UnloadRenderTexture(target);
        pass.Camera3DPtr = nullptr;
        return pixels;
    }
    // The most samples this device gives a render target: what a multisampled render texture asked for 64 is made with.
    int DeviceMost() {
        const BRITE::TextureHandle probe = m_backend.LoadMultisampledRenderTexture(8, 8, 64);
        const int most = m_backend.RenderTextureSamples(probe);
        m_backend.UnloadRenderTexture(probe);
        return most;
    }
    BRITE::ModelHandle m_water = BRITE::NullModelHandle;
    BRITE::ModelHandle m_box = BRITE::NullModelHandle;
};

} // namespace

// OFF, AS IT HAS ALWAYS BEEN AND AT NO COST: the default, 1, 0 and a negative count each draw exactly the picture and
// the reflection a pass that never named Samples draws, with one sample, and no multisampled target is made or tried.
// Mutations: a default of 4 -> the default's reflection drawn with 4, red; "1 or less is none" read as "0 or less" ->
// 1 asks for a multisampled target, tried 1, red.
TEST_F(ReflectionMultisample, OffCostsNothingAndDrawsAsBefore) {
    EXPECT_EQ(BRITE::ReflectionSettings{}.Samples, 1);
    for (const bool hdr : {true, false}) {
        BRITE::RenderPass untouched = Lake(hdr, BRITE::ReflectionSettings{}.Samples);
        std::vector<float> before, after;
        const std::vector<BRITE::Color> picture = Draw(untouched, &before);
        EXPECT_EQ(m_backend.LastPlanarReflectionSamples(), 1) << hdr;
        for (const int none : {1, 0, -3}) {
            BRITE::RenderPass off = Lake(hdr, none);
            EXPECT_TRUE(Same(Draw(off, &after), picture)) << hdr << " " << none;
            EXPECT_EQ(after, before) << hdr << " " << none;
            EXPECT_EQ(m_backend.LastPlanarReflectionSamples(), 1) << hdr << " " << none;
        }
        // A reflection drawn with one sample is the box or not, texel by texel: nothing between.
        EXPECT_EQ(RedOf(before).between, 0) << hdr;
    }
    EXPECT_EQ(m_backend.PlanarReflectionMultisampleTargetsTried(), 0);
}

// ON, ITS EDGES ARE SMOOTHED, UNDER HDR AND WITHOUT: 4 samples (or the device's most, if fewer) leave over 50 texels
// part way between the sky and the box (worked at the top; 91 measured), where one sample leaves none -- and the box's
// total red is the one-sample total to within 3% (worked at the top). Mutations: Samples
// ignored -> 1 sample, 0 between, red; the reflection drawn into its single-sample framebuffer rather than its
// multisampled one -> the resolve lays an empty multisampled picture over it, no red, red; no resolve -> the reflection
// never drawn into what is read, no red, red.
TEST_F(ReflectionMultisample, ItsEdgesAreSmoothedUnderHdrAndWithout) {
    const int wanted = std::min(4, DeviceMost());
    ASSERT_GE(wanted, 2) << "this device cannot multisample a render target";
    for (const bool hdr : {true, false}) {
        BRITE::RenderPass sharpPass = Lake(hdr, 1);
        std::vector<float> sharp, smooth;
        Draw(sharpPass, &sharp);
        BRITE::RenderPass smoothPass = Lake(hdr, 4);
        Draw(smoothPass, &smooth);
        EXPECT_EQ(m_backend.LastPlanarReflectionSamples(), wanted) << hdr;
        const Red one = RedOf(sharp), many = RedOf(smooth);
        ASSERT_GT(one.total, 0.0) << hdr;
        EXPECT_EQ(one.between, 0) << hdr;
        EXPECT_GT(many.between, 50) << hdr;
        EXPECT_NEAR(many.total / one.total, 1.0, 0.03) << hdr;
        EXPECT_FLOAT_EQ(many.most, one.most) << "the box's own colour, inside it, is unchanged; " << hdr;
    }
}

// RESOLVED BEFORE THE BLUR: a multisampled reflection blurred is the smoothed reflection spread -- its red is the
// box's, its total the same to within the edges' rounding and the blur's own cut (3 standard deviations either side
// keeps 99.7% of a Gaussian's weight, so within 3% as above), and it is not the sharp one. Mutations: no resolve -> the
// blur reads a reflection never drawn into, no red, red; the resolve after the blur -> the blurred picture overwritten
// by the unblurred one, the same as the unblurred reflection, red.
TEST_F(ReflectionMultisample, ItIsResolvedBeforeTheBlurReadsIt) {
    ASSERT_GE(DeviceMost(), 2) << "this device cannot multisample a render target";
    for (const bool hdr : {true, false}) {
        BRITE::RenderPass sharpPass = Lake(hdr, 4);
        std::vector<float> sharp, blurred;
        Draw(sharpPass, &sharp);
        BRITE::RenderPass pass = Lake(hdr, 4);
        pass.Reflections.Blur.Enabled = true;
        pass.Reflections.Blur.Softness = 0.01f;
        pass.Reflections.Blur.Stretch = 2.0f;
        Draw(pass, &blurred);
        ASSERT_TRUE(m_backend.LastPlanarReflectionBlurred()) << hdr;
        const Red s = RedOf(sharp), b = RedOf(blurred);
        ASSERT_GT(s.total, 0.0) << hdr;
        EXPECT_NEAR(b.total / s.total, 1.0, 0.03) << hdr;
        EXPECT_NE(blurred, sharp) << hdr;
        EXPECT_GT(b.between, 2 * s.between) << hdr;
    }
}

// HELD TO WHAT THE DEVICE GIVES: 64 samples asked are drawn with the device's most, the count a multisampled render
// texture asked for 64 is made with. Mutation: the count not held to the device -> 64 asked, the device's most made,
// the two disagree, the target refused and the reflection drawn with 1, red.
TEST_F(ReflectionMultisample, ItIsHeldToWhatTheDeviceGives) {
    const int most = DeviceMost();
    ASSERT_GE(most, 2) << "this device cannot multisample a render target";
    BRITE::RenderPass pass = Lake(true, 64);
    Draw(pass);
    EXPECT_EQ(m_backend.LastPlanarReflectionSamples(), std::min(64, most));
}

// A DEVICE THAT CANNOT MAKE A MULTISAMPLED TARGET draws the single-sample reflection, exactly as with Samples off,
// frame after frame, and is not asked again at that size; a second size is asked once more, and the refusals are
// logged once between them. Mutations: the refusal not answered with the single-sample target -> no reflection, red;
// the refused size not remembered -> 3 tried, red; the log not kept to once -> 2 lines, red.
TEST_F(ReflectionMultisample, ADeviceThatCannotMakeItDrawsTheSingleSampleReflection) {
    BRITE::RenderPass offPass = Lake(true, 1);
    std::vector<float> off, refused;
    const std::vector<BRITE::Color> picture = Draw(offPass, &off);
    LogCapture log;
    m_backend.RefuseReflectionMultisampleForTest();
    for (int frame = 0; frame < 2; ++frame) {
        BRITE::RenderPass pass = Lake(true, 4);
        EXPECT_TRUE(Same(Draw(pass, &refused), picture)) << frame;
        EXPECT_EQ(refused, off) << frame;
        EXPECT_EQ(m_backend.LastPlanarReflectionSamples(), 1) << frame;
    }
    EXPECT_EQ(m_backend.PlanarReflectionMultisampleTargetsTried(), 1);
    BRITE::RenderPass smaller = Lake(true, 4);
    Draw(smaller, nullptr, BIG / 2);
    EXPECT_EQ(m_backend.LastPlanarReflectionSamples(), 1);
    EXPECT_EQ(m_backend.PlanarReflectionMultisampleTargetsTried(), 2) << "a second size is asked once";
    EXPECT_EQ(log.Count(CannotMultisample), 1) << "two sizes refused, one line";
}

// A DEVICE WHOSE MOST IS ONE SAMPLE -- one that cannot multisample at all -- is asked for a multisampled reflection
// once, logs it once, and draws the single-sample reflection, exactly as with Samples off, frame after frame. Its
// target comes back with the one sample the device has, not the 2 asked for (the least a multisampled reflection asks
// for), and is refused for that. Mutations: the target's samples not checked against those asked -> the one-sample
// target taken as the multisampled one, never matched again, made anew each frame: 2 tried and 3 targets held, red;
// the floor of 2 dropped -> 4 asked of a device whose most is 1 asks for 1: no try and no line, red.
TEST_F(ReflectionMultisample, ADeviceWhoseMostIsOneSampleIsAskedOnceAndDrawsTheSingleSampleReflection) {
    BRITE::RenderPass offPass = Lake(true, 1);
    std::vector<float> off, capped;
    const std::vector<BRITE::Color> picture = Draw(offPass, &off);
    const MostSamplesCap cap(1);
    LogCapture log;
    for (int frame = 0; frame < 2; ++frame) {
        BRITE::RenderPass pass = Lake(true, 4);
        EXPECT_TRUE(Same(Draw(pass, &capped), picture)) << frame;
        EXPECT_EQ(capped, off) << frame;
        EXPECT_EQ(m_backend.LastPlanarReflectionSamples(), 1) << frame;
    }
    EXPECT_EQ(m_backend.PlanarReflectionMultisampleTargetsTried(), 1);
    EXPECT_EQ(m_backend.PlanarReflectionTargetsHeld(), 1) << "the single-sample reflection's, shared with Samples off";
    EXPECT_EQ(log.Count(CannotMultisample), 1);
}

// ITS TARGETS ARE KEPT BY THEIR SAMPLES: a pass with 1, then one with 4, then one with 1 again, at one size, draw with
// 1, 4 and 1, and hold two targets -- one each -- with one multisampled target made. Mutation: a target kept by size
// and format alone -> the second pass reuses the single-sample one, drawn with 1, red.
TEST_F(ReflectionMultisample, ItsTargetsAreKeptByTheirSamples) {
    ASSERT_GE(DeviceMost(), 2) << "this device cannot multisample a render target";
    const int wanted = std::min(4, DeviceMost());
    const int counts[3] = {1, 4, 1};
    const int drawn[3] = {1, wanted, 1};
    for (int i = 0; i < 3; ++i) {
        BRITE::RenderPass pass = Lake(true, counts[i]);
        Draw(pass);
        EXPECT_EQ(m_backend.LastPlanarReflectionSamples(), drawn[i]) << i;
    }
    EXPECT_EQ(m_backend.PlanarReflectionTargetsHeld(), 2);
    EXPECT_EQ(m_backend.PlanarReflectionMultisampleTargetsTried(), 1);
}
