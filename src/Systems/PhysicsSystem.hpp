#pragma once

#include <ECS/Components.hpp>
#include <box2d/box2d.h>
#include <cassert>
#include <cstring>
#include <entt/entt.hpp>

namespace BRITE {

class PhysicsSystem {
  public:
    static inline b2BodyId GetB2Body(PhysicsHandle handle) {
        b2BodyId id = b2_nullBodyId;
        std::memcpy(&id, &handle, sizeof(b2BodyId));
        return id;
    }
    static inline PhysicsHandle ToHandle(b2BodyId id) {
        PhysicsHandle handle = NullPhysicsHandle;
        std::memcpy(&handle, &id, sizeof(b2BodyId));
        return handle;
    }
    static inline b2ShapeId GetB2Shape(PhysicsHandle handle) {
        b2ShapeId id = b2_nullShapeId;
        std::memcpy(&id, &handle, sizeof(b2ShapeId));
        return id;
    }
    static inline PhysicsHandle ToHandle(b2ShapeId id) {
        PhysicsHandle handle = NullPhysicsHandle;
        std::memcpy(&handle, &id, sizeof(b2ShapeId));
        return handle;
    }
    static inline b2JointId GetB2Joint(PhysicsHandle handle) {
        b2JointId id = b2_nullJointId;
        std::memcpy(&id, &handle, sizeof(b2JointId));
        return id;
    }
    static inline PhysicsHandle ToHandle(b2JointId id) {
        PhysicsHandle handle = NullPhysicsHandle;
        std::memcpy(&handle, &id, sizeof(b2JointId));
        return handle;
    }

    static void PreStep(entt::registry& registry, b2WorldId worldId);
    static void PostStep(entt::registry& registry, b2WorldId worldId);
    static void SetMaterial(entt::registry& registry, entt::entity entity, std::shared_ptr<PhysicsMaterial> material);

    static float GetJointForce(entt::registry& registry, entt::entity entity);
    static void DestroyJoint(entt::registry& registry, entt::entity entity);
};

inline b2BodyId GetValidBody(const entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<RigidBodyComponent>(entity)) {
#ifndef NDEBUG
        assert(false && "Entity is invalid or missing RigidBodyComponent");
#endif
        return b2_nullBodyId;
    }

    auto& rb = registry.get<RigidBodyComponent>(entity);
    b2BodyId b2Body = PhysicsSystem::GetB2Body(rb.RuntimeBody);
    if (!b2Body_IsValid(b2Body)) {
#ifndef NDEBUG
        assert(false && "Box2D body is uninitialized or invalid!");
#endif
        return b2_nullBodyId;
    }

    return b2Body;
}

} // namespace BRITE
