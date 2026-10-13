#pragma once
#include <entt/entt.hpp>
#include <raylib.h>

namespace brite {
namespace framework {

enum class CameraMode { HardLock, SmoothLerp, Deadzone, LeadVelocity };

struct CameraFollowComponent {
    entt::entity targetEntity = entt::null;
    CameraMode mode = CameraMode::SmoothLerp;

    // Smooth Lerp params
    float lerpSpeed = 5.0f;

    // Deadzone params (relative to the screen center)
    // Positive width/height expand the box from the center
    Rectangle deadzone = {-50.0f, -50.0f, 100.0f, 100.0f};

    // Lead Velocity params
    float leadDistance = 100.0f;
    float leadSpeed = 2.0f;
    Vector2 currentLead = {0.0f, 0.0f};
};

} // namespace framework
} // namespace brite
