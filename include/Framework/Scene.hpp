#pragma once
#include <entt/entt.hpp>
#include <box2d/box2d.h>

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

    entt::registry& GetRegistry() { return m_registry; }
    b2WorldId GetPhysicsWorld() { return m_worldId; }
    Application* GetApp() { return m_app; }

protected:
    Application* m_app;
    entt::registry m_registry;
    b2WorldId m_worldId;
};

} // namespace framework
} // namespace brite
