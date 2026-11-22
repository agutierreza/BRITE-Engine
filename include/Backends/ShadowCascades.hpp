#pragma once

#include "Math/BriteMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>

// THE CASCADES OF A SUN'S SHADOW (RenderPass.hpp, ShadowSettings), on the CPU: where the camera's view is split, the
// sphere that bounds each slice, the snap that keeps a cascade's map from swimming as the camera moves, and which
// casters can reach a cascade. The
// backend draws with these; a caller and a test can reach them.
namespace BRITE::ShadowCascades {

inline constexpr int MaxCascades = 4;

// The far edge of cascade `index` (0-based) of `count`, over the view from `nearPlane` to `distance`: the practical
// split of Zhang et al. (2006), a blend of the logarithmic split near * (distance / near)^f -- which gives every
// cascade the same texels per unit of depth on screen -- and the even one near + (distance - near) f, where
// f = (index + 1) / count. Lambda 1 is all logarithmic, 0 all even.
inline float PracticalSplit(int index, int count, float nearPlane, float distance, float lambda) {
    const float f = static_cast<float>(index + 1) / static_cast<float>(std::max(count, 1));
    const float logarithmic = nearPlane * std::pow(distance / nearPlane, f);
    const float even = nearPlane + (distance - nearPlane) * f;
    return lambda * logarithmic + (1.0f - lambda) * even;
}

// Every cascade's far edge, nearest first: an explicit split where the caller gave one (`splits[i]` above the edge
// before it and below `distance`), the practical split where not, and the last cascade's always `distance`.
inline std::array<float, MaxCascades> FarEdges(int count, float nearPlane, float distance, float lambda,
                                               const std::array<float, MaxCascades - 1>& splits) {
    count = std::clamp(count, 1, MaxCascades);
    std::array<float, MaxCascades> edges{};
    float previous = nearPlane;
    for (int i = 0; i < count; ++i) {
        float edge = PracticalSplit(i, count, nearPlane, distance, lambda);
        if (i < count - 1 && splits[static_cast<std::size_t>(i)] > previous &&
            splits[static_cast<std::size_t>(i)] < distance)
            edge = splits[static_cast<std::size_t>(i)];
        if (i == count - 1)
            edge = distance;
        edges[static_cast<std::size_t>(i)] = std::max(edge, previous);
        previous = edges[static_cast<std::size_t>(i)];
    }
    return edges;
}

struct Sphere {
    Vector3 Centre;
    float Radius;
};

// The sphere around the slice of a view from `nearEdge` to `farEdge` along `forward`, whose half-width and half-height
// are `halfWidth(d)` = widthSlope * d + widthConstant (and the same for height) at a distance d: tan(half the angle)
// and 0 for a perspective view, 0 and half the view's size for an orthographic one. Centred on the mean of the slice's
// eight corners, out to the farthest of them, the radius rounded UP to a sixteenth of a unit so it does not change
// with the camera's every turn. A sphere's size does not depend on which way the camera looks, so the map fitted to it
// keeps its texel size as the camera turns. `right` and `up` are the camera's unit axes.
inline Sphere SliceSphere(Vector3 eye, Vector3 forward, Vector3 right, Vector3 up, float nearEdge, float farEdge,
                          float widthSlope, float widthConstant, float heightSlope, float heightConstant) {
    Vector3 corners[8];
    int n = 0;
    for (float d : {nearEdge, farEdge}) {
        const float w = widthSlope * d + widthConstant;
        const float h = heightSlope * d + heightConstant;
        for (float sy : {-1.0f, 1.0f})
            for (float sx : {-1.0f, 1.0f})
                corners[n++] = {eye.x + forward.x * d + right.x * sx * w + up.x * sy * h,
                                eye.y + forward.y * d + right.y * sx * w + up.y * sy * h,
                                eye.z + forward.z * d + right.z * sx * w + up.z * sy * h};
    }
    Vector3 centre{0.0f, 0.0f, 0.0f};
    for (const Vector3& c : corners)
        centre = {centre.x + c.x / 8.0f, centre.y + c.y / 8.0f, centre.z + c.z / 8.0f};
    float radius = 0.0f;
    for (const Vector3& c : corners)
        radius = std::max(radius, std::sqrt((c.x - centre.x) * (c.x - centre.x) + (c.y - centre.y) * (c.y - centre.y) +
                                            (c.z - centre.z) * (c.z - centre.z)));
    return {centre, std::ceil(radius * 16.0f) / 16.0f};
}

// A world unit of a cascade whose map is `mapSize` texels across a sphere of `radius`.
inline float TexelSize(float radius, int mapSize) {
    return 2.0f * radius / static_cast<float>(std::max(mapSize, 1));
}

// A coordinate in the light's view moved down to a whole texel: the cascade's box moves only in whole texels, so a
// texel of the map covers the same patch of ground from one frame to the next and the shadow's edge does not crawl.
inline float SnapToTexel(float coordinate, float texel) {
    return std::floor(coordinate / texel) * texel;
}

// Whether a caster's sphere -- centre and radius in the light's view, which looks down -z, so nearer the sun is larger
// z -- can throw shadow into a cascade fitted to the sphere at `cascadeCentre` (the light's view, snapped) of radius
// `cascadeRadius`: it must overlap the cascade's square across the light, and must not lie wholly beyond the cascade's
// far side, where nothing it shades is drawn. Anywhere toward the sun it can still shade the cascade, so it is kept.
inline bool CasterReachesCascade(Vector3 caster, float casterRadius, Vector3 cascadeCentre, float cascadeRadius) {
    const float reach = cascadeRadius + casterRadius;
    return std::fabs(caster.x - cascadeCentre.x) <= reach && std::fabs(caster.y - cascadeCentre.y) <= reach &&
           caster.z + casterRadius >= cascadeCentre.z - cascadeRadius;
}

} // namespace BRITE::ShadowCascades
