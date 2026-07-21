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

struct PhysicsBodyComponent {
    b2Body* Body = nullptr;
};

} // namespace BRITE
