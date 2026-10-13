#pragma once
#include <entt/entt.hpp>
#include <memory>

namespace BRITE {

struct PhysicsMaterial;

class IPhysicsBackend {
  public:
    virtual ~IPhysicsBackend() = default;

    virtual void Init(entt::registry& registry) = 0;
    virtual void PreStep(entt::registry& registry) = 0;
    virtual void Step(entt::registry& registry, float dt) = 0;

    virtual void SetMaterial(entt::registry& registry, entt::entity entity,
                             std::shared_ptr<PhysicsMaterial> material) = 0;

    virtual float GetJointForce(entt::registry& registry, entt::entity entity) = 0;
    virtual void DestroyJoint(entt::registry& registry, entt::entity entity) = 0;
};

} // namespace BRITE
