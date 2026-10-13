#pragma once
#include "IPhysicsBackend.hpp"
#include <entt/entt.hpp>
#include <memory>

namespace BRITE {

// A scene's physics: the backend it was given, if any, and the calls the framework and the scene make on it. With no
// backend every call does nothing, and GetJointForce reads 0.
class PhysicsSystem {
  public:
    PhysicsSystem();
    ~PhysicsSystem();

    // Gives the scene its physics: `backend` -- for instance a Box2DBackend or a Box3DBackend, from their modules --
    // is initialised on `registry` now, and from then on the framework steps it in phases 2 and 4 of every fixed tick
    // (PreStep before the scene's OnLogicStep, Step after it). A backend given before replaces the one before it.
    void Init(entt::registry& registry, std::unique_ptr<IPhysicsBackend> backend);
    void PreStep(entt::registry& registry);
    void Step(entt::registry& registry, float dt);

    void SetMaterial(entt::registry& registry, entt::entity entity, std::shared_ptr<PhysicsMaterial> material);

    float GetJointForce(entt::registry& registry, entt::entity entity);
    void DestroyJoint(entt::registry& registry, entt::entity entity);

    IPhysicsBackend* GetBackend() {
        return m_backend.get();
    }

  private:
    std::unique_ptr<IPhysicsBackend> m_backend;
};

} // namespace BRITE
