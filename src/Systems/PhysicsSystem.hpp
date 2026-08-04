#pragma once
#include "IPhysicsBackend.hpp"
#include <entt/entt.hpp>
#include <memory>

namespace BRITE {

class PhysicsSystem {
  public:
    PhysicsSystem();
    ~PhysicsSystem();

    void Init(entt::registry& registry);
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
