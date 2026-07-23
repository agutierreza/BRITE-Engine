#pragma once
#include <raylib.h>
#include <raymath.h>
#include "Framework/CameraFollowComponent.hpp"

namespace brite {
namespace framework {

// Framework-agnostic camera update function. 
// Can be called by the game's ECS systems to update the main Raylib Camera2D.
inline void UpdateCamera(Camera2D& camera, CameraFollowComponent& follow, Vector2 targetPos, Vector2 targetVelocity, float dt) {
    switch (follow.mode) {
        case CameraMode::HardLock:
            camera.target = targetPos;
            break;
            
        case CameraMode::SmoothLerp:
            // Standard linear interpolation
            camera.target.x += (targetPos.x - camera.target.x) * follow.lerpSpeed * dt;
            camera.target.y += (targetPos.y - camera.target.y) * follow.lerpSpeed * dt;
            break;
            
        case CameraMode::Deadzone: {
            // Check distance from current camera target
            float dx = targetPos.x - camera.target.x;
            float dy = targetPos.y - camera.target.y;
            
            // Push camera if target exceeds deadzone bounds
            if (dx < follow.deadzone.x) {
                camera.target.x = targetPos.x - follow.deadzone.x;
            } else if (dx > follow.deadzone.x + follow.deadzone.width) {
                camera.target.x = targetPos.x - (follow.deadzone.x + follow.deadzone.width);
            }
            
            if (dy < follow.deadzone.y) {
                camera.target.y = targetPos.y - follow.deadzone.y;
            } else if (dy > follow.deadzone.y + follow.deadzone.height) {
                camera.target.y = targetPos.y - (follow.deadzone.y + follow.deadzone.height);
            }
            break;
        }
            
        case CameraMode::LeadVelocity:
            // Calculate desired lead based on velocity
            Vector2 desiredLead = Vector2Scale(Vector2Normalize(targetVelocity), follow.leadDistance);
            if (Vector2Length(targetVelocity) < 0.1f) desiredLead = { 0.0f, 0.0f }; // Reset if not moving
            
            // Smoothly interpolate current lead to desired lead
            follow.currentLead.x += (desiredLead.x - follow.currentLead.x) * follow.leadSpeed * dt;
            follow.currentLead.y += (desiredLead.y - follow.currentLead.y) * follow.leadSpeed * dt;
            
            // Smoothly move camera to target + lead
            Vector2 finalTarget = Vector2Add(targetPos, follow.currentLead);
            camera.target.x += (finalTarget.x - camera.target.x) * follow.lerpSpeed * dt;
            camera.target.y += (finalTarget.y - camera.target.y) * follow.lerpSpeed * dt;
            break;
    }
}

} // namespace framework
} // namespace brite
