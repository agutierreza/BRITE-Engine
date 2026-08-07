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

class Application {
  public:
    Application(std::unique_ptr<BRITE::Backends::IApplicationBackend> appBackend,
                std::unique_ptr<BRITE::Backends::IInputBackend> inputBackend,
                std::unique_ptr<BRITE::Backends::IRenderBackend> renderBackend,
                const std::string& title = "BRITE Engine", const std::string& orgName = "BRITE",
                const std::string& appName = "BRITE", int width = 1280, int height = 720);
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

    void SetInternalResolution(int width, int height);
    BRITE::Vector2 GetInternalResolution() const {
        return m_internalResolution;
    }

    void PushScene(std::shared_ptr<Scene> newScene);
    void PopScene();
    void ChangeScene(std::shared_ptr<Scene> newScene);

    nlohmann::json& GetGameState() {
        return m_gameState;
    }
    bool SaveState(const std::string& filename);
    bool LoadState(const std::string& filename);

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
    std::string m_orgName;
    std::string m_appName;
    int m_width;
    int m_height;
    bool m_running;
    double m_fixedDt;
    double m_timeScale;

    std::vector<std::shared_ptr<Scene>> m_sceneStack;
    std::vector<SceneAction> m_pendingActions;

    nlohmann::json m_gameState;

    std::unique_ptr<BRITE::Backends::IApplicationBackend> m_appBackend;
    std::unique_ptr<BRITE::Backends::IInputBackend> m_inputBackend;
    std::unique_ptr<BRITE::Backends::IRenderBackend> m_renderBackend;

    PhysicsEngineType m_physicsEngine = PhysicsEngineType::Box2D;

    // Internal Resolution Management
    BRITE::TextureHandle m_framebuffer = BRITE::NullTextureHandle;
    BRITE::Vector2 m_internalResolution = {0.0f, 0.0f};
    bool m_useInternalResolution = false;
};

} // namespace framework
} // namespace brite
