#pragma once

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace BRITE {
namespace Math {

// TODO: Next time we need to add anything to this math library, we had better just include GLM!

struct Vector2 {
    float x;
    float y;

    Vector2& operator+=(const Vector2& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    Vector2& operator-=(const Vector2& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    Vector2& operator*=(float rhs) {
        x *= rhs;
        y *= rhs;
        return *this;
    }
    Vector2& operator/=(float rhs) {
        x /= rhs;
        y /= rhs;
        return *this;
    }
    Vector2 operator-() const {
        return {-x, -y};
    }
};

inline Vector2 operator+(const Vector2& a, const Vector2& b) {
    return {a.x + b.x, a.y + b.y};
}
inline Vector2 operator-(const Vector2& a, const Vector2& b) {
    return {a.x - b.x, a.y - b.y};
}
inline Vector2 operator*(const Vector2& a, float b) {
    return {a.x * b, a.y * b};
}
inline Vector2 operator*(float a, const Vector2& b) {
    return {a * b.x, a * b.y};
}
inline Vector2 operator/(const Vector2& a, float b) {
    return {a.x / b, a.y / b};
}

struct Vector3 {
    float x;
    float y;
    float z;

    Vector3& operator+=(const Vector3& rhs) {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    Vector3& operator-=(const Vector3& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }
    Vector3& operator*=(float rhs) {
        x *= rhs;
        y *= rhs;
        z *= rhs;
        return *this;
    }
    Vector3& operator/=(float rhs) {
        x /= rhs;
        y /= rhs;
        z /= rhs;
        return *this;
    }
    Vector3 operator-() const {
        return {-x, -y, -z};
    }
};

inline Vector3 operator+(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vector3 operator-(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vector3 operator*(const Vector3& a, float b) {
    return {a.x * b, a.y * b, a.z * b};
}
inline Vector3 operator*(float a, const Vector3& b) {
    return {a * b.x, a * b.y, a * b.z};
}
inline Vector3 operator/(const Vector3& a, float b) {
    return {a.x / b, a.y / b, a.z / b};
}

inline float DotProduct(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vector3 CrossProduct(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Length(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vector3 Normalize(const Vector3& v) {
    float len = Length(v);
    if (len > 0.0f) {
        return v / len;
    }
    return v;
}

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

enum class CameraProjection { Perspective, Orthographic };

struct Camera3D {
    Vector3 position;
    Vector3 target;
    Vector3 up;
    float fovy;
    CameraProjection projection;
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
using Camera3D = Math::Camera3D;
using CameraProjection = Math::CameraProjection;
using Math::Black;
using Math::White;

// A generic texture handle
using TextureHandle = uint64_t;
constexpr TextureHandle NullTextureHandle = 0;

// A generic shader handle
using ShaderHandle = uint64_t;
constexpr ShaderHandle NullShaderHandle = 0;

// A generic 3D model handle
using ModelHandle = uint64_t;
constexpr ModelHandle NullModelHandle = 0;

struct PBRMaterial {
    TextureHandle AlbedoMap = NullTextureHandle;
    TextureHandle NormalMap = NullTextureHandle;
    TextureHandle RoughnessMap = NullTextureHandle;
    TextureHandle MetallicMap = NullTextureHandle;
    TextureHandle EmissionMap = NullTextureHandle;
    TextureHandle AOMap = NullTextureHandle;
    Color AlbedoTint = White;
};

struct EnvironmentMap {
    TextureHandle Cubemap = NullTextureHandle;
    TextureHandle IrradianceMap = NullTextureHandle;
    TextureHandle PrefilterMap = NullTextureHandle;
    TextureHandle BRDFLUT = NullTextureHandle;
};

constexpr float Pi = 3.14159265358979323846f;
constexpr float Deg2Rad = Pi / 180.0f;
constexpr float Rad2Deg = 180.0f / Pi;

} // namespace BRITE
