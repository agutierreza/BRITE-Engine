#pragma once

#include "PhysicsMaterial.hpp"
#include <cstdint>
#include <entt/entt.hpp>
#include <memory>
#include <raylib.h>
#include <raymath.h>

namespace BRITE {

using PhysicsHandle = uint64_t;
constexpr PhysicsHandle NullPhysicsHandle = 0;

struct TransformComponent {
    Vector3 Position = {0.0f, 0.0f, 0.0f};
    Quaternion Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    Vector3 Scale = {1.0f, 1.0f, 1.0f};

    TransformComponent() = default;

    TransformComponent(Vector3 pos, Quaternion rot = {0.0f, 0.0f, 0.0f, 1.0f}, Vector3 scale = {1.0f, 1.0f, 1.0f})
        : Position(pos), Rotation(rot), Scale(scale) {}

    // Backward compatibility constructor for 2D initialization
    TransformComponent(Vector2 pos, float rotDeg = 0.0f, Vector2 scale = {1.0f, 1.0f})
        : Position{pos.x, pos.y, 0.0f}, Scale{scale.x, scale.y, 1.0f} {
        Rotation = QuaternionFromAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, rotDeg * DEG2RAD);
    }
};

struct SpriteComponent {
    Texture2D Texture;
    Color Tint = WHITE;
    Rectangle SourceRect;
    Rectangle DestRect;
    Vector2 Origin = {0.0f, 0.0f}; // for rotation/scaling around a point
};

struct RigidBodyComponent {
    enum class BodyType { Static, Kinematic, Dynamic };
    BodyType Type = BodyType::Dynamic;
    bool FixedRotation = false;
    float GravityScale = 1.0f;

    // Internal handle managed by PhysicsSystem
    PhysicsHandle RuntimeBody = NullPhysicsHandle;
};

struct BoxColliderComponent {
    Vector2 Size = {32.0f, 32.0f}; // In pixels
    Vector2 Offset = {0.0f, 0.0f};

    std::shared_ptr<PhysicsMaterial> Material = std::make_shared<PhysicsMaterial>();
    bool IsSensor = false;

    // Internal handle
    PhysicsHandle RuntimeShape = NullPhysicsHandle;
};

struct CircleColliderComponent {
    float Radius = 16.0f; // In pixels
    Vector2 Offset = {0.0f, 0.0f};

    std::shared_ptr<PhysicsMaterial> Material = std::make_shared<PhysicsMaterial>();
    bool IsSensor = false;

    // Internal handle
    PhysicsHandle RuntimeShape = NullPhysicsHandle;
};

struct DistanceJointComponent {
    entt::entity TargetEntity{entt::null}; // The other entity to connect to
    bool CollideConnected = false;
    float BreakForce = 0.0f; // The force that instantly snaps the joint. 0.0f means unbreakable.

    float Length = 32.0f; // In pixels
    bool EnableSpring = false;
    float Hertz = 1.0f;
    float DampingRatio = 0.5f;

    // Internal Box2D Handle
    PhysicsHandle RuntimeJoint = NullPhysicsHandle;
};

struct RevoluteJointComponent {
    entt::entity TargetEntity{entt::null}; // The other entity to connect to
    bool CollideConnected = false;
    float BreakForce = 0.0f; // The force that instantly snaps the joint. 0.0f means unbreakable.

    Vector2 LocalAnchorA = {0.0f, 0.0f}; // In pixels
    Vector2 LocalAnchorB = {0.0f, 0.0f}; // In pixels
    float ReferenceAngle = 0.0f;         // In degrees
    bool EnableLimit = false;
    float LowerAngle = 0.0f; // In degrees
    float UpperAngle = 0.0f; // In degrees

    // Internal Box2D Handle
    PhysicsHandle RuntimeJoint = NullPhysicsHandle;
};

struct BoxCollider3DComponent {
    Vector3 Size = {32.0f, 32.0f, 32.0f}; // In pixels
    Vector3 Offset = {0.0f, 0.0f, 0.0f};

    std::shared_ptr<PhysicsMaterial> Material = std::make_shared<PhysicsMaterial>();
    bool IsSensor = false;

    // Internal handle
    PhysicsHandle RuntimeShape = NullPhysicsHandle;
};

struct SphereColliderComponent {
    float Radius = 16.0f; // In pixels
    Vector3 Offset = {0.0f, 0.0f, 0.0f};

    std::shared_ptr<PhysicsMaterial> Material = std::make_shared<PhysicsMaterial>();
    bool IsSensor = false;

    // Internal handle
    PhysicsHandle RuntimeShape = NullPhysicsHandle;
};

struct DistanceJoint3DComponent {
    entt::entity TargetEntity{entt::null}; // The other entity to connect to
    bool CollideConnected = false;
    float BreakForce = 0.0f; // The force that instantly snaps the joint. 0.0f means unbreakable.

    float Length = 32.0f; // In pixels
    bool EnableSpring = false;
    float Hertz = 1.0f;
    float DampingRatio = 0.5f;

    // Internal Box3D Handle
    PhysicsHandle RuntimeJoint = NullPhysicsHandle;
};

struct RevoluteJoint3DComponent {
    entt::entity TargetEntity{entt::null}; // The other entity to connect to
    bool CollideConnected = false;
    float BreakForce = 0.0f; // The force that instantly snaps the joint. 0.0f means unbreakable.

    Vector3 LocalAnchorA = {0.0f, 0.0f, 0.0f}; // In pixels
    Vector3 LocalAnchorB = {0.0f, 0.0f, 0.0f}; // In pixels
    Vector3 Axis = {0.0f, 0.0f, 1.0f};         // The rotation axis

    float ReferenceAngle = 0.0f; // In degrees
    bool EnableLimit = false;
    float LowerAngle = 0.0f; // In degrees
    float UpperAngle = 0.0f; // In degrees

    // Internal Box3D Handle
    PhysicsHandle RuntimeJoint = NullPhysicsHandle;
};

struct AoIComponent {
    float RadiusPixels = 1000.0f; // Configurable Area of Interest radius in pixels
    bool Active = true;
};

struct InAoITag {}; // Empty tag attached to entities currently inside the simulation AoI

} // namespace BRITE
