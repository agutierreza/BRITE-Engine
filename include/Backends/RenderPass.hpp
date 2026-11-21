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

// Distance fog on the lit path. A lit model's surface fades linearly toward Tint
// as its distance from the camera goes from Start to End, in world units: as
// drawn nearer than Start, exactly Tint beyond End. The fade is applied to the
// finished colour, after tone mapping, so Tint is the colour a distant surface
// shows. An End at or before Start fogs everything beyond Start completely.
//
// Only lit models are fogged. Unlit materials carry their colour as authored,
// and primitives, sprites and lines never pass through the lit shader, so none
// of them is fogged. Off unless Enabled.
struct FogSettings {
    bool Enabled = false;
    Color Tint = White;
    float Start = 0.0f;
    float End = 1.0f;
};

// THE ORDER A PASS IS DRAWN IN, whatever order its command lists hold:
//   1. the background callbacks;
//   2. the OPAQUE 3D draws: primitives, then models, then 3D lines, depth-tested and writing depth;
//   3. the TRANSLUCENT 3D draws, farthest first (below), after everything in 2, blended over what is really there;
//   4. the 2D draws: sprites, lines, rectangles, and the world and UI callbacks.
// Within the opaque draws the order does not show: the depth test settles what is in front. Two opaque draws at exactly
// the same depth are the one case where it does, and the later one wins.
//
// A 3D draw is TRANSLUCENT when it blends with what is behind it:
//   - a primitive or a 3D line whose tint's alpha is below 255 (a grid has no tint and is opaque);
//   - a mesh of a model draw whose colour's alpha is below 255 -- the model's own times the draw's AlbedoTint -- or
//     whose glTF material says BLEND, which also counts the albedo texture's alpha. A cut-out (a draw's AlphaMask, a
//     file's MASK) is never translucent: it is drawn solid or not at all. A vertex colour's alpha is not read.
// A translucent draw is depth-TESTED, so anything nearer and opaque still hides it, but it writes no depth of its own.
// The translucent draws are sorted farthest first by the depth, along the camera's view direction, of the centre of the
// box around each: a primitive's position, a 3D line's midpoint, a model mesh's bounding box. That is one sort key per
// draw, and per MESH for a model: two meshes of one model are sorted apart, but the triangles of one mesh are drawn
// in the order the mesh lists them. Draws of equal depth, and every draw of a pass with no 3D camera, keep the order
// the pass lists them in: primitives, then models, then lines. There is no order-independent transparency.
// A model draw that sets PBRMaterial::DepthWrite off is a layer and asks for its submission order instead: its
// translucent meshes come after every sorted draw, in the order submitted.
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

    FogSettings Fog;

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
