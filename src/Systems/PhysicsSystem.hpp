#pragma once

#include <entt/entt.hpp>
#include <box2d/box2d.h>
#include <cassert>
#include <ECS/Components.hpp>

namespace BRITE {

    inline b2BodyId GetValidBody(const entt::registry& registry, entt::entity entity) {
        if (!registry.valid(entity) || !registry.all_of<RigidBodyComponent>(entity)) {
#ifndef NDEBUG
            assert(false && "Entity is invalid or missing RigidBodyComponent");
#endif
            return b2_nullBodyId;
        }
        
        auto& rb = registry.get<RigidBodyComponent>(entity);
        if (!b2Body_IsValid(rb.RuntimeBody)) {
#ifndef NDEBUG
            assert(false && "Box2D body is uninitialized or invalid!");
#endif
            return b2_nullBodyId;
        }
        
        return rb.RuntimeBody;
    }

    class PhysicsSystem {
    public:
        static void PreStep(entt::registry& registry, b2WorldId worldId);
        static void PostStep(entt::registry& registry);
    };
} // namespace BRITE
