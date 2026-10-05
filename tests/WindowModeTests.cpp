// The window's mode and size, and the size the scenes draw at.
//
// An application opens windowed or borderless full screen, resizable or not,
// and can switch mode and windowed size while it runs. Both switches are
// deferred to the top of the next frame, where the sizes are read once for the
// whole frame -- so every phase of a frame lays out and draws by one size.
//
// The first cases drive the real Application::Run loop against a window that
// records what it is asked and reports the size a script gives it, and no GPU.
// The rest open real raylib windows, HIDDEN, and read what the platform says of
// them: the frame's style bits and the size of the back buffer. Going full
// screen here never shows, raises or focuses the window (see
// RaylibApplicationBackend.hpp), which is why it can be tested on the machine
// it runs on at all.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/IApplicationBackend.hpp>
#include <Backends/IRenderBackend.hpp>
#include <Backends/Raylib/RaylibApplicationBackend.hpp>
#include <Backends/RenderPass.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <gtest/gtest.h>

#include <raylib.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
// Two questions to the window manager about a window, declared rather than
// included: <windows.h> and raylib's header name the same things.
extern "C" {
__declspec(dllimport) long __stdcall GetWindowLongA(void* window, int index);
__declspec(dllimport) int __stdcall IsWindowVisible(void* window);
}
#endif

using BRITE::Backends::WindowMode;
using brite::framework::Application;
using brite::framework::ApplicationOptions;
using brite::framework::PixelSize;
using brite::framework::Scene;

namespace {

// ---------------------------------------------------------------------------
// A scripted window
// ---------------------------------------------------------------------------

// What the window was asked, in order, and what it reports. Frames are driven
// by its clock: each GetTime is a sixty-fourth of a second after the last, and
// the window closes after `frames` frames.
struct WindowScript {
    // What Init was given.
    int optionInits = 0;
    WindowMode openedMode = WindowMode::Windowed;
    bool openedResizable = false;
    int openedWidth = 0, openedHeight = 0;
    // What it reports.
    PixelSize reported{1280, 720};
    WindowMode mode = WindowMode::Windowed;
    // What it does when asked to go full screen: the size it reports then.
    PixelSize monitor{2560, 1440};
    bool canGoFullscreen = true;
    // Every change asked of it, in order: "mode:windowed", "mode:fullscreen",
    // "size:1600x900".
    std::vector<std::string> calls;
    int frames = 3;
};

class ScriptedWindow : public BRITE::Backends::IApplicationBackend {
  public:
    explicit ScriptedWindow(WindowScript& script) : m_s(script) {}

    void Init(const std::string&, int, int) override {}
    void Init(const std::string&, int width, int height, const BRITE::Backends::WindowOptions& options) override {
        ++m_s.optionInits;
        m_s.openedMode = options.Mode;
        m_s.openedResizable = options.Resizable;
        m_s.openedWidth = width;
        m_s.openedHeight = height;
        if (options.Mode == WindowMode::BorderlessFullscreen && m_s.canGoFullscreen) {
            m_s.mode = WindowMode::BorderlessFullscreen;
            m_s.reported = m_s.monitor;
        }
    }
    void Shutdown() override {}
    bool WindowShouldClose() override {
        // Run() reads the clock once before its loop, then once per frame.
        return m_clockReads > m_s.frames;
    }
    void SetTargetFPS(int) override {}
    double GetTime() override {
        return (m_clockReads++) / 64.0;
    }
    int GetScreenWidth() override {
        return m_s.reported.Width;
    }
    int GetScreenHeight() override {
        return m_s.reported.Height;
    }
    WindowMode GetWindowMode() override {
        return m_s.mode;
    }
    bool SetWindowMode(WindowMode mode) override {
        m_s.calls.push_back(mode == WindowMode::Windowed ? "mode:windowed" : "mode:fullscreen");
        if (mode == WindowMode::BorderlessFullscreen && !m_s.canGoFullscreen)
            return false;
        if (mode != m_s.mode) {
            if (mode == WindowMode::BorderlessFullscreen) {
                m_windowed = m_s.reported;
                m_s.reported = m_s.monitor;
            } else {
                m_s.reported = m_windowed;
            }
            m_s.mode = mode;
        }
        return true;
    }
    bool SetWindowSize(int width, int height) override {
        m_s.calls.push_back("size:" + std::to_string(width) + "x" + std::to_string(height));
        if (m_s.mode != WindowMode::Windowed)
            return false;
        m_s.reported = {width, height};
        return true;
    }

  private:
    WindowScript& m_s;
    PixelSize m_windowed;
    int m_clockReads = 0;
};

// Draws nothing; keeps the screen passes it was handed, for the letterbox.
class PassKeeper : public BRITE::Backends::IRenderBackend {
  public:
    std::vector<BRITE::Rectangle> screenBlits; // the destination of every sprite drawn to the window

    void SubmitRenderPass(const BRITE::RenderPass& pass) override {
        if (pass.TargetFramebuffer == BRITE::NullTextureHandle)
            for (const auto& sprite : pass.SpriteCommands)
                screenBlits.push_back(sprite.DestRect);
    }
    BRITE::TextureHandle LoadRenderTexture(int, int) override {
        return ++m_next;
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
    BRITE::TextureHandle m_next = 0;
};

// What one frame saw, read in its tick and again as it was drawn.
struct FrameSeen {
    PixelSize atTick;
    PixelSize atRender;
    PixelSize window;
    bool changed;    ///< as it was drawn
    WindowMode mode;
    bool changedAtTick; ///< as the tick began, before the frame's asks
};

// Asks the window for whatever its script says in a frame's tick, and records
// what every frame saw.
class AskingScene : public Scene {
  public:
    explicit AskingScene(Application* app) : Scene(app) {}

    // Run in the tick of the frame with that index (0 is the first frame).
    std::vector<std::pair<int, std::function<void(Application&)>>> asks;
    std::vector<FrameSeen> frames;

    void OnLogicStep(double) override {
        m_tickSize = GetApp()->GetDrawSize();
        m_tickChanged = GetApp()->DrawSizeChanged();
        for (auto& [frame, ask] : asks)
            if (frame == static_cast<int>(frames.size()))
                ask(*GetApp());
    }
    void OnRender(BRITE::RenderPass&) override {
        frames.push_back({m_tickSize, GetApp()->GetDrawSize(), GetApp()->GetWindowSize(),
                          GetApp()->DrawSizeChanged(), GetApp()->GetWindowMode(), m_tickChanged});
    }

  private:
    PixelSize m_tickSize;
    bool m_tickChanged = false;
};

class WindowApp : public Application {
  public:
    WindowApp(WindowScript& script, PassKeeper* renderer, const ApplicationOptions& options = {}, int width = 1280,
              int height = 720)
        : Application(std::make_unique<ScriptedWindow>(script), nullptr,
                      std::unique_ptr<BRITE::Backends::IRenderBackend>(renderer), "Window", "BRITE", "Engine", width,
                      height, options) {
        // One tick a frame: the frames are a sixty-fourth of a second apart.
        SetFixedTimeStep(1.0 / 64.0);
    }
    std::shared_ptr<AskingScene> scene;
    // Set before Run: run in OnStart, after the scene is pushed.
    std::function<void(WindowApp&)> onStart;

  protected:
    void OnStart() override {
        scene = std::make_shared<AskingScene>(this);
        PushScene(scene);
        if (onStart)
            onStart(*this);
    }
};

std::unique_ptr<WindowApp> MakeApp(WindowScript& script, const ApplicationOptions& options = {}) {
    return std::make_unique<WindowApp>(script, new PassKeeper, options);
}

} // namespace

// ---------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------

// Asked to open full screen and resizable, the application creates its window
// with both, given the constructor's size as its windowed size; reports the
// mode the window opened in; and has the monitor's size before any frame runs.
//
// Mutations: the mode not passed on -> openedMode Windowed; Resizable not passed
// on -> false; the mode not read back after Init -> GetWindowMode Windowed; the
// sizes not read at construction -> 0 x 0.
TEST(WindowOpening, OpensInTheModeAskedWithTheMonitorsSizeAtOnce) {
    WindowScript script;
    ApplicationOptions options;
    options.Mode = WindowMode::BorderlessFullscreen;
    options.Resizable = true;
    auto app = MakeApp(script, options);

    EXPECT_EQ(script.optionInits, 1);
    EXPECT_EQ(script.openedMode, WindowMode::BorderlessFullscreen);
    EXPECT_TRUE(script.openedResizable);
    EXPECT_EQ(script.openedWidth, 1280) << "the windowed size it returns to";
    EXPECT_EQ(script.openedHeight, 720);
    EXPECT_EQ(app->GetWindowMode(), WindowMode::BorderlessFullscreen);
    EXPECT_EQ(app->GetWindowSize(), (PixelSize{2560, 1440}));
    EXPECT_EQ(app->GetDrawSize(), (PixelSize{2560, 1440}));
    EXPECT_FALSE(app->DrawSizeChanged()) << "nothing has changed yet: there was no size before";
}

// Asked for nothing, the window opens windowed, fixed in size, at the size the
// constructor was given.
//
// Mutation: ApplicationOptions defaulting to Resizable -> true.
TEST(WindowOpening, OpensWindowedAndFixedUnlessAsked) {
    WindowScript script;
    auto app = MakeApp(script);

    EXPECT_EQ(script.openedMode, WindowMode::Windowed);
    EXPECT_FALSE(script.openedResizable);
    EXPECT_EQ(app->GetWindowMode(), WindowMode::Windowed);
    EXPECT_EQ(app->GetDrawSize(), (PixelSize{1280, 720}));
}

// With no window backend there is no window to read: both sizes are what the
// constructor was given, and a window change asked for is dropped, not kept.
//
// Mutation: the sizes taken from nothing when there is no backend -> 0 x 0.
TEST(WindowOpening, WithNoWindowTheSizesAreTheOnesGiven) {
    Application app(nullptr, nullptr, nullptr, "Window", "BRITE", "Engine", 640, 360);
    EXPECT_EQ(app.GetWindowSize(), (PixelSize{640, 360}));
    EXPECT_EQ(app.GetDrawSize(), (PixelSize{640, 360}));
    app.SetWindowMode(WindowMode::BorderlessFullscreen);
    EXPECT_EQ(app.GetWindowMode(), WindowMode::Windowed);
}

// ---------------------------------------------------------------------------
// Switching, a frame at a time
// ---------------------------------------------------------------------------

// Full screen asked for in frame 0's tick is not applied in frame 0: frame 0 is
// drawn at the size its tick saw, 1280 x 720, in windowed mode. Frame 1 opens
// with the switch made, and both its tick and its drawing see the monitor's
// 2560 x 1440; it is the one frame that says the size changed. Frame 2 sees the
// same size and says nothing changed.
//
// Mutations: SetWindowMode applying at once -> frame 0 renders at 2560 x 1440
// after ticking at 1280 x 720; the requests applied after the sizes are read ->
// frame 1 still 1280 x 720; DrawSizeChanged never set -> frame 1 false; never
// cleared -> frame 2 true.
TEST(WindowSwitching, FullScreenIsAppliedAtTheTopOfTheNextFrameAndSeenByAllOfIt) {
    WindowScript script;
    auto app = MakeApp(script);
    app->onStart = [](WindowApp& a) {
        a.scene->asks.push_back({0, [](Application& x) { x.SetWindowMode(WindowMode::BorderlessFullscreen); }});
    };
    app->Run();

    const auto& f = app->scene->frames;
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0].atTick, (PixelSize{1280, 720}));
    EXPECT_EQ(f[0].atRender, (PixelSize{1280, 720})) << "frame 0 is drawn at the size it ticked at";
    EXPECT_EQ(f[0].mode, WindowMode::Windowed);
    EXPECT_FALSE(f[0].changed);

    EXPECT_EQ(f[1].atTick, (PixelSize{2560, 1440}));
    EXPECT_EQ(f[1].atRender, (PixelSize{2560, 1440}));
    EXPECT_EQ(f[1].mode, WindowMode::BorderlessFullscreen);
    EXPECT_TRUE(f[1].changed);

    EXPECT_EQ(f[2].atRender, (PixelSize{2560, 1440}));
    EXPECT_FALSE(f[2].changed);
    EXPECT_EQ(script.calls, (std::vector<std::string>{"mode:fullscreen"}));
}

// Asked twice in one frame, the window is asked once, for the last.
//
// Mutation: every request passed on -> two calls.
TEST(WindowSwitching, TheLastModeAskedInAFrameWins) {
    WindowScript script;
    auto app = MakeApp(script);
    app->onStart = [](WindowApp& a) {
        a.scene->asks.push_back({0, [](Application& x) {
                                     x.SetWindowMode(WindowMode::BorderlessFullscreen);
                                     x.SetWindowMode(WindowMode::Windowed);
                                 }});
    };
    app->Run();
    EXPECT_EQ(script.calls, (std::vector<std::string>{"mode:windowed"}));
    EXPECT_EQ(app->scene->frames.back().atRender, (PixelSize{1280, 720}));
}

// A windowed size asked for while full screen is a size to return to: it is not
// passed to the window while it is full screen. In frame 0's tick: full screen
// and 1600 x 900, together. Frame 1 is full screen at the monitor's size, and
// the window was asked for the mode only. In frame 1's tick: windowed. Frame 2
// is windowed at 1600 x 900 -- the mode changed first, then the size kept.
//
// Mutations: the size applied whatever the mode -> "size:1600x900" asked of a
// full-screen window in frame 1; the size dropped when it cannot be applied ->
// frame 2 is 1280 x 720; the size applied before the mode -> it is asked while
// still full screen.
TEST(WindowSwitching, ASizeAskedWhileFullScreenWaitsForWindowed) {
    WindowScript script;
    auto app = MakeApp(script);
    app->onStart = [](WindowApp& a) {
        a.scene->asks.push_back({0, [](Application& x) {
                                     x.SetWindowMode(WindowMode::BorderlessFullscreen);
                                     x.SetWindowSize(1600, 900);
                                 }});
        a.scene->asks.push_back({1, [](Application& x) { x.SetWindowMode(WindowMode::Windowed); }});
    };
    app->Run();

    const auto& f = app->scene->frames;
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[1].atRender, (PixelSize{2560, 1440}));
    EXPECT_EQ(f[2].atRender, (PixelSize{1600, 900}));
    EXPECT_EQ(f[2].mode, WindowMode::Windowed);
    EXPECT_EQ(script.calls, (std::vector<std::string>{"mode:fullscreen", "mode:windowed", "size:1600x900"}));
}

// A windowed size asked while windowed is applied at the next frame's top, and
// a size of nothing is ignored rather than passed on.
//
// Mutations: the zero size passed on -> "size:0x900" asked; the size applied at
// once -> frame 0 renders at 1600 x 900.
TEST(WindowSwitching, AWindowedSizeIsAppliedNextFrameAndNothingIsIgnored) {
    WindowScript script;
    auto app = MakeApp(script);
    app->onStart = [](WindowApp& a) {
        a.scene->asks.push_back({0, [](Application& x) {
                                     x.SetWindowSize(1600, 900);
                                     x.SetWindowSize(0, 900);
                                 }});
    };
    app->Run();
    const auto& f = app->scene->frames;
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0].atRender, (PixelSize{1280, 720}));
    EXPECT_EQ(f[1].atRender, (PixelSize{1600, 900}));
    EXPECT_TRUE(f[1].changed);
    EXPECT_EQ(script.calls, (std::vector<std::string>{"size:1600x900"}));
}

// A window that cannot go full screen stays windowed at its size, and the
// application says it is windowed -- not the mode it asked for.
//
// Mutation: GetWindowMode reporting the mode asked rather than the window's ->
// BorderlessFullscreen.
TEST(WindowSwitching, AModeTheWindowRefusesIsNotReported) {
    WindowScript script;
    script.canGoFullscreen = false;
    auto app = MakeApp(script);
    app->onStart = [](WindowApp& a) {
        a.scene->asks.push_back({0, [](Application& x) { x.SetWindowMode(WindowMode::BorderlessFullscreen); }});
    };
    app->Run();
    EXPECT_EQ(app->scene->frames.back().mode, WindowMode::Windowed);
    EXPECT_EQ(app->scene->frames.back().atRender, (PixelSize{1280, 720}));
}

// ---------------------------------------------------------------------------
// The size, read each frame
// ---------------------------------------------------------------------------

// The person at the screen resizes the window between frames 0 and 1 (the
// window reports 1920 x 1080 with nothing asked), and minimises it between 1
// and 2 (it reports 0 x 0). Frame 1 sees the new size and says it changed;
// frame 2 keeps 1920 x 1080 and says nothing changed.
//
// Mutations: the size not read each frame -> frame 1 still 1280 x 720; a
// reported 0 x 0 taken -> frame 2 is 0 x 0.
TEST(WindowSize, AResizeIsSeenNextFrameAndAMinimisedWindowKeepsItsSize) {
    WindowScript script;
    auto app = MakeApp(script);
    app->onStart = [&script](WindowApp& a) {
        a.scene->asks.push_back({0, [&script](Application&) { script.reported = {1920, 1080}; }});
        a.scene->asks.push_back({1, [&script](Application&) { script.reported = {0, 0}; }});
    };
    app->Run();
    const auto& f = app->scene->frames;
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[1].atTick, (PixelSize{1920, 1080}));
    EXPECT_TRUE(f[1].changed);
    EXPECT_EQ(f[2].window, (PixelSize{1920, 1080}));
    EXPECT_EQ(f[2].atRender, (PixelSize{1920, 1080}));
    EXPECT_FALSE(f[2].changed);
}

// At an internal resolution the scenes draw at that resolution whatever the
// window's size, and the finished picture is letterboxed into the window's
// size as read that frame. 320 x 180 into 2560 x 1440 is 8 times exactly: the
// whole window, {0, 0, 2560, 1440}. The window then becomes 2560 x 1080 (a
// wider shape): the scale is min(2560 / 320, 1080 / 180) = min(8, 6) = 6, so the
// picture is 1920 x 1080, centred: x = (2560 - 1920) / 2 = 320, y = 0.
//
// Mutations: the letterbox reading the constructor's size -> the second blit
// fills 1280 x 720's shape; GetDrawSize reporting the window's size under an
// internal resolution -> 2560 x 1440; SetInternalResolution leaving the draw
// size to the next frame -> 2560 x 1440 straight after it.
TEST(WindowSize, AnInternalResolutionIsTheDrawSizeAndIsLetterboxedIntoTheWindowAsItIsNow) {
    WindowScript script;
    script.reported = {2560, 1440};
    auto* renderer = new PassKeeper;
    auto app = std::make_unique<WindowApp>(script, renderer);
    app->SetInternalResolution(320, 180);
    EXPECT_EQ(app->GetDrawSize(), (PixelSize{320, 180})) << "at once";
    EXPECT_TRUE(app->DrawSizeChanged());
    app->onStart = [&script](WindowApp& a) {
        a.scene->asks.push_back({0, [&script](Application&) { script.reported = {2560, 1080}; }});
    };
    app->Run();

    const auto& f = app->scene->frames;
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[1].atRender, (PixelSize{320, 180}));
    EXPECT_EQ(f[1].window, (PixelSize{2560, 1080}));
    ASSERT_EQ(renderer->screenBlits.size(), 3u);
    const BRITE::Rectangle first = renderer->screenBlits[0];
    EXPECT_FLOAT_EQ(first.x, 0.0f);
    EXPECT_FLOAT_EQ(first.y, 0.0f);
    EXPECT_FLOAT_EQ(first.width, 2560.0f);
    EXPECT_FLOAT_EQ(first.height, 1440.0f);
    const BRITE::Rectangle second = renderer->screenBlits[1];
    EXPECT_FLOAT_EQ(second.x, 320.0f);
    EXPECT_FLOAT_EQ(second.y, 0.0f);
    EXPECT_FLOAT_EQ(second.width, 1920.0f);
    EXPECT_FLOAT_EQ(second.height, 1080.0f);
}

// ---------------------------------------------------------------------------
// A backend written before modes existed
// ---------------------------------------------------------------------------

// Torus's pattern: the window is read at construction, 1280 x 720, and OnStart
// then sets an internal resolution of 320 x 180, before any frame. Frame 0
// began at 320 x 180 and the size read at construction was 1280 x 720, so frame
// 0 says the size changed -- in its tick and as it is drawn. Frame 1 began at
// 320 x 180 as frame 0 did, and says nothing changed.
//
// Mutation: ReadSizes comparing with the draw size as it stands (which
// SetInternalResolution has already moved) rather than with the previous
// frame's top -> frame 0 says nothing changed.
TEST(WindowSize, AnInternalResolutionSetInOnStartIsAChangeInFrameZero) {
    WindowScript script;
    auto app = MakeApp(script);
    app->onStart = [](WindowApp& a) { a.SetInternalResolution(320, 180); };
    app->Run();

    const auto& f = app->scene->frames;
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0].atTick, (PixelSize{320, 180}));
    EXPECT_TRUE(f[0].changedAtTick) << "320 x 180 against construction's 1280 x 720";
    EXPECT_TRUE(f[0].changed);
    EXPECT_FALSE(f[1].changedAtTick);
    EXPECT_FALSE(f[1].changed);
}

// An internal resolution set during frame 1's tick, after the tick read the
// flag: frame 1's tick saw nothing changed (frames 0 and 1 both began at 1280 x
// 720), but the rest of frame 1 draws into the new 320 x 180 framebuffer and
// says so as it is drawn. Frame 2 began at 320 x 180 and frame 1 began at 1280 x
// 720, so frame 2 says it again, in its tick and as drawn. Frame 3 began as
// frame 2 did: nothing changed.
//
// Mutations: SetInternalResolution not setting the flag at once -> frame 1
// drawn with false; ReadSizes comparing with the draw size as it stands -> frame
// 2 false; the frame's top not kept for the next comparison -> frame 3 true.
TEST(WindowSize, AnInternalResolutionSetMidFrameIsAChangeForTheRestOfItAndTheNext) {
    WindowScript script;
    script.frames = 4;
    auto app = MakeApp(script);
    app->onStart = [](WindowApp& a) {
        a.scene->asks.push_back({1, [](Application& x) { x.SetInternalResolution(320, 180); }});
    };
    app->Run();

    const auto& f = app->scene->frames;
    ASSERT_EQ(f.size(), 4u);
    EXPECT_FALSE(f[1].changedAtTick) << "read before the call";
    EXPECT_EQ(f[1].atRender, (PixelSize{320, 180}));
    EXPECT_TRUE(f[1].changed) << "the rest of frame 1";
    EXPECT_TRUE(f[2].changedAtTick) << "frame 2 began at 320 x 180, frame 1 at 1280 x 720";
    EXPECT_TRUE(f[2].changed);
    EXPECT_FALSE(f[3].changedAtTick);
    EXPECT_FALSE(f[3].changed);
}

namespace {
class OlderWindow : public BRITE::Backends::IApplicationBackend {
  public:
    void Init(const std::string&, int, int) override {}
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
} // namespace

// It is windowed, stays windowed when asked for windowed, refuses full screen
// and refuses a size: never half-done, and never claiming what it cannot do.
//
// Mutations: the default SetWindowMode returning true for any mode -> true;
// returning false for windowed -> false; the default SetWindowSize returning
// true -> true.
TEST(WindowOlderBackend, IsWindowedAndRefusesWhatItCannotDo) {
    OlderWindow window;
    BRITE::Backends::IApplicationBackend& asInterface = window;
    EXPECT_EQ(asInterface.GetWindowMode(), WindowMode::Windowed);
    EXPECT_TRUE(asInterface.SetWindowMode(WindowMode::Windowed));
    EXPECT_FALSE(asInterface.SetWindowMode(WindowMode::BorderlessFullscreen));
    EXPECT_FALSE(asInterface.SetWindowSize(800, 600));
}

// ---------------------------------------------------------------------------
// raylib, under a hidden window
// ---------------------------------------------------------------------------

#if defined(_WIN32)
namespace {
// Window style bits, from WinUser.h.
constexpr long WS_CAPTION_ = 0x00C00000L;
constexpr long WS_THICKFRAME_ = 0x00040000L;
constexpr int GWL_STYLE_ = -16;

long Style() {
    return GetWindowLongA(::GetWindowHandle(), GWL_STYLE_);
}

BRITE::Backends::WindowOptions Options(bool resizable, WindowMode mode = WindowMode::Windowed) {
    BRITE::Backends::WindowOptions options;
    options.Resizable = resizable;
    options.Mode = mode;
    return options;
}
} // namespace

// A window asked to be resizable has a sizing frame; the next window in the same
// process, not asked, has none -- though raylib keeps the first one's flag for
// every later window.
//
// Mutations: Resizable not applied -> the first has no sizing frame; the flag
// left as the last window left it -> the second has one.
TEST(WindowRaylib, ResizableIsTheWindowsOwnNotTheLastWindowsLeftover) {
    ::SetTraceLogLevel(LOG_WARNING);
    ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
    BRITE::Backends::Raylib::RaylibApplicationBackend window;

    window.Init("BRITE window modes", 320, 180, Options(true));
    const long resizable = Style();
    window.Shutdown();

    window.Init("BRITE window modes", 320, 180, Options(false));
    const long fixed = Style();
    window.Shutdown();

    EXPECT_NE(resizable & WS_THICKFRAME_, 0) << "asked to be resizable";
    EXPECT_EQ(fixed & WS_THICKFRAME_, 0) << "not asked, after one that was";
}

// Borderless full screen on a hidden window: the frame goes, the window covers
// its monitor -- the back buffer is the monitor's width and height, as raylib
// learns from the platform's resize -- and it is still hidden. Back to windowed:
// the frame returns and the window is 320 x 180 where it was. Resized while
// windowed it takes the new size; asked while full screen it refuses.
//
// Mutations: the frame not removed -> WS_CAPTION still set; the window not sized
// to the monitor -> the render width stays 320; the windowed size not restored
// -> the monitor's width after returning; the frame not restored -> no
// WS_CAPTION; SetWindowSize obeyed while full screen -> true.
TEST(WindowRaylib, BorderlessFullScreenCoversTheMonitorAndReturnsToTheWindow) {
    ::SetTraceLogLevel(LOG_WARNING);
    ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
    BRITE::Backends::Raylib::RaylibApplicationBackend window;
    window.Init("BRITE window modes", 320, 180, Options(false));
    const int monitor = ::GetCurrentMonitor();
    const int monitorWidth = ::GetMonitorWidth(monitor);
    const int monitorHeight = ::GetMonitorHeight(monitor);
    const ::Vector2 windowedAt = ::GetWindowPosition();
    ASSERT_GT(monitorWidth, 320);
    EXPECT_NE(Style() & WS_CAPTION_, 0) << "a window with a frame";

    ASSERT_TRUE(window.SetWindowMode(WindowMode::BorderlessFullscreen));
    const long fullStyle = Style();
    const int fullWidth = window.GetScreenWidth(), fullHeight = window.GetScreenHeight();
    const int fullRender = ::GetRenderWidth(), fullRenderHeight = ::GetRenderHeight();
    const bool refusedSize = !window.SetWindowSize(400, 240);
    const bool hiddenWhileFull = IsWindowVisible(::GetWindowHandle()) == 0;
    const WindowMode fullMode = window.GetWindowMode();

    ASSERT_TRUE(window.SetWindowMode(WindowMode::Windowed));
    const long backStyle = Style();
    const int backWidth = window.GetScreenWidth(), backHeight = window.GetScreenHeight();
    const int backRender = ::GetRenderWidth();
    const ::Vector2 backAt = ::GetWindowPosition();

    const bool resized = window.SetWindowSize(400, 240);
    const int resizedRender = ::GetRenderWidth(), resizedRenderHeight = ::GetRenderHeight();
    window.Shutdown();

    EXPECT_EQ(fullMode, WindowMode::BorderlessFullscreen);
    EXPECT_EQ(fullStyle & WS_CAPTION_, 0) << "no frame in full screen";
    EXPECT_EQ(fullWidth, monitorWidth);
    EXPECT_EQ(fullHeight, monitorHeight);
    EXPECT_EQ(fullRender, monitorWidth) << "the back buffer itself";
    EXPECT_EQ(fullRenderHeight, monitorHeight);
    EXPECT_TRUE(refusedSize) << "a full-screen window's size is its monitor's";
    EXPECT_TRUE(hiddenWhileFull) << "going full screen shows nothing";

    EXPECT_NE(backStyle & WS_CAPTION_, 0) << "the frame back";
    EXPECT_EQ(backWidth, 320);
    EXPECT_EQ(backHeight, 180);
    EXPECT_EQ(backRender, 320);
    EXPECT_FLOAT_EQ(backAt.x, windowedAt.x);
    EXPECT_FLOAT_EQ(backAt.y, windowedAt.y);

    EXPECT_TRUE(resized);
    EXPECT_EQ(resizedRender, 400);
    EXPECT_EQ(resizedRenderHeight, 240);
}

// Opened full screen, the window covers the monitor from the first frame, and
// the size it was opened with is the one it returns to.
//
// Mutation: Init ignoring the mode -> the render width is 320.
TEST(WindowRaylib, OpenedFullScreenReturnsToTheSizeItWasOpenedWith) {
    ::SetTraceLogLevel(LOG_WARNING);
    ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
    BRITE::Backends::Raylib::RaylibApplicationBackend window;
    window.Init("BRITE window modes", 320, 180, Options(false, WindowMode::BorderlessFullscreen));
    const int monitorWidth = ::GetMonitorWidth(::GetCurrentMonitor());
    const int openedRender = ::GetRenderWidth();
    const WindowMode openedMode = window.GetWindowMode();
    window.SetWindowMode(WindowMode::Windowed);
    const int backRender = ::GetRenderWidth();
    window.Shutdown();

    EXPECT_EQ(openedMode, WindowMode::BorderlessFullscreen);
    EXPECT_EQ(openedRender, monitorWidth);
    EXPECT_EQ(backRender, 320);
}
#endif
