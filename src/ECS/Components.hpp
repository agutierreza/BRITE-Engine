#pragma once

#include <raylib.h>
#include <box2d/box2d.h>

namespace BRITE {

struct TransformComponent {
    Vector2 Position = { 0.0f, 0.0f };
    float Rotation = 0.0f; // in degrees
    Vector2 Scale = { 1.0f, 1.0f };
};

struct SpriteComponent {
    Texture2D Texture;
    Color Tint = WHITE;
    Rectangle SourceRect;
    Rectangle DestRect;
    Vector2 Origin = { 0.0f, 0.0f }; // for rotation/scaling around a point
};

struct RigidBodyComponent {
    enum class BodyType { Static, Kinematic, Dynamic };
    BodyType Type = BodyType::Dynamic;
    bool FixedRotation = false;
    float GravityScale = 1.0f;

    // Internal handle managed by PhysicsSystem
    b2BodyId RuntimeBody = b2_nullBodyId;
};

struct BoxColliderComponent {
    Vector2 Size = { 32.0f, 32.0f }; // In pixels
    Vector2 Offset = { 0.0f, 0.0f };
    float Density = 1.0f;
    float Friction = 0.3f;
    float Restitution = 0.0f;

    // Internal handle
    b2ShapeId RuntimeShape = b2_nullShapeId;
};

struct CircleColliderComponent {
    float Radius = 16.0f; // In pixels
    Vector2 Offset = { 0.0f, 0.0f };
    float Density = 1.0f;
    float Friction = 0.3f;
    float Restitution = 0.0f;

    // Internal handle
    b2ShapeId RuntimeShape = b2_nullShapeId;
};

} // namespace BRITE
