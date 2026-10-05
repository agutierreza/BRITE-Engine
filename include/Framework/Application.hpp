#pragma once
#include "Backends/IApplicationBackend.hpp"
#include "Backends/IInputBackend.hpp"
#include "Backends/IRenderBackend.hpp"
#include "Math/BriteMath.hpp"
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace brite {
namespace framework {

class Scene;

enum class PhysicsEngineType { Box2D, Box3D };

enum class SceneActionType { Push, Pop, Change };

struct SceneAction {
    SceneActionType type;
    std::shared_ptr<Scene> scene;
};

// What an application settles before its window exists; the constructor
// creates the window with them. The window's mode and size can also be changed
// afterwards (SetWindowMode, SetWindowSize); the rest cannot.
struct ApplicationOptions {
    // Samples per pixel for multisample anti-aliasing; 1 is none. The window is
    // asked for a multisampled back buffer. An application rendering at an
    // internal resolution draws its scenes into a framebuffer of its own and
    // hands the window one finished picture, whose edges a multisampled window
    // cannot reach -- so that framebuffer is multisampled too, and resolved
    // before post-processing and the final blit read it.
    int MultisampleCount = 1;
    // The mode the window opens in. Opened full screen, the constructor's width
    // and height are the windowed size: the one it takes on leaving full screen.
    BRITE::Backends::WindowMode Mode = BRITE::Backends::WindowMode::Windowed;
    // Whether the person at the screen may resize the window by its frame.
    bool Resizable = false;
};

// A size in pixels.
struct PixelSize {
    int Width = 0;
    int Height = 0;
    bool operator==(const PixelSize&) const = default;
};

class Application {
  public:
    Application(std::unique_ptr<BRITE::Backends::IApplicationBackend> appBackend,
                std::unique_ptr<BRITE::Backends::IInputBackend> inputBackend,
                std::unique_ptr<BRITE::Backends::IRenderBackend> renderBackend,
                const std::string& title = "BRITE Engine", const std::string& orgName = "BRITE",
                const std::string& appName = "BRITE", int width = 1280, int height = 720,
                const ApplicationOptions& options = {});

    const ApplicationOptions& GetOptions() const {
        return m_options;
    }
    virtual ~Application();

    void Run();
    void Quit();

    // Note: uses raylib's SetTargetFPS internally to unlock or lock framerate
    void SetTargetFPS(int fps);
    void SetFixedTimeStep(double dt);

    void SetTimeScale(double scale);
    double GetTimeScale() const {
        return m_timeScale;
    }

    // How far the clock has advanced into the fixed tick that has not run yet,
    // as a fraction of a tick, in [0, 1).
    //
    // A fixed-timestep loop that renders faster than it simulates draws the
    // same state on every frame between two ticks, so anything drawn moves in
    // steps of one tick however many frames are drawn. This is the missing
    // half of the fixed timestep: the residual time the loop could not turn
    // into a tick. A consumer that keeps the previous tick's state can draw
    // previous + (current - previous) * TickFraction() and move on every frame.
    //
    // VALID DURING RENDERING -- OnRender -- and set once per frame, after the
    // tick loop has drained the accumulator and before any scene is rendered.
    // During the fixed-tick phases (OnInstantiation, OnLogicStep,
    // OnRenderPrepStep) it still holds the PREVIOUS frame's value, which says
    // nothing about the tick in progress: a tick is a whole step, and the
    // residual belongs to the frame drawn after it. Zero before the first
    // frame has run.
    double TickFraction() const {
        return m_tickFraction;
    }

    // Draw the scenes into a framebuffer of this size, letterboxed into the
    // window. It changes the draw size (below) at once, not at the next frame:
    // the framebuffer is replaced now.
    void SetInternalResolution(int width, int height);
    BRITE::Vector2 GetInternalResolution() const {
        return m_internalResolution;
    }

    // The window, and what the scenes draw at.
    //
    // Both sizes are read ONCE PER FRAME, at the top of the frame, after any
    // window change asked for has been applied and before any scene ticks or
    // renders -- so every phase of a frame sees the same size, and the size a
    // scene lays out by is the size its OnRender is drawn at. They are first
    // read as the constructor creates the window, so they are valid in OnStart.
    //
    //   GetWindowSize  what the window draws into, in pixels. A window reported
    //                  as nothing wide or high -- minimised -- keeps the size it
    //                  had, so nothing laid out by it collapses to zero.
    //   GetDrawSize    what the scenes draw into: the internal resolution when
    //                  one is set, and otherwise the window's size. A scene
    //                  sizing anything to the screen asks this.
    //   DrawSizeChanged  whether the draw size this frame began with differs
    //                  from the one the previous frame began with (for the first
    //                  frame, from the size read at construction): the frame to
    //                  lay out again in. SetInternalResolution, which changes the
    //                  draw size at once, also sets it at once, for the rest of
    //                  its frame -- and the next frame reports the change again.
    //                  A change may be reported twice; it is never missed.
    //
    // With no window backend there is no window to read, and both are the
    // width and height the constructor was given.
    PixelSize GetWindowSize() const {
        return m_windowSize;
    }
    PixelSize GetDrawSize() const {
        return m_drawSize;
    }
    bool DrawSizeChanged() const {
        return m_drawSizeChanged;
    }

    // Changing the window. Both are DEFERRED, like the scene stack: kept, and
    // applied at the top of the next frame before the sizes above are read --
    // so they are safe to call from any phase, and a frame never draws at a
    // size its scenes did not see. The last of each asked before a frame wins.
    //
    //   SetWindowMode  windowed, or borderless full screen at the monitor's own
    //                  resolution. A backend that cannot is left as it is, with
    //                  a warning; GetWindowMode says what the window is in.
    //   SetWindowSize  the windowed size. Applied at once when the window is
    //                  windowed; while it is full screen, kept, and applied as
    //                  it returns to windowed -- full screen's size belongs to
    //                  the monitor. Applied in that order within one frame: the
    //                  mode, then the size.
    void SetWindowMode(BRITE::Backends::WindowMode mode);
    void SetWindowSize(int width, int height);
    // The mode the window is in, as of the top of this frame: a mode asked for
    // during the frame is not reported until the frame that applies it.
    BRITE::Backends::WindowMode GetWindowMode() const {
        return m_windowMode;
    }

    // The scene stack. Each of these is DEFERRED: it is queued, and the queue is
    // applied at the top of the next frame, in the order it was asked for and
    // before any scene ticks -- so all three are safe to call from inside a scene.
    //
    //   PushScene    starts `newScene` on top of the stack.
    //   PopScene     shuts the top scene down and removes it.
    //   ChangeScene  REPLACES THE WHOLE STACK: shuts down every scene on it, from
    //                the top down, and then starts `newScene` as the only one.
    //
    // Whatever must outlive a ChangeScene therefore cannot be owned by a scene on
    // the stack. Own it in the Application (or above it) and hand it to each
    // scene, for example through the scene's constructor -- which runs while the
    // old scenes are still alive, because the change is applied only afterwards.
    //
    // To replace only the top scene and keep the ones under it, call PopScene and
    // then PushScene in the same frame: both are applied together, so no tick
    // sees the stack without its top.
    void PushScene(std::shared_ptr<Scene> newScene);
    void PopScene();
    void ChangeScene(std::shared_ptr<Scene> newScene);

    nlohmann::json& GetGameState() {
        return m_gameState;
    }
    bool SaveState(const std::string& filename);
    bool LoadState(const std::string& filename);

    // TODO: We need an architecture to extract all parameters from existing and future shaders
    // dynamically, rather than relying on hardcoded GetShaderLocation/SetShaderValue calls in client code.
    void AddPostProcessShader(BRITE::ShaderHandle shader) {
        m_postProcessShaders.push_back(shader);
    }
    void ClearPostProcessShaders() {
        m_postProcessShaders.clear();
    }

    // Removed GetAudio() to decouple SoLoud from the core framework

    BRITE::Backends::IRenderBackend* GetRenderBackend() {
        return m_renderBackend.get();
    }

    void SetPhysicsEngine(PhysicsEngineType type) {
        m_physicsEngine = type;
    }

    PhysicsEngineType GetPhysicsEngine() const {
        return m_physicsEngine;
    }

  protected:
    // Override this to set the initial scene and load global assets
    virtual void OnStart() {}

    // Override this for what the application itself does on a fixed tick,
    // before any scene: input that belongs to the application rather than to
    // a scene, such as a full-screen key, a screenshot key or a pause key.
    //
    // Called once per fixed tick, with the fixed timestep, AFTER that tick's
    // input has been applied -- so the actions read here are the tick's, as a
    // scene's are -- and BEFORE the scene stack is walked. So a key claimed here
    // (InputManager::ClaimKey) is hidden from every scene's actions in the same
    // tick, and a window or scene change asked for here is applied at the top
    // of the next frame like any other. It runs whatever the scenes do: when a
    // scene blocks the updates of those under it, and when the stack is empty.
    // The default does nothing.
    virtual void OnFixedTick(double dt) {
        (void)dt;
    }

  private:
    void InitSubsystems(const std::string& title, int width, int height);
    void ShutdownSubsystems();
    // Applies the window changes asked for, then reads the sizes. Once per
    // frame, at its top, and once as the window is created.
    void ApplyWindowRequests();
    void ReadSizes();

    std::string m_title;
    ApplicationOptions m_options;
    std::string m_orgName;
    std::string m_appName;
    int m_width;
    int m_height;
    bool m_running;
    // This Application started the profiler, so its destructor stops it (Core/Profiler.hpp).
    bool m_startedProfiler = false;
    double m_fixedDt;
    double m_timeScale;

    // Frame time not yet turned into ticks, in seconds; what the tick loop
    // drains. Kept between frames, which is what makes the timestep fixed.
    double m_accumulator = 0.0;
    // m_accumulator / m_fixedDt, taken once per frame after the tick loop.
    // See TickFraction().
    double m_tickFraction = 0.0;

    std::vector<std::shared_ptr<Scene>> m_sceneStack;
    std::vector<SceneAction> m_pendingActions;

    nlohmann::json m_gameState;

    std::unique_ptr<BRITE::Backends::IApplicationBackend> m_appBackend;
    std::unique_ptr<BRITE::Backends::IInputBackend> m_inputBackend;
    std::unique_ptr<BRITE::Backends::IRenderBackend> m_renderBackend;

    PhysicsEngineType m_physicsEngine = PhysicsEngineType::Box2D;

    // Internal Resolution Management
    BRITE::TextureHandle m_framebuffer = BRITE::NullTextureHandle;
    BRITE::TextureHandle m_framebufferAlt = BRITE::NullTextureHandle;
    BRITE::Vector2 m_internalResolution = {0.0f, 0.0f};
    bool m_useInternalResolution = false;

    // The window: what it is, and what has been asked of it since the last frame.
    PixelSize m_windowSize;
    PixelSize m_drawSize;
    bool m_drawSizeChanged = false;
    // The draw size as the last ReadSizes left it: what DrawSizeChanged compares with.
    PixelSize m_frameTopDrawSize;
    BRITE::Backends::WindowMode m_windowMode = BRITE::Backends::WindowMode::Windowed;
    bool m_modeRequested = false;
    BRITE::Backends::WindowMode m_requestedMode = BRITE::Backends::WindowMode::Windowed;
    // A windowed size asked for and not yet applied: asked this frame, or kept
    // while the window is full screen.
    bool m_sizeRequested = false;
    PixelSize m_requestedSize;

    std::vector<BRITE::ShaderHandle> m_postProcessShaders;
};

} // namespace framework
} // namespace brite
