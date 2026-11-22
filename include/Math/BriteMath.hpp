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

// How a model draw's colour is combined with what is already behind it.
enum class BlendMode {
    Alpha,    // source * alpha + behind * (1 - alpha): opaque at an alpha of 255
    Additive, // source * alpha + behind: light added to light
};

struct PBRMaterial {
    TextureHandle AlbedoMap = NullTextureHandle;
    // A tangent-space normal map, as glTF defines one: red along the texture's
    // u, green toward the TOP of the texture, blue out of the surface. A mesh
    // needs texture coordinates to carry one; its tangents are generated at load
    // when its file gives none. Replaces the model's own map for this draw.
    TextureHandle NormalMap = NullTextureHandle;
    // Metalness, roughness and occlusion, each read from the channel glTF packs
    // it in -- roughness green, metalness blue, occlusion red -- so one glTF
    // metal-roughness texture serves as both RoughnessMap and MetallicMap, and a
    // greyscale map serves as any. A map multiplies its factor (Metallic,
    // Roughness), and a factor under a map is 1 unless a loaded model's file
    // says otherwise. Occlusion darkens only the ambient light. Each replaces the
    // model's own map for this draw.
    TextureHandle RoughnessMap = NullTextureHandle;
    TextureHandle MetallicMap = NullTextureHandle;
    TextureHandle EmissionMap = NullTextureHandle;
    TextureHandle AOMap = NullTextureHandle;
    Color AlbedoTint = White;
    // Scalar surface parameters, used where the corresponding map is absent.
    // Roughness 1 is fully rough, glTF's default; anything glossier is the
    // caller's choice. Metallic 0 is NOT glTF's default (glTF says 1, meaning
    // "as the metallic map says"): with no map, a surface is a dielectric
    // unless the caller asks for metal. A loaded model whose file WRITES a
    // metallicFactor or roughnessFactor uses its own; these are the fallback
    // for a file that writes neither, and for every mesh built in code.
    float Metallic = 0.0f;
    float Roughness = 1.0f;
    // Draw the albedo as authored -- tint times vertex colour times albedo
    // texture -- with no lights, no ambient, no tone mapping and no fog: for a
    // sky, a painted backdrop or a light-emitting sign. The scalars and every
    // map but the albedo and the emission are ignored. Under an HDR pass it
    // still shows as authored, and adds its emission (below). A model draw
    // only; sprites and primitives are unlit already.
    bool Unlit = false;
    // Alpha as a cut-out: a fragment whose alpha -- tint times vertex colour
    // times albedo texture -- falls below AlphaCutoff is not drawn, and every
    // other is drawn solid. For leaves, fences and a backdrop with a transparent
    // sky. Without it the texture's alpha is ignored, as glTF's OPAQUE says. A
    // loaded model's own MASK materials are cut out whatever this says; set here,
    // it cuts out every mesh of the draw at this cutoff.
    bool AlphaMask = false;
    float AlphaCutoff = 0.5f;
    // Both faces drawn, each lit from its own side: a single sheet seen from
    // behind. A loaded model's own double-sided materials are drawn so whatever
    // this says; set here, it applies to every mesh of the draw.
    bool DoubleSided = false;
    // Light the surface gives off, added to whatever light falls on it: a sign,
    // a display, a lamp. An sRGB colour, as the tint is, times EmissionMap when
    // there is one, times EmissionStrength -- so an emission map shows as it is
    // under a white Emission. Black is none. Set, it replaces a loaded model's
    // own emission for this draw.
    //
    // BRIGHTER THAN WHITE needs a pass whose PostSettings::Hdr is on. There the
    // light is kept as it is, so a strength above 1 is light beyond anything a
    // colour can author -- a sun, a lamp, a spark -- and it is what the pass's
    // bloom spreads. An emission of strength s and colour c adds s * c^2.2 of
    // linear light, where an authored white is about 7.24 (ToneCurve.hpp): a
    // white emission needs a strength of several to show past white, and tens
    // to glow like a sun. On an UNLIT surface under an HDR pass the emission is
    // added too, on top of the authored colour, so an unlit game can make one
    // of its own shapes glow without lighting anything. Without HDR every
    // channel stops at white, and an unlit surface ignores its emission and is
    // drawn as authored, as it always was.
    Color Emission = Black;
    float EmissionStrength = 1.0f;
    // How the draw's colour meets what is behind it (BlendMode, above). Alpha
    // is the ordinary blend, and opaque when the colour's alpha is 255.
    // Additive adds its colour, times its alpha, to what is behind it -- for a
    // glow, a spark, a flash or a beam -- and is always translucent: drawn
    // after every opaque draw, sorted with the translucent ones, writing no
    // depth (RenderPass says the order). Under an HDR pass it adds linear
    // light, so overlapping glows build past white and bloom; without HDR they
    // stop at white.
    BlendMode Blend = BlendMode::Alpha;
    // Whether the draw writes its depth. Off, it is still depth-TESTED -- hidden
    // by anything nearer already drawn -- but leaves no depth of its own, so
    // whatever is drawn after it is tested against what lay behind it: for a
    // see-through layer over a surface -- a decal, a tint, a glow -- that must
    // not hide what is drawn later, and for several such layers at one height,
    // which blend over each other instead of fighting for the same depth. A
    // see-through layer's colour then depends on the order the layers are drawn
    // in. On by default: an opaque surface writes its depth.
    //
    // A TRANSLUCENT draw (RenderPass says what that is) writes no depth whatever this says, and is drawn after every
    // opaque draw of its pass. With this on it is sorted back to front with the other translucent draws; with this
    // off it is a layer, drawn before the sorted ones in the order submitted, as above. On an opaque draw, off still
    // means depth-tested but not depth-written, and the draw keeps its place among the opaque ones.
    bool DepthWrite = true;
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
