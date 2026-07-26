#pragma once
#include <string>
#include <memory>
#include <soloud.h>

namespace brite {
namespace framework {

class Scene;

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

    std::shared_ptr<Scene> m_currentScene;
    std::shared_ptr<Scene> m_nextScene;

    SoLoud::Soloud m_soloud;
};

} // namespace framework
} // namespace brite
