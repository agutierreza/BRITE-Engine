#pragma once

// PRIVATE to the raylib render backend: render targets with a colour format and a readable depth, the HDR pass's
// chain -- its float scene target, the bloom chain and the tone map (RenderPass.hpp, PostSettings) -- the sun-shadow
// atlas, the planar reflection's blur, and the instanced draws' attribute stream. Raw OpenGL 3.3
// through raylib's own loader, glad, where rlgl has no call; no raylib type in this header.

#include "Backends/IRenderBackend.hpp"
#include "Backends/ReflectionBlur.hpp"
#include "Backends/RenderPass.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace BRITE::Backends::Raylib {

// The GL objects of one render target. `framebuffer` holds the single-sample colour (a texture every reader reads)
// and the depth (a texture when readable, else a renderbuffer); when multisampled, passes draw into
// `msFramebuffer` instead and are resolved into `framebuffer`.
struct TargetObjects {
    unsigned int framebuffer = 0;
    unsigned int colour = 0;
    unsigned int depthTexture = 0;
    unsigned int depthRenderbuffer = 0;
    unsigned int msFramebuffer = 0;
    unsigned int msColour = 0;
    unsigned int msDepth = 0;
    int samples = 1;
    int width = 0;
    int height = 0;
    RenderTargetFormat format = RenderTargetFormat::RGBA8;
};

// Makes the target; false, with `why` and nothing left made, when the size is not positive or the device cannot.
bool CreateTarget(const RenderTargetDescription& description, TargetObjects& out, std::string& why);
void DestroyTarget(TargetObjects& target);
// A multisampled target's samples averaged into its single-sample colour, and its depth copied when readable. A
// single-sample target needs nothing and is left alone.
void ResolveTarget(const TargetObjects& target);
// The target's colour read back, linear and unclamped, RGBA, bottom row first as OpenGL holds it.
bool ReadTargetFloats(const TargetObjects& target, std::vector<float>& rgba);
// Clears whatever framebuffer is bound to this colour, as floats (a float target keeps light above 1), and its depth.
void ClearBound(float r, float g, float b, float a);
// The most samples the device can give a render target.
int MostSamples();

// THE HDR CHAIN. One per backend, made on the first HDR pass, its targets kept between passes and remade when a pass
// asks for another size or sample count.
class PostChain {
  public:
    PostChain() = default;
    ~PostChain();
    PostChain(const PostChain&) = delete;
    PostChain& operator=(const PostChain&) = delete;

    // Compiles the chain's shaders on first use. False, logged once, when the device refuses any of them: every HDR
    // pass is then drawn as if Hdr were off.
    bool Ready();

    // Binds the float scene target, sized and multisampled as asked, and clears it -- to `linearClear` (RGB linear
    // light, alpha as authored) when `clear`, else to nothing (transparent). False, logged once, when the device
    // cannot make the target; nothing is then bound. `pass` is the backend's pass serial: the set drawn into is
    // stamped with it, for ForgetIdle.
    bool BeginScene(int width, int height, int samples, bool clear, const float linearClear[4], std::uint64_t pass);
    // Frees every set of targets -- parked or in use -- that no pass has drawn into for more than IdlePasses passes,
    // so a resize or a change of full screen does not keep the old sizes' targets for the application's life (at
    // 1080p a set is about 33 MB, at 4K about 130 MB). Called on every pass, with the backend's pass serial.
    void ForgetIdle(std::uint64_t pass);
    // Passes a set may stand unused before it is freed. Every pass counts, HDR or not -- a pass that draws a planar
    // reflection counts two -- so this is about a second at 144 frames a second with two passes a frame; a set a client
    // draws into every frame is never idle that long.
    static constexpr std::uint64_t IdlePasses = 240;
    // Ends drawing into the scene target and resolves it.
    void EndScene();
    // The scene's depth -- the depth the pass's 3D world left -- copied into a framebuffer of the same size whose depth
    // is a DEPTH_COMPONENT24 texture: a pass target made with ReadableDepth.
    void CopySceneDepth(unsigned int framebuffer) const;
    // MOTION BLUR (RenderPass.hpp, MotionBlurSettings), over the resolved scene, before the bloom. Its programs are
    // compiled on first use: false, logged once, when the device refuses any of them -- every pass is then drawn as if
    // motion blur were off.
    bool MotionReady();
    // Makes its targets for a pass of this size and tile (the gather's, in pixels), BEFORE the pass is drawn: kept
    // between passes, the tile's three remade alone when only the tile changes. False, logged once, when the device
    // cannot make them -- the pass is then drawn as with motion blur off -- and that size and tile is not asked again.
    bool PrepareMotion(int width, int height, int tile, std::uint64_t pass);
    // Copies the scene's depth where the blur's shaders can read it and binds the velocity target, cleared, for the
    // velocity draws: no depth test, no blending, no culling. False, nothing bound, unless PrepareMotion made the
    // targets at the scene's size.
    bool BeginMotion(std::uint64_t pass);
    // The velocity program for the draws that follow: an instanced draw's or a model draw's, writing motion or, when
    // `exempt`, the exempt mark (motion_velocity.fs). `depthRange`: near, far, 1 for a perspective projection.
    void UseVelocity(bool instanced, bool exempt, const float depthRange[3]);
    // One mesh drawn into the velocity target through the velocity program, by its position buffer and, when it has
    // one, its 16-bit index buffer; the two matrices column-major, the draw's model-view-projection now and last frame.
    void DrawVelocity(unsigned int positions, unsigned int indices, int elements, int vertices, const float mvpNow[16],
                      const float mvpBefore[16]);
    // An instanced draw's copies, uploaded once for all its meshes: each copy's model matrix now and, `withBefore`,
    // last frame's, column-major -- 32 floats a copy, or 16 read as both. Then each mesh of it drawn once per copy
    // uploaded, through the pass's two view-projections.
    void UploadVelocityInstances(const float* placements, int count, bool withBefore);
    void DrawVelocityInstanced(unsigned int positions, unsigned int indices, int elements, int vertices, int count,
                               const float viewProjectionNow[16], const float viewProjectionBefore[16]);
    // Ends the velocity draws, puts the state back, and blurs: each pixel's streak, the tiles, the gather into the
    // blurred picture, which the bloom and the tone map then read in place of the scene. `reprojection`: last frame's
    // view-projection times the inverse of this frame's, column-major; `cameraMoves` false leaves the camera still.
    void EndMotion(const MotionBlurSettings& settings, const float reprojection[16], bool cameraMoves,
                   const float depthRange[3]);
    // For tests: the last blurred pass's streaks (x, y in pixels, +y up; 1 for an exempt pixel; the distance along the
    // view) and its blurred picture, bottom row first; false when the last pass drew no blur. How many sets of motion
    // targets are held (0 or 1). RefuseMotionForTest: the blur's programs refused from now on, as on a device whose
    // compiler refuses them; RefuseMotionTargetsForTest: its targets refused, as on a device that cannot make them.
    bool ReadMotionFloats(std::vector<float>& rgba, int& width, int& height) const;
    bool ReadBlurredFloats(std::vector<float>& rgba, int& width, int& height) const;
    int MotionSetsHeld() const {
        return m_motion.velocity.framebuffer != 0 ? 1 : 0;
    }
    // How many times a set of the pass's size has been made, or tried: a pass at a size it holds, or at a size refused
    // before, makes none, and a change of tile alone remakes only the tile's three.
    int MotionSetsMade() const {
        return m_motionSetsMade;
    }
    bool BlurredThisPass() const {
        return m_blurredThisPass;
    }
    void RefuseMotionForTest() {
        m_refuseMotionForTest = true;
    }
    void RefuseMotionTargetsForTest() {
        m_refuseMotionTargetsForTest = true;
    }
    // Called as every pass begins: what the last pass blurred is no longer the last pass's.
    void ForgetLastBlur() {
        m_blurredThisPass = false;
    }

    // The bloom chain over the resolved scene, when the settings enable it; otherwise nothing.
    void Bloom(const BloomSettings& settings);
    // The tone map of the scene and its bloom into whatever framebuffer and viewport are bound: replacing what is
    // there when `replace`, laid over it by the scene's alpha when not. `displayReferred`: the scene holds display
    // colours (bloom with Hdr off), and the bloom is screened over them with no curve.
    void Composite(float exposure, const BloomSettings& settings, bool replace, bool displayReferred);

    // For tests: the scene target as it was last drawn (resolved), and how many bloom levels the chain holds.
    const TargetObjects& Scene() const {
        return m_scene;
    }
    // How many scene targets the chain has made since it was made: a pass at a size it holds makes none.
    int SceneTargetsMade() const {
        return m_sceneTargetsMade;
    }
    int BloomLevelsBuilt() const {
        return static_cast<int>(m_levels.size());
    }
    // How many sets of targets the chain holds now: the one in use, if any, and the parked ones.
    int SetsHeld() const {
        return (m_scene.framebuffer != 0 ? 1 : 0) + static_cast<int>(m_parked.size());
    }
    // The bloom's first level after the last Bloom: every level's light summed (BloomLevelsBuilt() of them).
    bool ReadBloomFloats(std::vector<float>& rgba, int& width, int& height) const;

  private:
    bool m_tried = false;
    bool m_ready = false;
    int m_sceneTargetsMade = 0;
    bool m_sceneFailureLogged = false;
    bool m_bloomFailureLogged = false;
    // Sizes (width, height, samples) the device refused a scene target at: not asked again.
    std::vector<std::array<int, 3>> m_refused;
    unsigned int m_vao = 0;
    unsigned int m_down = 0, m_up = 0, m_tonemap = 0;
    // The scene target and bloom levels in use, and up to MaxParked others, by size and samples: two passes a frame
    // at different sizes each keep their own rather than remaking one set every pass.
    TargetObjects m_scene;
    std::vector<TargetObjects> m_levels;
    std::uint64_t m_sceneUsed = 0; // the pass that last drew into m_scene
    struct Parked {
        TargetObjects scene;
        std::vector<TargetObjects> levels;
        std::uint64_t used = 0; // the pass that last drew into it
    };
    std::vector<Parked> m_parked; // most recently used last
    static constexpr std::size_t MaxParked = 3;
    bool m_bloomThisPass = false;
    float m_bloomScale = 0.0f;
    void FreeLevels();

    // Motion blur's programs, its own vertex array (position, and an instanced draw's placements, so no mesh's own
    // arrays are touched) and instance buffer, and its one set of targets.
    bool m_motionTried = false;
    bool m_motionReady = false;
    bool m_refuseMotionForTest = false;
    bool m_refuseMotionTargetsForTest = false;
    int m_motionSetsMade = 0;
    // Sizes and tiles (width, height, tile) the device refused motion blur's targets at: not asked again.
    std::vector<std::array<int, 3>> m_motionRefused;
    bool m_motionFailureLogged = false;
    unsigned int m_velocity = 0, m_velocityInstanced = 0, m_resolve = 0, m_tiles = 0, m_gather = 0;
    unsigned int m_velocityVao = 0, m_velocityInstances = 0;
    unsigned int m_velocityProgram = 0; // the one UseVelocity bound
    bool m_velocityWithBefore = true;   // the last upload's: each copy's matrix then, or its matrix now read twice
    struct MotionTargets {
        int width = 0, height = 0, tile = 0;
        unsigned int depthFramebuffer = 0, depthTexture = 0;
        TargetObjects velocity, motion, rows, tiles, neighbours, blurred;
        std::uint64_t used = 0;
    };
    MotionTargets m_motion;
    bool m_blurredThisPass = false;
    // What the bloom and the tone map read: the blurred picture on a pass that blurred, else the scene.
    unsigned int PostSource() const {
        return m_blurredThisPass ? m_motion.blurred.colour : m_scene.colour;
    }
    void FreeMotion();
    void FreeMotionTiles();
    struct SavedMotionState;
    std::shared_ptr<SavedMotionState> m_motionState;
};

// THE SUN-SHADOW ATLAS (RenderPass.hpp, ShadowSettings): one depth texture, a tile per cascade, sampled with hardware
// depth comparison. Made on the first pass with shadows, remade when a pass asks for another size.
class ShadowAtlas {
  public:
    ShadowAtlas() = default;
    ~ShadowAtlas();
    ShadowAtlas(const ShadowAtlas&) = delete;
    ShadowAtlas& operator=(const ShadowAtlas&) = delete;

    // The atlas at this size, made or kept; false, logged once, when the device cannot make it.
    bool Prepare(int width, int height);
    // Binds it, clears its depth, and sets the depth pass's state: depth test and writes on, no culling (a single
    // sheet casts from either side), a slope-scaled polygon offset against acne. End puts back the framebuffer, the
    // viewport and rlgl's own state.
    void Begin();
    void Tile(int x, int y, int size);
    void End();
    unsigned int Texture() const {
        return m_texture;
    }
    int Width() const {
        return m_width;
    }
    int Height() const {
        return m_height;
    }

  private:
    unsigned int m_framebuffer = 0;
    unsigned int m_texture = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_failureLogged = false;
    int m_savedFramebuffer = 0;
    int m_savedViewport[4] = {0, 0, 0, 0};
    void Free();
};

// THE PLANAR REFLECTION'S BLUR (RenderPass.hpp, ReflectionBlurSettings; ReflectionBlur.hpp): its program, compiled on
// first use, and the two passes that blur a reflection in place -- across into a scratch target of its size and format,
// then down back into the reflection. Its targets are the planar reflection's, kept and freed with it.
class ReflectionBlurPasses {
  public:
    ReflectionBlurPasses() = default;
    ~ReflectionBlurPasses();
    ReflectionBlurPasses(const ReflectionBlurPasses&) = delete;
    ReflectionBlurPasses& operator=(const ReflectionBlurPasses&) = delete;
    // Compiles the program on first use. False, logged once, when the device refuses it: every reflection is then
    // drawn sharp.
    bool Ready();
    // A colour target of this size and format, no depth: the blur's scratch. False, with `why` and nothing left made,
    // when the device cannot make it.
    static bool MakeScratch(int width, int height, RenderTargetFormat format, TargetObjects& out, std::string& why);
    // `reflection` blurred in place by `plan`, through `scratch`, of the same size and format. Leaves the GL state as
    // it found it, and the reflection read at its own size again.
    void Blur(const TargetObjects& reflection, const TargetObjects& scratch, const ReflectionBlur::Plan& plan);
    // For tests: the program refused from now on, as on a device whose compiler refuses it.
    void RefuseForTest() {
        m_refuseForTest = true;
    }

  private:
    bool m_tried = false;
    bool m_ready = false;
    bool m_refuseForTest = false;
    bool m_failureLogged = false;
    unsigned int m_program = 0;
    unsigned int m_vao = 0;
};

// THE INSTANCE STREAM (RenderPass.hpp, InstancedModelDrawCommand): an instanced draw's attributes, one set per copy --
// its placement, a column-major 4x4 as a ModelDrawCommand's model matrix is, at locations 9 to 12 (raylib's own for
// instanceTransform), and its tint as 0..1 floats at 13 -- streamed into one buffer the backend owns. pbr.vs and
// shadow_depth.vs read them in their BRITE_INSTANCED variants.
constexpr int InstanceTransformLocation = 9;
constexpr int InstanceTintLocation = 13;
struct InstanceData {
    float transform[16];
    float tint[4];
};
class InstanceStream {
  public:
    InstanceStream() = default;
    ~InstanceStream();
    InstanceStream(const InstanceStream&) = delete;
    InstanceStream& operator=(const InstanceStream&) = delete;
    // Draws the vertex array `vao` -- a mesh's: `elements` indices when it is indexed, else `vertices` vertices -- once
    // per copy in `data`, through the program and uniforms already set. Leaves the vertex array as it found it, the
    // instance attributes off and their divisors 0, and nothing bound.
    void Draw(unsigned int vao, bool indexed, int elements, int vertices, const InstanceData* data, int count);

  private:
    unsigned int m_buffer = 0;
};

} // namespace BRITE::Backends::Raylib
