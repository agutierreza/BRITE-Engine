#pragma once

// PRIVATE to the raylib render backend: render targets with a colour format and a readable depth, and the HDR pass's
// chain -- its float scene target, the bloom chain and the tone map (RenderPass.hpp, PostSettings). Raw OpenGL 3.3
// through raylib's own loader, glad, where rlgl has no call; no raylib type in this header.

#include "Backends/IRenderBackend.hpp"
#include "Backends/RenderPass.hpp"

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
    // cannot make the target; nothing is then bound.
    bool BeginScene(int width, int height, int samples, bool clear, const float linearClear[4]);
    // Ends drawing into the scene target and resolves it.
    void EndScene();
    // The bloom chain over the resolved scene, when the settings enable it; otherwise nothing.
    void Bloom(const BloomSettings& settings);
    // The tone map of the scene and its bloom into whatever framebuffer and viewport are bound: replacing what is
    // there when `replace`, laid over it by the scene's alpha when not.
    void Composite(float exposure, const BloomSettings& settings, bool replace);

    // For tests: the scene target as it was last drawn (resolved), and how many bloom levels the chain holds.
    const TargetObjects& Scene() const {
        return m_scene;
    }
    int BloomLevelsBuilt() const {
        return static_cast<int>(m_levels.size());
    }
    // The bloom's first level after the last Bloom: every level's light summed (BloomLevelsBuilt() of them).
    bool ReadBloomFloats(std::vector<float>& rgba, int& width, int& height) const;

  private:
    bool m_tried = false;
    bool m_ready = false;
    bool m_targetFailureLogged = false;
    unsigned int m_vao = 0;
    unsigned int m_down = 0, m_up = 0, m_tonemap = 0;
    TargetObjects m_scene;
    std::vector<TargetObjects> m_levels;
    bool m_bloomThisPass = false;
    float m_bloomScale = 0.0f;
    void FreeLevels();
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

} // namespace BRITE::Backends::Raylib
