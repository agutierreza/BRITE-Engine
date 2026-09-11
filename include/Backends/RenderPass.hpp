#pragma once

#include "Math/BriteMath.hpp"
#include <cstddef>
#include <functional>
#include <vector>

namespace BRITE {

enum class Primitive3DType { Cube, CubeWires, Sphere, SphereWires, Grid };

struct Primitive3DDrawCommand {
    Primitive3DType Type;
    Vector3 Position;
    Quaternion Rotation;
    Vector3 Scale;
    Vector3 Size;
    Color Tint;
};

struct ModelDrawCommand {
    ModelHandle Model;
    PBRMaterial Material;
    Vector3 Position;
    Quaternion Rotation;
    Vector3 Scale;
};

struct SpriteDrawCommand {
    PBRMaterial Material;
    Rectangle SourceRect;
    Rectangle DestRect;
    Vector2 Origin;
    float RotationDeg;
};

struct LineDrawCommand {
    Vector2 Start;
    Vector2 End;
    Color Tint;
};

// LineDrawCommand one dimension up: endpoints in world space, drawn under the
// pass's 3D camera and ignored when it has none. No thickness and no depth-test
// control.
struct Line3DDrawCommand {
    Vector3 Start;
    Vector3 End;
    Color Tint;
};

struct RectDrawCommand {
    Rectangle DestRect;
    Vector2 Origin;
    float RotationDeg;
    Color Tint;
    bool IsFilled; // true for DrawRectanglePro, false for DrawRectangleLinesEx (thickness could be added later)
};

enum class LightType { Directional, Point };

// One analytic light for the model path. Primitives are unlit and ignore it.
struct Light {
    LightType Type = LightType::Directional;
    // Point lights: where the light is. Ignored by a directional light.
    Vector3 Position = {0.0f, 0.0f, 0.0f};
    // Directional lights: the direction the light TRAVELS -- straight down for a
    // sun at the zenith. Ignored by a point light.
    Vector3 Direction = {0.0f, -1.0f, 0.0f};
    Color Tint = White;
    float Intensity = 1.0f;
};

// The most lights a backend honours per pass; entries beyond it are ignored.
inline constexpr std::size_t MaxLightsPerPass = 4;

struct RenderPass {
    TextureHandle TargetFramebuffer = NullTextureHandle; // NullTextureHandle means default screen
    ShaderHandle Shader = NullShaderHandle;              // NullShaderHandle means no post-processing shader
    Color ClearColor = Black;
    bool ShouldClear = true;
    Camera2D* Camera = nullptr;      // Optional 2D Camera
    Camera3D* Camera3DPtr = nullptr; // Optional 3D Camera

    EnvironmentMap Environment = {};
    bool DrawSkybox = true;

    // Lighting for ModelCommands. A model under no lights and zero ambient draws
    // black, which is the honest result of asking for a lit surface in the dark:
    // set at least one of the two. With an Environment map the ambient comes from
    // the map instead and these two ambient fields are unused.
    std::vector<Light> Lights;
    Color AmbientColor = White;
    float AmbientIntensity = 0.0f;

    std::vector<ModelDrawCommand> ModelCommands;
    std::vector<SpriteDrawCommand> SpriteCommands;
    std::vector<LineDrawCommand> LineCommands;
    std::vector<RectDrawCommand> RectCommands;
    std::vector<Primitive3DDrawCommand> Primitive3DCommands;
    std::vector<Line3DDrawCommand> Line3DCommands;

    // Optional callbacks executed immediately after ClearBackground, before anything else.
    // Useful for full-screen shaders like TorusStarsRenderSystem.
    std::vector<std::function<void()>> BackgroundDrawCallbacks;

    // Optional callbacks executed inside the 2D Camera mode (after BeginMode2D).
    // Useful for complex custom drawing (like shaders) that are not yet data-driven.
    std::vector<std::function<void()>> WorldDrawCallbacks;

    // Optional callbacks executed at the very end of the render pass,
    // after EndMode2D(), useful for immediate mode UI (ImGui) or direct debug text drawing.
    std::vector<std::function<void()>> UIDrawCallbacks;
};

} // namespace BRITE
