#pragma once

#include "RenderPass.hpp"

#include <algorithm>
#include <array>
#include <cmath>

// MOTION BLUR'S GEOMETRY (RenderPass.hpp, MotionBlurSettings), on the CPU. The backend's shaders do the same per pixel
// with the matrices it draws with; these are the ones a caller and a test can reach, and the backend decides a cut
// with them.
//
// A camera projects as the backend's 3D draws project it: it looks from `position` at `target`, with `up` turned
// square to the view; a perspective camera's fovy is its vertical angle in degrees, an orthographic camera's the
// height of its view in world units. Normalised device coordinates run -1..1 across the target, +x right, +y up.
namespace BRITE::MotionBlur {

// The settings' holds (MotionBlurSettings).
inline constexpr float LeastMaxLength = 1.0f;
inline constexpr float MostMaxLength = 64.0f;
inline constexpr int LeastSamples = 2;
inline constexpr int MostSamples = 32;

// The camera's frame: the way it looks, its right and its up, each of unit length; false when it looks nowhere.
struct Frame {
    Vector3 Forward;
    Vector3 Right;
    Vector3 Up;
};
inline bool CameraFrame(const Camera3D& camera, Frame& frame) {
    const Vector3 look = {camera.target.x - camera.position.x, camera.target.y - camera.position.y,
                          camera.target.z - camera.position.z};
    if (Math::Length(look) <= 0.0f)
        return false;
    frame.Forward = Math::Normalize(look);
    const Vector3 right = Math::CrossProduct(frame.Forward, camera.up);
    if (Math::Length(right) <= 0.0f)
        return false;
    frame.Right = Math::Normalize(right);
    frame.Up = Math::CrossProduct(frame.Right, frame.Forward);
    return true;
}

// Half the view's height at a distance `along` the view: tan(fovy / 2) x along for a perspective camera, fovy / 2 for
// an orthographic one.
inline float HalfHeight(const Camera3D& camera, float along) {
    return camera.projection == Math::CameraProjection::Perspective ? std::tan(camera.fovy * Deg2Rad * 0.5f) * along
                                                                    : camera.fovy * 0.5f;
}

// Where `world` lies on the target, in normalised device coordinates, under `camera` on a target `aspect` (width over
// height) wide: false when the camera looks nowhere, or when a perspective camera has the point at or behind it.
inline bool Project(const Camera3D& camera, float aspect, Vector3 world, Vector2& ndc) {
    Frame frame;
    if (!CameraFrame(camera, frame) || !(aspect > 0.0f))
        return false;
    const Vector3 from = {world.x - camera.position.x, world.y - camera.position.y, world.z - camera.position.z};
    const float along = Math::DotProduct(from, frame.Forward);
    if (camera.projection == Math::CameraProjection::Perspective && !(along > 0.0f))
        return false;
    const float halfHeight = HalfHeight(camera, along);
    if (!(halfHeight > 0.0f))
        return false;
    ndc.x = Math::DotProduct(from, frame.Right) / (halfHeight * aspect);
    ndc.y = Math::DotProduct(from, frame.Up) / halfHeight;
    return std::isfinite(ndc.x) && std::isfinite(ndc.y);
}

// The world point drawn at `ndc` at a distance `along` the camera's view: Project's inverse.
inline Vector3 Unproject(const Camera3D& camera, float aspect, Vector2 ndc, float along) {
    Frame frame;
    if (!CameraFrame(camera, frame))
        return camera.position;
    const float halfHeight = HalfHeight(camera, along);
    const float x = ndc.x * halfHeight * aspect, y = ndc.y * halfHeight;
    return {camera.position.x + frame.Forward.x * along + frame.Right.x * x + frame.Up.x * y,
            camera.position.y + frame.Forward.y * along + frame.Right.y * x + frame.Up.y * y,
            camera.position.z + frame.Forward.z * along + frame.Right.z * x + frame.Up.z * y};
}

// A point's motion over the frame, in pixels of a width x height target: from where it was drawn to where it is,
// (now - before) x size / 2, +y up.
inline Vector2 PixelMotion(Vector2 ndcBefore, Vector2 ndcNow, int width, int height) {
    return {(ndcNow.x - ndcBefore.x) * 0.5f * static_cast<float>(width),
            (ndcNow.y - ndcBefore.y) * 0.5f * static_cast<float>(height)};
}

// The streak a pixel is smeared along: its motion times the shutter (held to 0..1), shortened along its own direction
// to at most the maximum length (held to LeastMaxLength..MostMaxLength).
inline Vector2 Streak(Vector2 motion, float shutter, float maxLength) {
    const float open = std::clamp(shutter, 0.0f, 1.0f);
    const float most = std::clamp(maxLength, LeastMaxLength, MostMaxLength);
    Vector2 streak = {motion.x * open, motion.y * open};
    const float length = std::sqrt(streak.x * streak.x + streak.y * streak.y);
    if (length > most)
        streak = {streak.x * most / length, streak.y * most / length};
    return streak;
}

// Whether anything moved between two frames: a camera or a placement that differs in any field. Exact: a caller that
// keeps last frame's state hands back the very values, and a pass where nothing moved is drawn as with motion blur off.
inline bool Moved(const Camera3D& before, const Camera3D& now) {
    auto same = [](Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    return !same(before.position, now.position) || !same(before.target, now.target) || !same(before.up, now.up) ||
           before.fovy != now.fovy || before.projection != now.projection;
}
inline bool Moved(const Placement& before, const Placement& now) {
    auto same = [](Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    return !same(before.Position, now.Position) || !same(before.Scale, now.Scale) ||
           before.Rotation.x != now.Rotation.x || before.Rotation.y != now.Rotation.y ||
           before.Rotation.z != now.Rotation.z || before.Rotation.w != now.Rotation.w;
}

// The points a cut is judged at: the middle of the view and half way to each corner, at the distance of the camera's
// target -- what it looks at.
inline constexpr std::array<Vector2, 5> CutPoints = {
    {{0.0f, 0.0f}, {0.5f, 0.5f}, {-0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}}};

// The camera's motion over the frame at those points, the largest, in pixels; negative when the frame is a cut whatever
// its length: a point drawn now lies behind last frame's camera, a camera looks nowhere, or the projection changed.
inline float CameraMotion(const Camera3D& before, const Camera3D& now, int width, int height) {
    if (before.projection != now.projection || width <= 0 || height <= 0)
        return -1.0f;
    Frame frame;
    if (!CameraFrame(now, frame))
        return -1.0f;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Vector3 look = {now.target.x - now.position.x, now.target.y - now.position.y, now.target.z - now.position.z};
    const float along = Math::DotProduct(look, frame.Forward);
    float most = 0.0f;
    for (const Vector2& at : CutPoints) {
        const Vector3 world = Unproject(now, aspect, at, along);
        Vector2 was;
        if (!Project(before, aspect, world, was))
            return -1.0f;
        const Vector2 motion = PixelMotion(was, at, width, height);
        most = std::max(most, std::sqrt(motion.x * motion.x + motion.y * motion.y));
    }
    return most;
}

// Whether a pass's frame is a cut (MotionBlurSettings): the caller says so, or its camera jumped. A pass with no
// previous camera is not a cut -- it has no camera motion to judge.
inline bool IsCut(const MotionBlurSettings& settings, const Camera3D& now, int width, int height) {
    if (settings.Cut)
        return true;
    if (!settings.PreviousCamera)
        return false;
    const float motion = CameraMotion(*settings.PreviousCamera, now, width, height);
    return motion < 0.0f || motion > settings.CutLength;
}

} // namespace BRITE::MotionBlur
