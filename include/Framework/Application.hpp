#pragma once
#include <string>
#include <memory>
#include <soloud.h>
#include <raylib.h>
#include <vector>

namespace brite {
namespace framework {

class Scene;

enum class SceneActionType {
    Push,
    Pop,
    Change
};

struct SceneAction {
    SceneActionType type;
    std::shared_ptr<Scene> scene;
};

class Application {
public:
    Application(const std::string& title = "BRITE Engine", int width = 1280, int height = 720);
    virtual ~Application();

    void Run();
    void Quit();

    // Note: uses raylib's SetTargetFPS internally to unlock or lock framerate
    void SetTargetFPS(int fps);
    void SetFixedTimeStep(double dt);
    
    void SetTimeScale(double scale);
    double GetTimeScale() const { return m_timeScale; }

    void SetInternalResolution(int width, int height);
    Vector2 GetInternalResolution() const { return m_internalResolution; }

    void PushScene(std::shared_ptr<Scene> newScene);
    void PopScene();
    void ChangeScene(std::shared_ptr<Scene> newScene);

    SoLoud::Soloud& GetAudio() { return m_soloud; }

protected:
    // Override this to set the initial scene and load global assets
    virtual void OnStart() {}

private:
    void InitSubsystems(const std::string& title, int width, int height);
    void ShutdownSubsystems();

    std::string m_title;
    int m_width;
    int m_height;
    bool m_running;
    double m_fixedDt;
    double m_timeScale;

    std::vector<std::shared_ptr<Scene>> m_sceneStack;
    std::vector<SceneAction> m_pendingActions;

    SoLoud::Soloud m_soloud;

    // Internal Resolution Management
    RenderTexture2D m_framebuffer = { 0 };
    Vector2 m_internalResolution = { 0.0f, 0.0f };
    bool m_useInternalResolution = false;
};

} // namespace framework
} // namespace brite
