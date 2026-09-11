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

inline Quaternion QuaternionFromLookRotation(const Vector3& forward, const Vector3& up) {
    Vector3 f = Normalize(forward);
    // If forward is zero (or very close), return identity
    if (Length(f) < 0.0001f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    Vector3 r = Normalize(CrossProduct(up, f));
    // If forward and up are parallel, right will be zero. Handle it by picking an arbitrary right vector.
    if (Length(r) < 0.0001f) {
        r = Normalize(CrossProduct({1.0f, 0.0f, 0.0f}, f));
        if (Length(r) < 0.0001f) {
            r = Normalize(CrossProduct({0.0f, 1.0f, 0.0f}, f));
        }
    }
    Vector3 u = CrossProduct(f, r);

    float m00 = r.x, m01 = u.x, m02 = f.x;
    float m10 = r.y, m11 = u.y, m12 = f.y;
    float m20 = r.z, m21 = u.z, m22 = f.z;

    float trace = m00 + m11 + m22;
    Quaternion q;
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if ((m00 >= m11) && (m00 >= m22)) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 >= m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }

    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 0.0f) {
        q.x /= len;
        q.y /= len;
        q.z /= len;
        q.w /= len;
    }

    return q;
}

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
    // Scalar surface parameters, used where the corresponding map is absent.
    // Roughness 1 is fully rough, glTF's default; anything glossier is the
    // caller's choice. Metallic 0 is NOT glTF's default (glTF says 1, meaning
    // "as the metallic map says"): with no map, a surface is a dielectric
    // unless the caller asks for metal.
    float Metallic = 0.0f;
    float Roughness = 1.0f;
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
