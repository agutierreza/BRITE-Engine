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
// creates the window, so these cannot change afterwards.
struct ApplicationOptions {
    // Samples per pixel for multisample anti-aliasing; 1 is none. The window is
    // asked for a multisampled back buffer. An application rendering at an
    // internal resolution draws its scenes into a framebuffer of its own and
    // hands the window one finished picture, whose edges a multisampled window
    // cannot reach -- so that framebuffer is multisampled too, and resolved
    // before post-processing and the final blit read it.
    int MultisampleCount = 1;
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

    void SetInternalResolution(int width, int height);
    BRITE::Vector2 GetInternalResolution() const {
        return m_internalResolution;
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

  private:
    void InitSubsystems(const std::string& title, int width, int height);
    void ShutdownSubsystems();

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

    std::vector<BRITE::ShaderHandle> m_postProcessShaders;
};

} // namespace framework
} // namespace brite
