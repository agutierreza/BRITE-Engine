// Render targets with a format and a readable depth, and the HDR pass's chain (RaylibPostChain.hpp). Raw OpenGL 3.3
// through raylib's glad loader, whose function pointers raylib compiles in and leaves visible to the program it is
// linked into, loaded once the window exists. Every function here leaves the GL state rlgl relies on as it found it:
// the framebuffer, the viewport, blending as SRC_ALPHA / ONE_MINUS_SRC_ALPHA, depth testing off, culling on, the
// program and texture unit 0.
#include "external/glad.h"

#include "Backends/Raylib/PostShaderSource.hpp"
#include "RaylibPostChain.hpp"

#include <raylib.h>
#include <rlgl.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace BRITE::Backends::Raylib {

namespace {

struct ColourFormat {
    GLenum internal;
    GLenum format;
    GLenum type;
};

ColourFormat ColourFormatOf(RenderTargetFormat format) {
    switch (format) {
    case RenderTargetFormat::RGBA16F:
        return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
    case RenderTargetFormat::R11G11B10F:
        return {GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT};
    case RenderTargetFormat::RGBA8:
    default:
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    }
}

unsigned int MakeTexture(int width, int height, GLenum internal, GLenum format, GLenum type, GLenum filter) {
    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

// The framebuffers and viewport bound now, put back when this goes out of scope.
class SavedBinding {
  public:
    SavedBinding() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_read);
        glGetIntegerv(GL_VIEWPORT, m_viewport.data());
    }
    ~SavedBinding() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_read));
        glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    }
    SavedBinding(const SavedBinding&) = delete;
    SavedBinding& operator=(const SavedBinding&) = delete;

  private:
    GLint m_draw = 0;
    GLint m_read = 0;
    std::array<GLint, 4> m_viewport{};
};

unsigned int CompileStage(GLenum stage, const char* source, const char* name) {
    const unsigned int id = glCreateShader(stage);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 1024> log{};
        glGetShaderInfoLog(id, static_cast<GLsizei>(log.size()), nullptr, log.data());
        spdlog::error("BRITE: the HDR chain's {} shader did not compile: {}", name, log.data());
        glDeleteShader(id);
        return 0;
    }
    return id;
}

// A program of the full-screen vertex stage and one fragment stage; 0, logged, when either is refused.
unsigned int MakeProgram(const char* fragment, const char* name) {
    const unsigned int vs = CompileStage(GL_VERTEX_SHADER, EmbeddedShaders::POST_FULLSCREEN_VERTEX, "full-screen");
    const unsigned int fs = CompileStage(GL_FRAGMENT_SHADER, fragment, name);
    unsigned int program = 0;
    if (vs != 0 && fs != 0) {
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            std::array<char, 1024> log{};
            glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
            spdlog::error("BRITE: the HDR chain's {} program did not link: {}", name, log.data());
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vs != 0)
        glDeleteShader(vs);
    if (fs != 0)
        glDeleteShader(fs);
    return program;
}

int Uniform(unsigned int program, const char* name) {
    return glGetUniformLocation(program, name);
}

// The GL state a full-screen draw changes, as it was: put back by EndFullScreen.
struct FullScreenState {
    GLboolean depthTest = GL_FALSE, cullFace = GL_TRUE, blend = GL_TRUE;
    GLint blendSrcRgb = GL_SRC_ALPHA, blendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
    GLint blendSrcAlpha = GL_SRC_ALPHA, blendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    GLint blendEquationRgb = GL_FUNC_ADD, blendEquationAlpha = GL_FUNC_ADD;
};

// The GL state a full-screen draw wants: no depth test, no culling, no blending unless asked.
FullScreenState BeginFullScreen(unsigned int vao) {
    FullScreenState was;
    was.depthTest = glIsEnabled(GL_DEPTH_TEST);
    was.cullFace = glIsEnabled(GL_CULL_FACE);
    was.blend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_RGB, &was.blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &was.blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &was.blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &was.blendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &was.blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &was.blendEquationAlpha);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);
    return was;
}

// ... and back to the state it found.
void EndFullScreen(const FullScreenState& was) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glUseProgram(0);
    auto set = [](GLenum capability, GLboolean on) { on ? glEnable(capability) : glDisable(capability); };
    set(GL_DEPTH_TEST, was.depthTest);
    set(GL_CULL_FACE, was.cullFace);
    set(GL_BLEND, was.blend);
    glBlendFuncSeparate(static_cast<GLenum>(was.blendSrcRgb), static_cast<GLenum>(was.blendDstRgb),
                        static_cast<GLenum>(was.blendSrcAlpha), static_cast<GLenum>(was.blendDstAlpha));
    glBlendEquationSeparate(static_cast<GLenum>(was.blendEquationRgb), static_cast<GLenum>(was.blendEquationAlpha));
}

} // namespace

void ClearBound(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

int MostSamples() {
    GLint most = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &most);
    return static_cast<int>(most);
}

bool CreateTarget(const RenderTargetDescription& description, TargetObjects& out, std::string& why) {
    out = {};
    if (description.Width <= 0 || description.Height <= 0) {
        why = "its size is not positive";
        return false;
    }
    const SavedBinding saved;
    const ColourFormat colour = ColourFormatOf(description.Format);
    out.width = description.Width;
    out.height = description.Height;
    out.format = description.Format;

    out.colour = MakeTexture(out.width, out.height, colour.internal, colour.format, colour.type, GL_LINEAR);
    glGenFramebuffers(1, &out.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, out.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out.colour, 0);
    if (description.ReadableDepth) {
        out.depthTexture =
            MakeTexture(out.width, out.height, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, out.depthTexture, 0);
    } else {
        glGenRenderbuffers(1, &out.depthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, out.depthRenderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, out.width, out.height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, out.depthRenderbuffer);
    }
    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    const int samples = std::min(description.Samples, MostSamples());
    if (complete && samples > 1) {
        out.samples = samples;
        glGenRenderbuffers(1, &out.msColour);
        glBindRenderbuffer(GL_RENDERBUFFER, out.msColour);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, colour.internal, out.width, out.height);
        glGenRenderbuffers(1, &out.msDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, out.msDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, out.width, out.height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glGenFramebuffers(1, &out.msFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, out.msFramebuffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, out.msColour);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, out.msDepth);
        complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    if (!complete) {
        why = "the device cannot draw into a target of that format";
        DestroyTarget(out);
        return false;
    }
    return true;
}

void DestroyTarget(TargetObjects& target) {
    for (unsigned int* framebuffer : {&target.framebuffer, &target.msFramebuffer})
        if (*framebuffer != 0)
            glDeleteFramebuffers(1, framebuffer);
    for (unsigned int* texture : {&target.colour, &target.depthTexture})
        if (*texture != 0)
            glDeleteTextures(1, texture);
    for (unsigned int* renderbuffer : {&target.depthRenderbuffer, &target.msColour, &target.msDepth})
        if (*renderbuffer != 0)
            glDeleteRenderbuffers(1, renderbuffer);
    target = {};
}

void ResolveTarget(const TargetObjects& target) {
    if (target.msFramebuffer == 0)
        return;
    const SavedBinding saved;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.msFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
    glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    // A depth blit must be NEAREST; the depth of a multisampled pixel is resolved by the driver's own rule (the
    // first sample on most), which is a depth that pixel really had.
    if (target.depthTexture != 0)
        glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height, GL_DEPTH_BUFFER_BIT,
                          GL_NEAREST);
}

bool ReadTargetFloats(const TargetObjects& target, std::vector<float>& rgba) {
    if (target.colour == 0)
        return false;
    rgba.assign(static_cast<std::size_t>(target.width) * static_cast<std::size_t>(target.height) * 4, 0.0f);
    glBindTexture(GL_TEXTURE_2D, target.colour);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------

PostChain::~PostChain() {
    // The objects die with the context; only a context still open is told.
    if (!m_tried || !::IsWindowReady())
        return;
    DestroyTarget(m_scene);
    FreeLevels();
    for (Parked& parked : m_parked) {
        DestroyTarget(parked.scene);
        for (TargetObjects& level : parked.levels)
            DestroyTarget(level);
    }
    for (unsigned int program : {m_down, m_up, m_tonemap})
        if (program != 0)
            glDeleteProgram(program);
    if (m_vao != 0)
        glDeleteVertexArrays(1, &m_vao);
}

bool PostChain::Ready() {
    if (m_tried)
        return m_ready;
    m_tried = true;
    m_down = MakeProgram(EmbeddedShaders::POST_BLOOM_DOWN_FRAGMENT, "bloom downsample");
    m_up = MakeProgram(EmbeddedShaders::POST_BLOOM_UP_FRAGMENT, "bloom upsample");
    m_tonemap = MakeProgram(EmbeddedShaders::POST_TONEMAP_FRAGMENT, "tone map");
    glGenVertexArrays(1, &m_vao);
    m_ready = m_down != 0 && m_up != 0 && m_tonemap != 0 && m_vao != 0;
    if (!m_ready)
        spdlog::error("BRITE: this device cannot run the HDR chain; every HDR pass is drawn as if Hdr were off");
    return m_ready;
}

bool PostChain::BeginScene(int width, int height, int samples, bool clear, const float linearClear[4],
                           std::uint64_t pass) {
    const int wanted = std::max(1, std::min(samples, MostSamples()));
    if (m_scene.framebuffer == 0 || m_scene.width != width || m_scene.height != height || m_scene.samples != wanted) {
        const std::array<int, 3> size = {width, height, wanted};
        if (std::find(m_refused.begin(), m_refused.end(), size) != m_refused.end())
            return false; // refused once at this size: not asked again
        // The set in use is parked, and one parked at this size is taken back; the least recently used beyond
        // MaxParked is freed.
        if (m_scene.framebuffer != 0) {
            m_parked.push_back({m_scene, m_levels, m_sceneUsed});
            m_scene = {};
            m_levels.clear();
        }
        auto match = std::find_if(m_parked.begin(), m_parked.end(), [&](const Parked& parked) {
            return parked.scene.width == width && parked.scene.height == height && parked.scene.samples == wanted;
        });
        if (match != m_parked.end()) {
            m_scene = match->scene;
            m_levels = match->levels;
            m_parked.erase(match);
        }
        while (m_parked.size() > MaxParked) {
            DestroyTarget(m_parked.front().scene);
            for (TargetObjects& level : m_parked.front().levels)
                DestroyTarget(level);
            m_parked.erase(m_parked.begin());
        }
    }
    if (m_scene.framebuffer == 0) {
        std::string why;
        const RenderTargetDescription description{width, height, RenderTargetFormat::RGBA16F, wanted, false};
        ++m_sceneTargetsMade;
        if (!CreateTarget(description, m_scene, why)) {
            if (!m_sceneFailureLogged)
                spdlog::error("BRITE: a pass cannot make its {}x{} float target ({}); it is drawn without HDR or "
                              "bloom",
                              width, height, why);
            m_sceneFailureLogged = true;
            m_refused.push_back({width, height, wanted});
            return false;
        }
    }
    m_sceneUsed = pass;
    ::RenderTexture2D surface{};
    surface.id = m_scene.msFramebuffer != 0 ? m_scene.msFramebuffer : m_scene.framebuffer;
    surface.texture.id = m_scene.colour;
    surface.texture.width = width;
    surface.texture.height = height;
    surface.texture.mipmaps = 1;
    surface.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    ::BeginTextureMode(surface);
    // Premultiplied, as the scene is kept: a clear colour with alpha below 1 is stored as colour x alpha.
    if (clear)
        glClearColor(linearClear[0] * linearClear[3], linearClear[1] * linearClear[3], linearClear[2] * linearClear[3],
                     linearClear[3]);
    else
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}

void PostChain::EndScene() {
    ::EndTextureMode();
    ResolveTarget(m_scene);
}

void PostChain::CopySceneDepth(unsigned int framebuffer) const {
    if (m_scene.framebuffer == 0 || framebuffer == 0)
        return;
    const SavedBinding saved;
    // The multisampled surface's depth, resolved by the blit, or the single-sample target's own.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene.msFramebuffer != 0 ? m_scene.msFramebuffer : m_scene.framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glBlitFramebuffer(0, 0, m_scene.width, m_scene.height, 0, 0, m_scene.width, m_scene.height, GL_DEPTH_BUFFER_BIT,
                      GL_NEAREST);
}

void PostChain::ForgetIdle(std::uint64_t pass) {
    const auto idle = [&](std::uint64_t used) { return pass > used && pass - used > IdlePasses; };
    for (auto it = m_parked.begin(); it != m_parked.end();) {
        if (!idle(it->used)) {
            ++it;
            continue;
        }
        DestroyTarget(it->scene);
        for (TargetObjects& level : it->levels)
            DestroyTarget(level);
        it = m_parked.erase(it);
    }
    if (m_scene.framebuffer != 0 && idle(m_sceneUsed)) {
        DestroyTarget(m_scene);
        m_scene = {};
        FreeLevels();
    }
}

void PostChain::FreeLevels() {
    for (TargetObjects& level : m_levels)
        DestroyTarget(level);
    m_levels.clear();
}

void PostChain::Bloom(const BloomSettings& settings) {
    m_bloomThisPass = false;
    m_bloomScale = 0.0f;
    if (!settings.Enabled || settings.Intensity <= 0.0f || m_scene.colour == 0)
        return;
    const int levels = std::clamp(settings.Levels, 1, 8);
    if (static_cast<int>(m_levels.size()) != levels) {
        FreeLevels();
        int width = m_scene.width, height = m_scene.height;
        for (int i = 0; i < levels; ++i) {
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
            TargetObjects level;
            std::string why;
            if (!CreateTarget({width, height, RenderTargetFormat::R11G11B10F, 1, false}, level, why)) {
                if (!m_bloomFailureLogged)
                    spdlog::error("BRITE: a pass cannot make its bloom ({}); it is drawn without bloom", why);
                m_bloomFailureLogged = true;
                FreeLevels();
                return;
            }
            m_levels.push_back(level);
        }
    }

    const SavedBinding saved;
    const FullScreenState was = BeginFullScreen(m_vao);

    // Down: the scene into the first level through the threshold, then each level into the next, each half the size.
    glUseProgram(m_down);
    glUniform1i(Uniform(m_down, "source"), 0);
    const float knee = std::max(settings.Knee * settings.Threshold, 1.0e-4f);
    glUniform4f(Uniform(m_down, "knee"), settings.Threshold, settings.Threshold - knee, 2.0f * knee, 0.25f / knee);
    unsigned int source = m_scene.colour;
    int sourceWidth = m_scene.width, sourceHeight = m_scene.height;
    for (std::size_t i = 0; i < m_levels.size(); ++i) {
        const TargetObjects& level = m_levels[i];
        glBindFramebuffer(GL_FRAMEBUFFER, level.framebuffer);
        glViewport(0, 0, level.width, level.height);
        glUniform1i(Uniform(m_down, "prefilter"), i == 0 ? 1 : 0);
        glUniform2f(Uniform(m_down, "texel"), 1.0f / static_cast<float>(sourceWidth),
                    1.0f / static_cast<float>(sourceHeight));
        glBindTexture(GL_TEXTURE_2D, source);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        source = level.colour;
        sourceWidth = level.width;
        sourceHeight = level.height;
    }

    // Up: each level blurred and added onto the one above it, so the first ends holding every level's light.
    glUseProgram(m_up);
    glUniform1i(Uniform(m_up, "source"), 0);
    glUniform1f(Uniform(m_up, "radius"), std::max(settings.Radius, 0.0f));
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    for (std::size_t i = m_levels.size() - 1; i > 0; --i) {
        const TargetObjects& from = m_levels[i];
        const TargetObjects& to = m_levels[i - 1];
        glBindFramebuffer(GL_FRAMEBUFFER, to.framebuffer);
        glViewport(0, 0, to.width, to.height);
        glUniform2f(Uniform(m_up, "texel"), 1.0f / static_cast<float>(from.width),
                    1.0f / static_cast<float>(from.height));
        glBindTexture(GL_TEXTURE_2D, from.colour);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    EndFullScreen(was);

    // The first level holds the sum of `levels` levels' light: Intensity of their average is added back.
    m_bloomThisPass = true;
    m_bloomScale = settings.Intensity / static_cast<float>(levels);
}

void PostChain::Composite(float exposure, const BloomSettings& settings, bool replace, bool displayReferred) {
    (void)settings;
    ::rlDrawRenderBatchActive();
    const FullScreenState was = BeginFullScreen(m_vao);
    // Laid over the target, the tone map's output is premultiplied (its colour already times the scene's coverage), so
    // it blends ONE, ONE_MINUS_SRC_ALPHA: where the world drew nothing the target shows through unchanged, and a layer
    // of alpha a lies over it by a, as in an 8-bit pass. Replacing, the output is straight colour and alpha.
    if (!replace) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glUseProgram(m_tonemap);
    glUniform1i(Uniform(m_tonemap, "scene"), 0);
    glUniform1i(Uniform(m_tonemap, "bloom"), 1);
    const bool bloom = m_bloomThisPass && !m_levels.empty();
    glUniform1f(Uniform(m_tonemap, "bloomScale"), bloom ? m_bloomScale : 0.0f);
    glUniform1f(Uniform(m_tonemap, "exposure"), exposure);
    glUniform1i(Uniform(m_tonemap, "displayReferred"), displayReferred ? 1 : 0);
    glUniform1i(Uniform(m_tonemap, "overlay"), replace ? 0 : 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom ? m_levels.front().colour : m_scene.colour);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_scene.colour);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    EndFullScreen(was);
}

bool PostChain::ReadBloomFloats(std::vector<float>& rgba, int& width, int& height) const {
    if (m_levels.empty())
        return false;
    width = m_levels.front().width;
    height = m_levels.front().height;
    return ReadTargetFloats(m_levels.front(), rgba);
}

// ---------------------------------------------------------------------------
// The sun-shadow atlas
// ---------------------------------------------------------------------------

ShadowAtlas::~ShadowAtlas() {
    if (::IsWindowReady())
        Free();
}

void ShadowAtlas::Free() {
    if (m_framebuffer != 0)
        glDeleteFramebuffers(1, &m_framebuffer);
    if (m_texture != 0)
        glDeleteTextures(1, &m_texture);
    m_framebuffer = 0;
    m_texture = 0;
    m_width = m_height = 0;
}

bool ShadowAtlas::Prepare(int width, int height) {
    if (m_texture != 0 && m_width == width && m_height == height)
        return true;
    Free();
    const SavedBinding saved;
    m_texture = MakeTexture(width, height, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    // Read through a sampler2DShadow: each lookup compares, and LINEAR gives the hardware's 2x2 filter of the results.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!complete) {
        if (!m_failureLogged)
            spdlog::error("BRITE: a {}x{} shadow atlas could not be made; the pass is drawn without shadows", width,
                          height);
        m_failureLogged = true;
        Free();
        return false;
    }
    m_width = width;
    m_height = height;
    return true;
}

void ShadowAtlas::Begin() {
    ::rlDrawRenderBatchActive();
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_savedFramebuffer);
    glGetIntegerv(GL_VIEWPORT, m_savedViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glViewport(0, 0, m_width, m_height);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
}

// ---------------------------------------------------------------------------
// The instance stream
// ---------------------------------------------------------------------------

InstanceStream::~InstanceStream() {
    // As the chain's and the atlas's: no GL call once the window, and its context, are gone.
    if (m_buffer != 0 && ::IsWindowReady())
        glDeleteBuffers(1, &m_buffer);
}

void InstanceStream::Draw(unsigned int vao, bool indexed, int elements, int vertices, const InstanceData* data,
                          int count) {
    if (vao == 0 || data == nullptr || count <= 0)
        return;
    if (m_buffer == 0)
        glGenBuffers(1, &m_buffer);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    // A fresh store every draw: the driver gives a new one rather than wait for the draw still reading the last.
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(InstanceData)) * count, data, GL_STREAM_DRAW);
    const GLsizei stride = static_cast<GLsizei>(sizeof(InstanceData));
    for (int column = 0; column < 4; ++column) {
        const GLuint location = static_cast<GLuint>(InstanceTransformLocation + column);
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(sizeof(float) * 4 * static_cast<std::size_t>(column)));
        glVertexAttribDivisor(location, 1);
    }
    glEnableVertexAttribArray(InstanceTintLocation);
    glVertexAttribPointer(InstanceTintLocation, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(InstanceData, tint)));
    glVertexAttribDivisor(InstanceTintLocation, 1);
    if (indexed)
        ::rlDrawVertexArrayElementsInstanced(0, elements, nullptr, count);
    else
        ::rlDrawVertexArrayInstanced(0, vertices, count);
    // The mesh's vertex array is drawn without instances everywhere else: the attributes are turned off again.
    for (int location = InstanceTransformLocation; location <= InstanceTintLocation; ++location) {
        glVertexAttribDivisor(static_cast<GLuint>(location), 0);
        glDisableVertexAttribArray(static_cast<GLuint>(location));
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void ShadowAtlas::Tile(int x, int y, int size) {
    glViewport(x * size, y * size, size, size);
}

void ShadowAtlas::End() {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_savedFramebuffer));
    glViewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
}

} // namespace BRITE::Backends::Raylib
