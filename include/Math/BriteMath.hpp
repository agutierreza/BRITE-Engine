#pragma once

#include <cstdint>

namespace BRITE {
namespace Math {

struct Vector2 {
    float x;
    float y;
};

struct Vector3 {
    float x;
    float y;
    float z;
};

struct Quaternion {
    float x;
    float y;
    float z;
    float w;
};

struct Rectangle {
    float x;
    float y;
    float width;
    float height;
};

struct Color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

struct Camera2D {
    Vector2 offset;
    Vector2 target;
    float rotation;
    float zoom;
};

// TODO: Once we implement a rich translation layer for rendering,
// we should re-introduce default color constants (e.g. WHITE) here.
// Not possible right now because of collisions with clients using Raylib directly...
constexpr Color White = {255, 255, 255, 255};
constexpr Color Black = {0, 0, 0, 255};

} // namespace Math

using Vector2 = Math::Vector2;
using Vector3 = Math::Vector3;
using Quaternion = Math::Quaternion;
using Rectangle = Math::Rectangle;
using Color = Math::Color;
using Camera2D = Math::Camera2D;
using Math::Black;
using Math::White;

// A generic texture handle
using TextureHandle = uint64_t;
constexpr TextureHandle NullTextureHandle = 0;

constexpr float Pi = 3.14159265358979323846f;
constexpr float Deg2Rad = Pi / 180.0f;
constexpr float Rad2Deg = 180.0f / Pi;

} // namespace BRITE
