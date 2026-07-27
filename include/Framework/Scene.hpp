#pragma once
#include <entt/entt.hpp>
#include <box2d/box2d.h>
#include <raylib.h>
#include <vector>
#include <memory>
#include <string>

namespace SoLoud {
    class Wav;
    class WavStream;
}

namespace brite {
namespace framework {

class Application;

class Scene {
public:
    Scene(Application* app);
    virtual ~Scene();

    virtual void OnStart() {}
    virtual void OnPreStep(double dt) {}
    virtual void OnPostStep(double dt) {}
    virtual void OnRender() {}
    virtual void OnShutdown() {}

    virtual bool BlocksUpdate() const { return true; }
    virtual bool BlocksRender() const { return true; }

    entt::registry& GetRegistry() { return m_registry; }
    b2WorldId GetPhysicsWorld() { return m_worldId; }
    Application* GetApp() { return m_app; }

protected:
    Application* m_app;
    entt::registry m_registry;
    b2WorldId m_worldId;

    // Automated Asset Tracking
    std::vector<Texture2D> m_trackedTextures;
    std::vector<Font> m_trackedFonts;
    std::vector<std::unique_ptr<SoLoud::Wav>> m_trackedWavs;
    std::vector<std::unique_ptr<SoLoud::WavStream>> m_trackedWavStreams;

    Texture2D LoadSceneTexture(const std::string& path);
    Font LoadSceneFont(const std::string& path);
    SoLoud::Wav* LoadSceneWav(const std::string& path);
    SoLoud::WavStream* LoadSceneWavStream(const std::string& path);
};

} // namespace framework
} // namespace brite
