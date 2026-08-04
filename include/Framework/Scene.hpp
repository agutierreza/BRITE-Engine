#pragma once
#include <Systems/PhysicsSystem.hpp>
#include <entt/entt.hpp>
#include <memory>
#include <raylib.h>
#include <string>
#include <vector>

namespace SoLoud {
class Wav;
class WavStream;
} // namespace SoLoud

namespace brite {
namespace framework {

class Application;

class Scene {
  public:
    Scene(Application* app);
    virtual ~Scene();

    virtual void OnStart() {}
    virtual void OnInstantiation() {}
    virtual void OnLogicStep(double dt) {}
    virtual void OnRenderPrepStep(double dt) {}
    virtual void OnRender() {}
    virtual void OnShutdown() {}

    virtual bool BlocksUpdate() const {
        return true;
    }
    virtual bool BlocksRender() const {
        return true;
    }

    entt::registry& GetRegistry() {
        return m_registry;
    }
    BRITE::PhysicsSystem& GetPhysicsSystem() {
        return m_physicsSystem;
    }
    Application* GetApp() {
        return m_app;
    }

  protected:
    Application* m_app;
    entt::registry m_registry;
    BRITE::PhysicsSystem m_physicsSystem;

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
