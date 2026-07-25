#pragma once

#include <entt/entt.hpp>
#include <box2d/box2d.h>
namespace BRITE {
    class PhysicsSystem {
    public:
        static void PreStep(entt::registry& registry, b2WorldId worldId);
        static void PostStep(entt::registry& registry);
    };
} // namespace BRITE
