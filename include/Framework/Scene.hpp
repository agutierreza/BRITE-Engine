#pragma once
#include <Backends/RenderPass.hpp>
#include <Framework/Application.hpp>
#include <Systems/PhysicsSystem.hpp>
#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>

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
    virtual void OnRender(BRITE::RenderPass& pass) {}
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
    // The scene's physics. It has none until it is given a backend (PhysicsSystem::Init), and then the framework
    // steps that backend in phases 2 and 4 of every fixed tick.
    BRITE::PhysicsSystem& GetPhysicsSystem() {
        return m_physicsSystem;
    }
    Application* GetApp() {
        return m_app;
    }

  protected:
    Application* m_app;
    entt::registry m_registry;
    // After the registry: destroyed before it, and after the destructor has cleared it.
    BRITE::PhysicsSystem m_physicsSystem;
};

} // namespace framework
} // namespace brite
