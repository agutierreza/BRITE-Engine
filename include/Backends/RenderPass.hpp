#pragma once

#include "Math/BriteMath.hpp"
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

struct RectDrawCommand {
    Rectangle DestRect;
    Vector2 Origin;
    float RotationDeg;
    Color Tint;
    bool IsFilled; // true for DrawRectanglePro, false for DrawRectangleLinesEx (thickness could be added later)
};

struct RenderPass {
    TextureHandle TargetFramebuffer = NullTextureHandle; // NullTextureHandle means default screen
    ShaderHandle Shader = NullShaderHandle;              // NullShaderHandle means no post-processing shader
    Color ClearColor = Black;
    bool ShouldClear = true;
    Camera2D* Camera = nullptr;      // Optional 2D Camera
    Camera3D* Camera3DPtr = nullptr; // Optional 3D Camera

    EnvironmentMap Environment = {};
    bool DrawSkybox = true;

    std::vector<ModelDrawCommand> ModelCommands;
    std::vector<SpriteDrawCommand> SpriteCommands;
    std::vector<LineDrawCommand> LineCommands;
    std::vector<RectDrawCommand> RectCommands;
    std::vector<Primitive3DDrawCommand> Primitive3DCommands;

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
