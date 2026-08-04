#pragma once
#include "IPhysicsBackend.hpp"
#include <ECS/Components.hpp>
#include <box3d/box3d.h>
#include <cassert>
#include <cstring>

namespace BRITE {

class Box3DBackend : public IPhysicsBackend {
  public:
    Box3DBackend();
    ~Box3DBackend() override;

    void Init(entt::registry& registry) override;
    void PreStep(entt::registry& registry) override;
    void Step(entt::registry& registry, float dt) override;

    void SetMaterial(entt::registry& registry, entt::entity entity, std::shared_ptr<PhysicsMaterial> material) override;

    float GetJointForce(entt::registry& registry, entt::entity entity) override;
    void DestroyJoint(entt::registry& registry, entt::entity entity) override;

    static inline b3BodyId GetB3Body(PhysicsHandle handle) {
        b3BodyId id = b3_nullBodyId;
        std::memcpy(&id, &handle, sizeof(b3BodyId));
        return id;
    }
    static inline PhysicsHandle ToHandle(b3BodyId id) {
        PhysicsHandle handle = NullPhysicsHandle;
        std::memcpy(&handle, &id, sizeof(b3BodyId));
        return handle;
    }
    static inline b3ShapeId GetB3Shape(PhysicsHandle handle) {
        b3ShapeId id = b3_nullShapeId;
        std::memcpy(&id, &handle, sizeof(b3ShapeId));
        return id;
    }
    static inline PhysicsHandle ToHandle(b3ShapeId id) {
        PhysicsHandle handle = NullPhysicsHandle;
        std::memcpy(&handle, &id, sizeof(b3ShapeId));
        return handle;
    }
    static inline b3JointId GetB3Joint(PhysicsHandle handle) {
        b3JointId id = b3_nullJointId;
        std::memcpy(&id, &handle, sizeof(b3JointId));
        return id;
    }
    static inline PhysicsHandle ToHandle(b3JointId id) {
        PhysicsHandle handle = NullPhysicsHandle;
        std::memcpy(&handle, &id, sizeof(b3JointId));
        return handle;
    }

    b3WorldId GetWorld() const {
        return m_worldId;
    }

  private:
    b3WorldId m_worldId;
};

inline b3BodyId GetValidB3Body(const entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<RigidBodyComponent>(entity)) {
#ifndef NDEBUG
        assert(false && "Entity is invalid or missing RigidBodyComponent");
#endif
        return b3_nullBodyId;
    }

    auto& rb = registry.get<RigidBodyComponent>(entity);
    b3BodyId b3Body = Box3DBackend::GetB3Body(rb.RuntimeBody);
    if (!b3Body_IsValid(b3Body)) {
#ifndef NDEBUG
        assert(false && "Box3D body is uninitialized or invalid!");
#endif
        return b3_nullBodyId;
    }

    return b3Body;
}

} // namespace BRITE
