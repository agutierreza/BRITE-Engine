#pragma once

#include "Math/BriteMath.hpp"
#include <functional>
#include <vector>

namespace BRITE {

struct SpriteDrawCommand {
    TextureHandle Texture;
    Rectangle SourceRect;
    Rectangle DestRect;
    Vector2 Origin;
    float RotationDeg;
    Color Tint;
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
    Color ClearColor = Black;
    bool ShouldClear = true;
    Camera2D* Camera = nullptr; // Optional 2D Camera

    std::vector<SpriteDrawCommand> SpriteCommands;
    std::vector<LineDrawCommand> LineCommands;
    std::vector<RectDrawCommand> RectCommands;

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
