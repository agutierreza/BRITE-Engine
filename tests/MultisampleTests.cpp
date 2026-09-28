// Multisample anti-aliasing as an application option.
//
// Two places need the samples. Without an internal resolution the scenes draw
// straight into the window's back buffer, so the window is asked for a
// multisampled one -- and that can only be asked while the window is being
// created. With an internal resolution the scenes draw into the application's
// own framebuffer and the window receives one finished picture, a single
// axis-aligned blit with no edges of its own, so a multisampled window smooths
// nothing the scenes drew. That framebuffer is the one that must be
// multisampled, and resolved before anything reads it.
//
// The first cases prove the option reaches both places, through recording
// backends and no GPU. The rest prove, on the GPU under a hidden window, that
// what it reaches does the work: a multisampled render texture smooths an
// edge, a count beyond the device is lowered to its most, unloading frees what
// it made, and the raylib window is multisampled when asked.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/IApplicationBackend.hpp>
#include <Backends/IRenderBackend.hpp>
#include <Backends/MeshData.hpp>
#include <Backends/Raylib/RaylibApplicationBackend.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <Backends/RenderPass.hpp>
#include <Framework/Application.hpp>
#include <gtest/gtest.h>

#include <raylib.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

// Three OpenGL queries, through the loader raylib links in, as the backend
// itself reaches the calls rlgl does not wrap.
extern "C" {
#if defined(_WIN32) && !defined(_WIN64)
#define TEST_GL_CALL __stdcall
#else
#define TEST_GL_CALL
#endif
extern void(TEST_GL_CALL* glad_glGetIntegerv)(unsigned int name, int* values);
extern unsigned char(TEST_GL_CALL* glad_glIsRenderbuffer)(unsigned int renderbuffer);
}

using BRITE::Backends::Raylib::RaylibRenderBackend;
using brite::framework::Application;
using brite::framework::ApplicationOptions;

namespace {

constexpr unsigned int GL_SAMPLES_ = 0x80A9;
constexpr unsigned int GL_MAX_SAMPLES_ = 0x8D57;

// ---------------------------------------------------------------------------
// Recording backends
// ---------------------------------------------------------------------------

// A window that creates nothing and remembers how it was asked to.
class RecordingWindow : public BRITE::Backends::IApplicationBackend {
  public:
    struct Log {
        int plainInits = 0;
        int optionInits = 0;
        int multisampleCount = -1; // what the options-taking Init was given
    };
    explicit RecordingWindow(Log& log) : m_log(log) {}

    void Init(const std::string&, int, int) override {
        ++m_log.plainInits;
    }
    void Init(const std::string&, int, int, const BRITE::Backends::WindowOptions& options) override {
        ++m_log.optionInits;
        m_log.multisampleCount = options.MultisampleCount;
    }
    void Shutdown() override {}
    bool WindowShouldClose() override {
        return true;
    }
    void SetTargetFPS(int) override {}
    double GetTime() override {
        return 0.0;
    }
    int GetScreenWidth() override {
        return 1;
    }
    int GetScreenHeight() override {
        return 1;
    }

  private:
    Log& m_log;
};

// A render backend that hands out numbered render textures and remembers which
// were asked for with samples, and how many.
class RecordingRenderer : public BRITE::Backends::IRenderBackend {
  public:
    struct Load {
        int width, height, samples; // samples 0: through LoadRenderTexture
    };
    struct Log {
        std::vector<Load> loads;
    };
    explicit RecordingRenderer(Log& log) : m_log(log) {}

    BRITE::TextureHandle LoadRenderTexture(int width, int height) override {
        m_log.loads.push_back({width, height, 0});
        return static_cast<BRITE::TextureHandle>(m_log.loads.size());
    }
    BRITE::TextureHandle LoadMultisampledRenderTexture(int width, int height, int samples) override {
        m_log.loads.push_back({width, height, samples});
        return static_cast<BRITE::TextureHandle>(m_log.loads.size());
    }
    void SubmitRenderPass(const BRITE::RenderPass&) override {}
    void UnloadRenderTexture(BRITE::TextureHandle) override {}
    bool ReadRenderTexture(BRITE::TextureHandle, int&, int&, std::vector<BRITE::Color>&) override {
        return false;
    }
    BRITE::TextureHandle LoadTexture(const char*) override {
        return BRITE::NullTextureHandle;
    }
    void UnloadTexture(BRITE::TextureHandle) override {}
    BRITE::ModelHandle LoadModel(const char*) override {
        return BRITE::NullModelHandle;
    }
    BRITE::ModelHandle LoadModelFromMesh(const BRITE::MeshData&) override {
        return BRITE::NullModelHandle;
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
        return BRITE::EnvironmentMap{};
    }
    void UnloadEnvironmentMap(BRITE::EnvironmentMap) override {}
    int GetShaderLocation(BRITE::ShaderHandle, const char*) override {
        return -1;
    }
    void SetShaderValue(BRITE::ShaderHandle, int, const void*, BRITE::Backends::ShaderUniformDataType) override {}

  private:
    Log& m_log;
};

std::unique_ptr<Application> MakeApp(RecordingWindow::Log& window, RecordingRenderer::Log& renderer,
                                     const ApplicationOptions& options = {}) {
    return std::make_unique<Application>(std::make_unique<RecordingWindow>(window), nullptr,
                                         std::make_unique<RecordingRenderer>(renderer), "Multisample", "BRITE",
                                         "Engine", 1, 1, options);
}

} // namespace

// Asked for 4 samples, the application creates its window through the Init that
// takes options, once, with 4 -- during construction, which is when the window
// is made and the only time the samples can be chosen.
//
// Mutations: the application calling the Init without options -> optionInits 0,
// plainInits 1; the count not passed on -> 1, not 4.
TEST(MultisampleOption, ReachesTheWindowAsItIsCreated) {
    RecordingWindow::Log window;
    RecordingRenderer::Log renderer;
    ApplicationOptions options;
    options.MultisampleCount = 4;
    auto app = MakeApp(window, renderer, options);

    EXPECT_EQ(window.optionInits, 1);
    EXPECT_EQ(window.plainInits, 0);
    EXPECT_EQ(window.multisampleCount, 4);
    EXPECT_EQ(app->GetOptions().MultisampleCount, 4);
}

// Asked for nothing, the window is created with 1 sample: none. Nothing changes
// for an application that does not ask.
//
// Mutation: ApplicationOptions defaulting to 4 samples -> 4.
TEST(MultisampleOption, IsOffUnlessAsked) {
    RecordingWindow::Log window;
    RecordingRenderer::Log renderer;
    auto app = MakeApp(window, renderer);

    EXPECT_EQ(window.optionInits, 1);
    EXPECT_EQ(window.multisampleCount, 1);
}

// At an internal resolution of 320 x 180 with 4 samples, the framebuffer the
// scenes draw into is a 4-sample render texture of 320 x 180, and the
// alternate the post-process passes ping-pong into is an ordinary one.
//
// Mutations: the scene framebuffer loaded through LoadRenderTexture -> samples
// 0; the count not passed -> 1; the alternate multisampled too -> its samples 4.
TEST(MultisampleOption, MakesTheInternalFramebufferMultisampled) {
    RecordingWindow::Log window;
    RecordingRenderer::Log renderer;
    ApplicationOptions options;
    options.MultisampleCount = 4;
    auto app = MakeApp(window, renderer, options);
    app->SetInternalResolution(320, 180);

    ASSERT_EQ(renderer.loads.size(), 2u);
    EXPECT_EQ(renderer.loads[0].width, 320);
    EXPECT_EQ(renderer.loads[0].height, 180);
    EXPECT_EQ(renderer.loads[0].samples, 4) << "the framebuffer the scenes draw into";
    EXPECT_EQ(renderer.loads[1].samples, 0) << "the post-process alternate, ordinary";
}

// Asked for nothing, the internal framebuffer is asked for 1 sample, which the
// interface defines as an ordinary render texture.
//
// Mutation: ApplicationOptions defaulting to 4 samples -> 4.
TEST(MultisampleOption, LeavesTheInternalFramebufferOrdinaryUnlessAsked) {
    RecordingWindow::Log window;
    RecordingRenderer::Log renderer;
    auto app = MakeApp(window, renderer);
    app->SetInternalResolution(320, 180);

    ASSERT_EQ(renderer.loads.size(), 2u);
    EXPECT_EQ(renderer.loads[0].samples, 1);
}

namespace {
// A window backend and a render backend written before either option existed:
// each overrides its pure methods and nothing else.
class OlderWindow : public BRITE::Backends::IApplicationBackend {
  public:
    int inits = 0;
    void Init(const std::string&, int, int) override {
        ++inits;
    }
    void Shutdown() override {}
    bool WindowShouldClose() override {
        return true;
    }
    void SetTargetFPS(int) override {}
    double GetTime() override {
        return 0.0;
    }
    int GetScreenWidth() override {
        return 1;
    }
    int GetScreenHeight() override {
        return 1;
    }
};
class OlderRenderer : public RecordingRenderer {
  public:
    using RecordingRenderer::RecordingRenderer;
    // Undo the recording override, back to the interface's default.
    BRITE::TextureHandle LoadMultisampledRenderTexture(int width, int height, int samples) override {
        return IRenderBackend::LoadMultisampledRenderTexture(width, height, samples);
    }
};
} // namespace

// A window backend that knows nothing of options still creates its window when
// asked with them; a render backend that cannot multisample still gives a
// render texture of the size asked -- its ordinary one.
//
// Mutations: the default Init with options doing nothing -> inits 0; the default
// LoadMultisampledRenderTexture returning the null handle -> handle 0, and no
// ordinary load recorded.
TEST(MultisampleOption, OlderBackendsStillMakeAWindowAndATarget) {
    OlderWindow window;
    BRITE::Backends::IApplicationBackend& asInterface = window;
    BRITE::Backends::WindowOptions options;
    options.MultisampleCount = 4;
    asInterface.Init("older", 1, 1, options);
    EXPECT_EQ(window.inits, 1);

    RecordingRenderer::Log log;
    OlderRenderer renderer(log);
    const BRITE::TextureHandle target = renderer.LoadMultisampledRenderTexture(64, 32, 4);
    EXPECT_NE(target, BRITE::NullTextureHandle);
    ASSERT_EQ(log.loads.size(), 1u);
    EXPECT_EQ(log.loads[0].width, 64);
    EXPECT_EQ(log.loads[0].height, 32);
    EXPECT_EQ(log.loads[0].samples, 0) << "through LoadRenderTexture";
}

// ---------------------------------------------------------------------------
// On the GPU
// ---------------------------------------------------------------------------

namespace {

constexpr int SIZE = 64;

class MultisampleGpuTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(SIZE, SIZE, "BRITE multisample tests");
    }
    static void TearDownTestSuite() {
        ::CloseWindow();
    }

    // An unlit white triangle on black: (-1.5, -1.5), (1.3, -1.5), (-1.5, 1.4),
    // under the orthographic view of ModelMaterialTests.cpp -- world -2..2 onto
    // 64 pixels, 16 a metre, +y up. Its long side runs corner to corner across
    // the picture at a slope that is not a whole number of pixels, so it crosses
    // pixels part-way all along its length.
    std::vector<BRITE::Color> DrawTriangle(BRITE::TextureHandle target) {
        BRITE::MeshData mesh;
        mesh.Positions = {{-1.5f, -1.5f, 0.0f}, {1.3f, -1.5f, 0.0f}, {-1.5f, 1.4f, 0.0f}};
        mesh.Normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
        mesh.Indices = {0, 1, 2};
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
        EXPECT_NE(model, BRITE::NullModelHandle);

        BRITE::Camera3D camera{
            {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4.0f, BRITE::CameraProjection::Orthographic};
        BRITE::RenderPass pass;
        pass.TargetFramebuffer = target;
        pass.ClearColor = {0, 0, 0, 255};
        pass.Camera3DPtr = &camera;
        BRITE::ModelDrawCommand draw;
        draw.Model = model;
        draw.Material.Unlit = true;
        draw.Position = {0.0f, 0.0f, 0.0f};
        draw.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        draw.Scale = {1.0f, 1.0f, 1.0f};
        pass.ModelCommands.push_back(draw);
        m_backend.SubmitRenderPass(pass);

        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        m_backend.UnloadModel(model);
        return pixels;
    }

    RaylibRenderBackend m_backend;
};

// The pixel under world (-1, -1), well inside the triangle: column (-1 + 2) * 16
// = 16, row (2 - (-1)) * 16 = 48.
constexpr std::size_t INSIDE = 48 * SIZE + 16;

bool IsQuarterStep(int value) {
    // k samples of 4 covered, k = 0..4: 255 * k / 4 = 0, 63.75, 127.5, 191.25, 255.
    for (int k = 0; k <= 4; ++k)
        if (std::abs(value - 255.0 * k / 4.0) <= 2.0)
            return true;
    return false;
}

} // namespace

// An edge drawn into an ordinary render texture is a staircase: every pixel's
// centre is in the triangle or out of it, so every pixel is 0 or 255. Drawn
// into a 4-sample one, a pixel the edge crosses keeps the colour of each of its
// four samples, 1.0 or 0.0, and the resolve averages them: k/4 of 255 for k
// samples covered -- 63.75, 127.5 or 191.25 along the edge, and 0 or 255 off
// it. The edge crosses dozens of pixels part-way; at least ten are asked for.
//
// Mutations: the resolve skipped -> the read-back texture was never drawn, and
// the inside pixel is not 255; the pass drawn into the ordinary texture instead
// of the multisampled one -> the resolve then blits the empty samples over it,
// black; the storage made single-sample -> no pixel between 0 and 255. (Asked
// for with 0 samples, not 1: OpenGL promises only at least the samples asked,
// and a request for 1 came back multisampled on the machine this was run on.)
TEST_F(MultisampleGpuTest, AMultisampledRenderTextureSmoothsAnEdge) {
    const BRITE::TextureHandle plain = m_backend.LoadRenderTexture(SIZE, SIZE);
    const BRITE::TextureHandle smooth = m_backend.LoadMultisampledRenderTexture(SIZE, SIZE, 4);
    ASSERT_EQ(m_backend.RenderTextureSamples(smooth), 4);

    const auto stairs = DrawTriangle(plain);
    ASSERT_EQ(stairs.size(), static_cast<std::size_t>(SIZE * SIZE));
    EXPECT_EQ(stairs[INSIDE].r, 255);
    int between = 0;
    for (const BRITE::Color& c : stairs)
        between += (c.r != 0 && c.r != 255) ? 1 : 0;
    EXPECT_EQ(between, 0) << "an ordinary target: whole pixels only";

    const auto smoothed = DrawTriangle(smooth);
    ASSERT_EQ(smoothed.size(), static_cast<std::size_t>(SIZE * SIZE));
    EXPECT_EQ(smoothed[INSIDE].r, 255) << "inside the triangle, every sample covered";
    between = 0;
    int offStep = 0;
    for (const BRITE::Color& c : smoothed) {
        between += (c.r > 2 && c.r < 253) ? 1 : 0;
        offStep += IsQuarterStep(c.r) ? 0 : 1;
    }
    EXPECT_GE(between, 10) << "pixels the edge crosses part-way";
    EXPECT_EQ(offStep, 0) << "every pixel a whole number of quarter samples";

    m_backend.UnloadRenderTexture(plain);
    m_backend.UnloadRenderTexture(smooth);
}

// A count the device cannot give is lowered to the most it can, which OpenGL
// 3.3 guarantees is at least 4; 1 is an ordinary render texture, as is one made
// through LoadRenderTexture; a handle that names no render texture has 0.
//
// Mutation: the count not lowered -> OpenGL refuses the storage, the target is
// incomplete, and an ordinary one (1) comes back instead of the device's most.
TEST_F(MultisampleGpuTest, ACountBeyondTheDeviceIsLoweredToItsMost) {
    int most = 0;
    glad_glGetIntegerv(GL_MAX_SAMPLES_, &most);
    ASSERT_GE(most, 4);

    const BRITE::TextureHandle greedy = m_backend.LoadMultisampledRenderTexture(SIZE, SIZE, 1024);
    EXPECT_EQ(m_backend.RenderTextureSamples(greedy), most);
    const BRITE::TextureHandle one = m_backend.LoadMultisampledRenderTexture(SIZE, SIZE, 1);
    EXPECT_EQ(m_backend.RenderTextureSamples(one), 1);
    const BRITE::TextureHandle ordinary = m_backend.LoadRenderTexture(SIZE, SIZE);
    EXPECT_EQ(m_backend.RenderTextureSamples(ordinary), 1);
    EXPECT_EQ(m_backend.RenderTextureSamples(BRITE::NullTextureHandle), 0);

    m_backend.UnloadRenderTexture(greedy);
    m_backend.UnloadRenderTexture(one);
    m_backend.UnloadRenderTexture(ordinary);
}

// A multisampled render texture adds exactly two renderbuffers -- its colour
// samples and its depth samples -- and unloading it takes both away. Counted as
// live renderbuffer names before, after loading, and after unloading. The
// ordinary texture it resolves into brings whatever raylib gives an ordinary
// render texture, counted first on its own.
//
// Mutation: the colour renderbuffer not deleted on unload -> one still alive.
TEST_F(MultisampleGpuTest, UnloadingAMultisampledRenderTextureFreesWhatItMade) {
    auto liveRenderbuffers = [] {
        int count = 0;
        for (unsigned int id = 1; id < 1024; ++id)
            count += glad_glIsRenderbuffer(id) ? 1 : 0;
        return count;
    };
    const int before = liveRenderbuffers();
    const BRITE::TextureHandle ordinary = m_backend.LoadRenderTexture(SIZE, SIZE);
    const int perOrdinary = liveRenderbuffers() - before;
    m_backend.UnloadRenderTexture(ordinary);
    ASSERT_EQ(liveRenderbuffers(), before);

    const BRITE::TextureHandle smooth = m_backend.LoadMultisampledRenderTexture(SIZE, SIZE, 4);
    EXPECT_EQ(liveRenderbuffers(), before + perOrdinary + 2) << "colour and depth samples";
    m_backend.UnloadRenderTexture(smooth);
    EXPECT_EQ(liveRenderbuffers(), before) << "and nothing left of them";
}

// Asked for no samples, the raylib window has none: OpenGL reports 0.
//
// raylib keeps the multisample hint for the rest of the process once any window
// has asked for it -- CloseWindow leaves it set, and nothing raylib offers
// clears it -- so after a multisampled window every later one is multisampled
// too, whatever it asks. So this case comes BEFORE the one that asks, for a run
// of the whole suite in one process to reach it; and should anything earlier in
// the process have asked, it says so and skips, rather than blaming the
// backend for raylib's memory.
//
// Mutation: Init setting the multisample hint whatever it is asked -> 4.
TEST(MultisampleWindow, TheRaylibWindowIsPlainUnlessAsked) {
    if (::IsWindowState(FLAG_MSAA_4X_HINT))
        GTEST_SKIP() << "an earlier window in this process asked for multisampling, and raylib keeps the hint";
    ::SetTraceLogLevel(LOG_WARNING);
    ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
    BRITE::Backends::Raylib::RaylibApplicationBackend window;
    window.Init("BRITE multisample window", SIZE, SIZE, BRITE::Backends::WindowOptions{});
    int samples = -1;
    glad_glGetIntegerv(GL_SAMPLES_, &samples);
    window.Shutdown();
    EXPECT_EQ(samples, 0);
}

// The raylib window asked for samples has a multisampled back buffer: OpenGL
// reports 4 samples a pixel -- raylib's one offer. The window is hidden, like
// every window these tests open, and a hidden window still has its back buffer.
//
// Mutation: RaylibApplicationBackend's Init with options ignoring them -> 0.
TEST(MultisampleWindow, TheRaylibWindowIsMultisampledWhenAsked) {
    ::SetTraceLogLevel(LOG_WARNING);
    ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
    BRITE::Backends::Raylib::RaylibApplicationBackend window;
    BRITE::Backends::WindowOptions options;
    options.MultisampleCount = 4;
    window.Init("BRITE multisample window", SIZE, SIZE, options);
    int samples = -1;
    glad_glGetIntegerv(GL_SAMPLES_, &samples);
    window.Shutdown();
    EXPECT_EQ(samples, 4);
}
