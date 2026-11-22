#pragma once

#include "Math/BriteMath.hpp"
#include "ModelPose.hpp"
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
    // A skinned model's pose for this draw alone (ModelPose.hpp): at rest unless set, and ignored by a model with no
    // skin. Two draws of one model in one pass each draw their own pose.
    ModelPose Pose;
    // Whether the draw throws a shadow when its pass has a sun that casts (ShadowSettings). On by default; set it off
    // for what should never shade anything -- a sky dome, a sun disc, a road surface, grass. Only the draw's OPAQUE
    // meshes cast, and only when it writes its depth: a translucent mesh, an additive one and a layer (DepthWrite off)
    // never do. A cut-out casts the shape it cuts. A skinned mesh casts in the draw's pose.
    bool CastsShadow = true;
};

// One copy of an instanced draw's model (InstancedModelDrawCommand): placed as a ModelDrawCommand places its model --
// scaled, then rotated, then moved, after the model's own transform -- and tinted: its Tint multiplies the draw's
// AlbedoTint, alpha included, as a vertex colour does.
struct ModelInstance {
    Vector3 Position = {0.0f, 0.0f, 0.0f};
    Quaternion Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    Vector3 Scale = {1.0f, 1.0f, 1.0f};
    Color Tint = White;
};

// MANY COPIES OF ONE MODEL IN ONE DRAW PER MESH: windows, lanterns, grass, debris. Each mesh of the model is drawn once
// for every instance with one instanced draw, so the CPU pays for the meshes, not for the copies; the GPU still pays
// for every triangle of every copy. What a copy looks like is what a ModelDrawCommand of the same model and material
// draws at its placement, lit, under HDR, fog, shadows and reflections alike, but for:
//   - a skinned model is drawn at rest: the copies share no pose;
//   - a mesh is translucent (RenderPass.hpp, the order) when the model draw's rules make it so, or when any copy's Tint
//     alpha is below 255. A translucent mesh is drawn in the translucent stage as ONE sorted draw, keyed by the centre
//     of its copies' positions, with the copies inside it drawn farthest first; a layer's copies (DepthWrite off) are
//     drawn in the order listed. Copies of one mesh that interpenetrate blend in that order, as one mesh's triangles
//     do;
//   - the draw casts (CastsShadow, as a model draw's) per copy: each cascade draws the copies whose bounding sphere
//     reaches it, in one draw per mesh.
// Nothing is culled against the camera: the GPU clips what lies outside the view, and a client that knows its copies'
// layout culls them cheaper than BRITE could. An empty Instances draws nothing.
struct InstancedModelDrawCommand {
    ModelHandle Model = NullModelHandle;
    PBRMaterial Material;
    std::vector<ModelInstance> Instances;
    bool CastsShadow = true;
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
// of them is fogged. Off unless Enabled. Under an HDR pass (PostSettings) the
// fade runs in linear light toward the light that shows as Tint, so a surface
// beyond End still shows exactly Tint.
struct FogSettings {
    bool Enabled = false;
    Color Tint = White;
    float Start = 0.0f;
    float End = 1.0f;
};

// THE ORDER A PASS IS DRAWN IN, whatever order its command lists hold:
//   1. the background callbacks;
//   2. the OPAQUE 3D draws, depth-tested and writing depth: the models first, each as it is drawn, in the order listed,
//      then the instanced models (InstancedModelDrawCommand) in the order listed; then the primitives, then the 3D
//      lines, which the backend holds in a batch and draws together after the models;
//   3. the TRANSLUCENT 3D draws (below), after everything in 2, blended over what is really there;
//   4. the 2D draws: sprites, lines, rectangles, and the world and UI callbacks.
// Within the opaque draws the order shows only at exactly equal depth, where the later draw wins: a primitive beats a
// model, a line beats a primitive, and of two models the one listed later wins. A line lying on a model's surface
// therefore resolves in front of it.
//
// A 3D draw is TRANSLUCENT when it blends with what is behind it:
//   - a primitive or a 3D line whose tint's alpha is below 255 (a grid has no tint and is opaque);
//   - every mesh of a model draw whose material's Blend is Additive, cut-out or not;
//   - a mesh of a model draw whose colour's alpha is below 255 -- the model's own times the draw's AlbedoTint -- or
//     whose glTF material says BLEND, which also counts the albedo texture's alpha. A cut-out (a draw's AlphaMask, a
//     file's MASK) is otherwise never translucent: it is drawn solid or not at all. A vertex colour's alpha is not
//     read.
// A translucent draw is depth-TESTED, so anything nearer and opaque still hides it, but it writes no depth of its own.
//
// The translucent draws come in two groups, drawn in this order:
//   a. LAYERS, in the order submitted: the meshes of model draws that set PBRMaterial::DepthWrite off, which asks for
//      its submission order (see there);
//   b. the SORTED draws, farthest first: translucent primitives, 3D lines and model meshes, over the layers.
// The sort key is the depth, along the camera's view direction, of the centre of the box around each draw: a
// primitive's position, a 3D line's midpoint, a model mesh's bounding box as placed. That is one key per draw, and per
// MESH for a model: two meshes of one model are sorted apart, but the triangles of one mesh are drawn in the order the
// mesh lists them. A skinned model's mesh is sorted by its box at rest, whatever pose the draw puts it in. Draws of
// equal depth, and every draw of a pass with no 3D camera, keep the order the pass lists them in: primitives, then
// models, then lines. There is no order-independent transparency.
//
// With PostSettings::Hdr on (below), steps 1 to 3 -- the background callbacks and every 3D draw, in the same order --
// are drawn in linear light into the backend's own float target; then bloom, then the tone map onto the pass's
// target; then step 4, the 2D draws and callbacks, over the tone-mapped picture, exactly as without HDR.

// BLOOM: light above Threshold spread over the picture, as a lens and an eye spread a bright light. A chain of
// half-size copies of the pass's linear light: the first takes only what is above the threshold, each next one is a
// blurred half of the one before, and they are summed back up, each blurred again on the way, so a bright point gets
// a tight core and a wide faint halo.
//
// TWO WAYS TO BLOOM, by PostSettings::Hdr:
//   - HDR on: light above Threshold, in linear light (below), is spread and ADDED before the tone curve. A sun, a lamp,
//     a spark past white glows; an authored colour blooms only if it is very bright.
//   - HDR off, Enabled on (a DISPLAY-REFERRED bloom): the 3D world is drawn as an 8-bit pass draws it -- every colour,
//     translucent layers included, blended as a painter's canvas blends them -- into the backend's own target;
//     what is brighter than Threshold, now in the colours the screen shows (0..1), is spread, and SCREENED over the
//     picture: 1 - (1 - picture)(1 - Intensity x the bloom). No curve, no dither: a pixel the bloom does not reach
//     comes out exactly as it would without bloom. For a 2D or unlit game, the cheapest glow there is. Threshold should
//     then be below 1 (0.7 to 0.9 is usual): at the default 1 only the knee's soft start blooms, from 1 - Knee (128 of
//     255 at the default Knee) up. "As an 8-bit pass draws it" holds exactly for every 3D draw, opaque, translucent or
//     additive, cleared or not, where the bloom does not reach; where it does, the glow is screened within what the
//     world covered, never takes light away, and does not spill past it over a target the pass did not clear. It does
//     not reach
//     PostSettings::Samples (the backend's own target is multisampled as asked, not as the pass's target is), the
//     pass's Shader (applied to the 2D draws only, as under HDR), or the 2D draws, which come after and never bloom.
struct BloomSettings {
    bool Enabled = false;
    // How many half-size copies, each half the last: 1 to 8. Each adds a halo twice as wide as the one before and costs
    // two full-screen draws at its size, so the first is most of the cost.
    int Levels = 5;
    // Where bloom starts: linear light under HDR, a display colour 0..1 without. Under HDR an authored white is
    // about 7.24 (ToneCurve.hpp), so at the default of 1 an authored channel above about 231 of 255 (the light 1 at
    // exposure 1) blooms a little too, and from about 205 on the knee's soft start below -- a bright sky round a sun,
    // as the eye sees it. Raise it to keep authored colours out and let only emission past white bloom.
    float Threshold = 1.0f;
    // A soft start: light from Threshold * (1 - Knee) to Threshold * (1 + Knee) blooms on a curve rather than
    // switching on at the threshold. 0 is a hard edge.
    float Knee = 0.5f;
    // How much of the spread light is added back onto the picture: the picture plus Intensity times the bloom, before
    // the tone map. 0 adds none.
    float Intensity = 0.05f;
    // How far each level's blur reaches on the way back up, in texels of that level: 1 is the chain's own, larger is
    // wider and softer.
    float Radius = 1.0f;
};

// SUN SHADOWS. One directional light of the pass casts: every opaque model draw that CastsShadow is drawn from the
// light into a depth map fitted to the camera's view out to Distance, split along the view into Cascades slices, each a
// tile of one atlas MapSize texels a side; lit models darken that light's term alone where the map says it is hidden,
// so the ambient and the other lights still reach the shaded side. Unlit models, primitives and lines neither cast nor
// receive. Off unless Enabled; a pass with no 3D camera, or whose LightIndex names no directional light, has none.
//
// Each cascade's map is fitted to the sphere around its slice of the view and moved only in whole texels, so it does
// not swim as the camera moves or turns. A caster is drawn into a cascade only when its bounds can reach it -- the
// CPU's cost is draws, about one per caster mesh per cascade -- and a skinned mesh is judged by its bounds at rest. The
// shadow fades out over the last tenth of Distance, so its far edge never shows as a line. The lit shader is compiled
// once per feature set, so a pass without shadows never runs the shadow code.
enum class ShadowFilter {
    Hard,    // one hardware-compared lookup: a crisp edge, two texels soft
    Pcf3x3,  // nine lookups: an edge about four texels soft
    Poisson, // sixteen lookups on a turned disc: the softest edge, grainier up close
};

struct ShadowSettings {
    bool Enabled = false;
    // Which of the pass's Lights casts. It must be directional (a sun); any other entry, or an index out of range,
    // casts nothing.
    int LightIndex = 0;
    // Texels a side of each cascade's map: a cascade covers about 2 x its slice's radius, so this is its sharpness.
    int MapSize = 2048;
    // How many slices the view is split into, 1 to 4. Each is a full re-draw of the casters that reach it.
    int Cascades = 1;
    // How far along the view, in world units from the camera, surfaces receive shadow.
    float Distance = 60.0f;
    // Where the cascades split: the practical split of Zhang et al., 0 even slices, 1 logarithmic ones that give each
    // cascade the same texels per unit of depth on screen (ShadowCascades.hpp).
    float SplitLambda = 0.75f;
    // Explicit far edges of the first Cascades - 1 cascades, in world units along the view, nearest first: a positive
    // value beyond the previous edge and short of Distance overrides SplitLambda for that cascade. For a camera that
    // follows something, the first cascade should hold what it follows: a chase camera's subject at 7 to 25 units ahead
    // wants its first edge beyond 25.
    float Splits[3] = {0.0f, 0.0f, 0.0f};
    ShadowFilter Filter = ShadowFilter::Pcf3x3;
    // Bias against a surface shadowing itself: DepthBias in the map's 0..1 depth, NormalBias as a push of the receiving
    // point along its normal, in texels of its cascade -- so it scales with the cascade, as acne does.
    float DepthBias = 0.0006f;
    float NormalBias = 1.5f;
};

// REFLECTIONS, for the lit surfaces whose material asks for one (PBRMaterial::Reflection).
//   - The sky (ReflectionMode::Sky), nearly free: the pass's environment map along the reflected ray when it has one
//     (light, as the lit shader's ambient reads it), else SkyColor, authored as what the screen shows.
//   - A planar reflection (ReflectionMode::Planar), with Planar on: the 3D world drawn a second time, seen from the
//     camera mirrored in the plane y = PlaneHeight, into a target Resolution times the pass's size, and read by each
//     reflecting fragment where its own point projects into it. A lake, a wet road, a polished floor. Only what lies
//     above the plane is drawn into it -- the near plane of the mirrored view is the plane itself (Lengyel's oblique
//     near plane) -- so a lake bed or a bank under the water never shows in it. Translucent draws and layers are in
//     it; the reflecting draws themselves, the background callbacks and the shadows are not. It costs the 3D world
//     drawn twice: every draw a second time on the CPU, at Resolution^2 of the pixels on the GPU.
//     It is drawn as the pass draws: in linear light, in a float target, for an HDR pass; in the colours the screen
//     shows for any other, display-referred bloom included. Its targets are kept by size, as the HDR chain's are, and
//     freed once no pass has drawn one for a while. A device that cannot make one (said once in the log), an HDR pass
//     that falls back to 8 bits after it was drawn, and a camera at or below the plane, which sees its underside,
//     reflect the sky instead.
struct ReflectionSettings {
    Color SkyColor = {128, 170, 220, 255};
    bool Planar = false;
    float PlaneHeight = 0.0f;
    // The reflection's size, a fraction of the pass's target: 0.5 is a quarter of the pixels. Held to 0.05..1.
    float Resolution = 0.5f;
};

// HDR: a pass whose 3D world is drawn in linear light and shown through a tone curve, so light can go beyond white --
// a sun, a lamp, glints on water -- and roll off softly into it instead of clipping. OFF BY DEFAULT: a pass that never
// touches its Post draws exactly as it always has (the lit shader's own Reinhard curve, into whatever target the pass
// names).
//
// With Hdr on and a 3D camera on the pass:
//   - The background callbacks and the 3D world are drawn into the backend's own float target (RGBA16F), the size of
//     the pass's target, with Samples samples per pixel, then resolved, bloomed (Bloom) and tone-mapped
//     (ToneCurve.hpp: ACES at Exposure, then gamma, then a dither too fine to see that keeps a sky's gradient from
//     banding in 8 bits) onto the pass's target, where the 2D draws and the UI follow, untouched by the curve.
//   - Lit models are lit in linear light, and EmissionStrength above 1 is real (PBRMaterial::Emission). A glossy
//     surface's highlight is widened where its normal turns faster than a pixel can show (the geometric specular
//     anti-aliasing of Kaplanyan et al., 2016, and Tokuyoshi and Kaplanyan, 2019), so a curved or distant glossy
//     surface gives a highlight, not single-pixel sparkles that bloom would spread.
//   - WHAT A CALLER AUTHORS AS WHAT THE SCREEN SHOWS STILL SHOWS SO -- WHEN IT IS OPAQUE -- at any exposure: the clear
//     colour, an opaque unlit material, the fog's tint at and beyond its End, and the tint of every opaque primitive
//     and 3D line are run backwards through the curve, ToneCurve::AuthoredToLinear, and show as authored before bloom
//     adds anything.
//   - A TRANSLUCENT DRAW DOES NOT. It blends as LIGHT: its colour, as the light that shows it, mixed by its alpha with
//     the light behind it, and the mix then goes through the curve. Near white an authored colour is light seven times
//     brighter than white, so a layer weighs far more than its alpha says: a white layer at alpha 0.1 over a dark
//     (20, 30, 60) shows as (222, 222, 223), where an 8-bit pass shows (44, 53, 80) (HdrPassTests pins both). That is
//     right for light and wrong for a picture built of painted layers: a layered 2D picture wants the display-referred
//     bloom (BloomSettings) with Hdr off, not HDR.
//   - The background callbacks draw into the float target: what they write is linear light. A callback written for an
//     8-bit pass shows brighter under the curve.
//   - The pass's Shader is applied to its 2D draws only, after the tone map -- the client post-processing passes of an
//     Application keep working on the 8-bit picture -- and not to the 3D world.
//   - With ShouldClear off, the float target starts empty and the tone-mapped picture is laid over what the pass's
//     target held, where the 3D world drew; the target shows through elsewhere. The backend's target holds
//     PREMULTIPLIED colour, its alpha the coverage the 3D world built -- a layer of alpha a over nothing leaves alpha a
//     -- so a translucent layer lies over the pass's target by its own alpha, as in an 8-bit pass: white at alpha 0.1
//     over a grey of 0.5 shows 0.55 (HdrPassTests). Light that covers nothing -- an additive draw over nothing, the
//     bloom's spill past the world's edge -- is ADDED to what the target held. With Hdr off and Bloom on the
//     same holds of added light, exactly where the bloom does not reach; where it does, the glow is screened only
//     within what the world covered and never takes light away (BloomSettings). UNDER HDR, light added over a PARTLY
//     covered pixel is weighed by that pixel's coverage: the tone map divides by the coverage, maps and multiplies
//     back, so a bright spark over a faint haze adds about the haze's alpha of it, and below an alpha of 1/512 a pixel
//     counts as uncovered and its light is added whole. A pass that wants added light over thin layers exact clears its
//     target, or draws them display-referred, where the composite needs no divide.
// Without a 3D camera there is no 3D world to draw, and Hdr changes nothing.
//
// TRANSLUCENT LAYERS UNDER HDR blend in linear light, as light does: a 0.1 haze over a dark sky lifts it more than the
// same haze blended in display colours, and an authored layer near white weighs nearly as much as light seven times
// brighter. That is right for light -- glass, smoke, a lamp's halo -- and wrong for a painting built of layers (glows,
// vignettes, feathered edges), whose author judged each blend on a screen. The engine does not blend some layers one
// way and some the other within one pass: which layers are "paint" is the author's knowledge, and mixing the two
// spaces in one target would make every layer's result depend on what was drawn under it. A game drawn as paint wants
// the display-referred bloom above (Hdr off, Bloom on), which keeps every blend as the author saw it and still glows.
//
// A device that cannot draw into a float target, or whose compiler refuses the chain's shaders, draws every HDR pass
// as if Hdr were off -- the picture it has always drawn -- and logs why once. It never draws a black screen.
struct PostSettings {
    bool Hdr = false;
    // Samples per pixel of the float target: multisample anti-aliasing for the 3D world. 1 or less is none; a count
    // the device cannot give is lowered to its most. A 4-sample RGBA16F target holds 32 bytes of colour a pixel: on a
    // weak device this is the lever.
    int Samples = 4;
    // Linear light is multiplied by this before the curve: above 1 brighter, below 1 darker. Authored colours are not
    // moved by it (above).
    float Exposure = 1.0f;
    BloomSettings Bloom;
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

    // Lighting for ModelCommands. A model under no lights and zero ambient draws
    // black, which is the honest result of asking for a lit surface in the dark:
    // set at least one of the two. With an Environment map the ambient comes from
    // the map instead and these two ambient fields are unused.
    std::vector<Light> Lights;
    Color AmbientColor = White;
    float AmbientIntensity = 0.0f;

    FogSettings Fog;

    // HDR, bloom and the tone curve; off unless Post.Hdr is set (PostSettings).
    PostSettings Post;
    // Sun shadows; off unless Shadows.Enabled is set (ShadowSettings).
    ShadowSettings Shadows;
    // What reflecting surfaces reflect (ReflectionSettings); nothing is drawn for it unless a draw asks.
    ReflectionSettings Reflections;

    std::vector<ModelDrawCommand> ModelCommands;
    // Many copies of one model, one draw per mesh (InstancedModelDrawCommand); drawn after ModelCommands in each stage.
    std::vector<InstancedModelDrawCommand> InstancedModelCommands;
    std::vector<SpriteDrawCommand> SpriteCommands;
    std::vector<LineDrawCommand> LineCommands;
    std::vector<RectDrawCommand> RectCommands;
    std::vector<Primitive3DDrawCommand> Primitive3DCommands;
    std::vector<Line3DDrawCommand> Line3DCommands;

    // Optional callbacks executed immediately after ClearBackground, before anything else.
    // Useful for full-screen shaders like TorusStarsRenderSystem.
    // Drawn first, before the 3D world. Under an HDR pass or a display-referred bloom (PostSettings) they draw into the
    // backend's premultiplied target and start under its blend -- raylib's custom separate mode, the colour over by its
    // alpha and the alpha as coverage; a callback may set another for its own draws, and the backend sets its own again
    // before the 3D world, so nothing a callback leaves reaches the world's draws.
    std::vector<std::function<void()>> BackgroundDrawCallbacks;

    // Optional callbacks executed inside the 2D Camera mode (after BeginMode2D).
    // Useful for complex custom drawing (like shaders) that are not yet data-driven.
    std::vector<std::function<void()>> WorldDrawCallbacks;

    // Optional callbacks executed at the very end of the render pass,
    // after EndMode2D(), useful for immediate mode UI (ImGui) or direct debug text drawing.
    std::vector<std::function<void()>> UIDrawCallbacks;
};

} // namespace BRITE
