#pragma once
#include <box2d/box2d.h>
#include <entt/entt.hpp>

namespace BRITE {

/// @brief Fired when two physics bodies begin contact during a physics step.
struct PhysicsCollisionEvent {
    entt::entity EntityA{entt::null};
    entt::entity EntityB{entt::null};
};

/// @brief Fired when two physics bodies separate.
struct PhysicsCollisionEndEvent {
    entt::entity EntityA{entt::null};
    entt::entity EntityB{entt::null};
};

/// @brief Fired during a solid impact, containing speed and normal data.
struct PhysicsHitEvent {
    entt::entity EntityA{entt::null};
    entt::entity EntityB{entt::null};
    float ApproachSpeed{0.0f};
    float NormalX{0.0f};
    float NormalY{0.0f};
};

/// @brief Fired when a physics body begins overlapping a sensor (trigger) shape.
struct PhysicsSensorBeginEvent {
    entt::entity SensorEntity{entt::null};
    entt::entity VisitorEntity{entt::null};
};

/// @brief Fired when a physics body stops overlapping a sensor (trigger) shape.
struct PhysicsSensorEndEvent {
    entt::entity SensorEntity{entt::null};
    entt::entity VisitorEntity{entt::null};
};

} // namespace BRITE
