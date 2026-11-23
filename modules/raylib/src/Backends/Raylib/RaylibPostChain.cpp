// Render targets with a format and a readable depth, and the HDR pass's chain (RaylibPostChain.hpp). Raw OpenGL 3.3
// through raylib's glad loader, whose function pointers raylib compiles in and leaves visible to the program it is
// linked into, loaded once the window exists. Every function here leaves the GL state rlgl relies on as it found it:
// the framebuffer, the viewport, blending as SRC_ALPHA / ONE_MINUS_SRC_ALPHA, depth testing off, culling on, the
// program and texture unit 0.
#include "external/glad.h"

#include "Backends/MotionBlur.hpp"
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

// A program of a vertex stage -- the full-screen one unless given -- and one fragment stage; 0, logged, when either is
// refused.
unsigned int MakeProgram(const char* fragment, const char* name,
                         const char* vertex = EmbeddedShaders::POST_FULLSCREEN_VERTEX) {
    const unsigned int vs = CompileStage(GL_VERTEX_SHADER, vertex, name);
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
    FreeMotion();
    for (unsigned int program : {m_velocity, m_velocityInstanced, m_resolve, m_tiles, m_gather})
        if (program != 0)
            glDeleteProgram(program);
    if (m_velocityVao != 0)
        glDeleteVertexArrays(1, &m_velocityVao);
    if (m_velocityInstances != 0)
        glDeleteBuffers(1, &m_velocityInstances);
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
    m_blurredThisPass = false;
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
    if (m_motion.velocity.framebuffer != 0 && idle(m_motion.used))
        FreeMotion();
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
    unsigned int source = PostSource();
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
    glBindTexture(GL_TEXTURE_2D, bloom ? m_levels.front().colour : PostSource());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, PostSource());
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
// Motion blur
// ---------------------------------------------------------------------------

// What BeginMotion found bound and set, put back by EndMotion before the full-screen draws.
struct PostChain::SavedMotionState {
    GLint draw = 0, read = 0;
    std::array<GLint, 4> viewport{};
    GLboolean depthMask = GL_TRUE;
    FullScreenState state;
};

namespace {

// A colour-only target of the motion set: RGBA16F, sampled by texel, or LINEAR for the picture the bloom reads.
bool MakeColourTarget(int width, int height, GLenum filter, TargetObjects& out) {
    out = {};
    out.width = width;
    out.height = height;
    out.format = RenderTargetFormat::RGBA16F;
    out.colour = MakeTexture(width, height, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, filter);
    glGenFramebuffers(1, &out.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, out.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out.colour, 0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

// A mesh's positions at location 0 of the bound vertex array, its 16-bit indices bound, and the draw.
void BindPositions(unsigned int positions, unsigned int indices) {
    glBindBuffer(GL_ARRAY_BUFFER, positions);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
}

} // namespace

bool PostChain::MotionReady() {
    if (m_refuseMotionForTest) {
        if (!m_motionFailureLogged)
            spdlog::error("BRITE: this device cannot run motion blur; every pass is drawn without it");
        m_motionFailureLogged = true;
        return false;
    }
    if (m_motionTried)
        return m_motionReady;
    m_motionTried = true;
    std::string instanced = EmbeddedShaders::MOTION_VELOCITY_VERTEX;
    instanced.insert(instanced.find('\n') + 1, "#define BRITE_INSTANCED 1\n");
    m_velocity = MakeProgram(EmbeddedShaders::MOTION_VELOCITY_FRAGMENT, "motion velocity",
                             EmbeddedShaders::MOTION_VELOCITY_VERTEX);
    m_velocityInstanced =
        MakeProgram(EmbeddedShaders::MOTION_VELOCITY_FRAGMENT, "instanced motion velocity", instanced.c_str());
    m_resolve = MakeProgram(EmbeddedShaders::MOTION_RESOLVE_FRAGMENT, "motion");
    m_tiles = MakeProgram(EmbeddedShaders::MOTION_TILES_FRAGMENT, "motion tiles");
    m_gather = MakeProgram(EmbeddedShaders::MOTION_GATHER_FRAGMENT, "motion gather");
    glGenVertexArrays(1, &m_velocityVao);
    glGenBuffers(1, &m_velocityInstances);
    m_motionReady = m_velocity != 0 && m_velocityInstanced != 0 && m_resolve != 0 && m_tiles != 0 && m_gather != 0 &&
                    m_velocityVao != 0 && m_velocityInstances != 0;
    if (!m_motionReady && !m_motionFailureLogged)
        spdlog::error("BRITE: this device cannot run motion blur; every pass is drawn without it");
    m_motionFailureLogged = m_motionFailureLogged || !m_motionReady;
    return m_motionReady;
}

void PostChain::FreeMotion() {
    for (TargetObjects* target : {&m_motion.velocity, &m_motion.motion, &m_motion.rows, &m_motion.tiles,
                                  &m_motion.neighbours, &m_motion.blurred})
        DestroyTarget(*target);
    if (m_motion.depthFramebuffer != 0)
        glDeleteFramebuffers(1, &m_motion.depthFramebuffer);
    if (m_motion.depthTexture != 0)
        glDeleteTextures(1, &m_motion.depthTexture);
    m_motion = {};
}

void PostChain::FreeMotionTiles() {
    for (TargetObjects* target : {&m_motion.rows, &m_motion.tiles, &m_motion.neighbours})
        DestroyTarget(*target);
    m_motion.tile = 0;
}

bool PostChain::PrepareMotion(int width, int height, int tile, std::uint64_t pass) {
    const std::array<int, 3> size = {width, height, tile};
    if (std::find(m_motionRefused.begin(), m_motionRefused.end(), size) != m_motionRefused.end())
        return false; // refused once at this size and tile: not asked again
    const SavedBinding saved;
    bool complete = true;
    // The targets of the pass's size: kept while it keeps its size.
    if (m_motion.velocity.framebuffer == 0 || m_motion.width != width || m_motion.height != height) {
        FreeMotion();
        ++m_motionSetsMade;
        m_motion.depthTexture =
            MakeTexture(width, height, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST);
        glGenFramebuffers(1, &m_motion.depthFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_motion.depthFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_motion.depthTexture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        complete = MakeColourTarget(width, height, GL_NEAREST, m_motion.velocity) && complete;
        complete = MakeColourTarget(width, height, GL_NEAREST, m_motion.motion) && complete;
        complete = MakeColourTarget(width, height, GL_LINEAR, m_motion.blurred) && complete;
        m_motion.width = width;
        m_motion.height = height;
    }
    // The tile's three: remade alone when only the tile changes.
    if (complete && m_motion.tile != tile) {
        FreeMotionTiles();
        const int tilesWide = (width + tile - 1) / tile, tilesHigh = (height + tile - 1) / tile;
        complete = MakeColourTarget(tilesWide, height, GL_NEAREST, m_motion.rows);
        complete = MakeColourTarget(tilesWide, tilesHigh, GL_NEAREST, m_motion.tiles) && complete;
        complete = MakeColourTarget(tilesWide, tilesHigh, GL_NEAREST, m_motion.neighbours) && complete;
        m_motion.tile = tile;
    }
    if (!complete || m_refuseMotionTargetsForTest) {
        if (!m_motionFailureLogged)
            spdlog::error("BRITE: a pass cannot make its {}x{} motion blur targets; it is drawn without motion blur",
                          width, height);
        m_motionFailureLogged = true;
        m_motionRefused.push_back(size);
        FreeMotion();
        return false;
    }
    m_motion.used = pass;
    return true;
}

bool PostChain::BeginMotion(std::uint64_t pass) {
    // PrepareMotion made the targets at the pass's size, before the scene was drawn at the same size.
    if (m_scene.framebuffer == 0 || m_motion.velocity.framebuffer == 0 || m_motion.width != m_scene.width ||
        m_motion.height != m_scene.height)
        return false;
    const int width = m_scene.width, height = m_scene.height;
    m_motion.used = pass;
    // The depth the scene left, where a shader can read it.
    CopySceneDepth(m_motion.depthFramebuffer);

    ::rlDrawRenderBatchActive();
    auto saved = std::make_shared<SavedMotionState>();
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved->draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved->read);
    glGetIntegerv(GL_VIEWPORT, saved->viewport.data());
    glGetBooleanv(GL_DEPTH_WRITEMASK, &saved->depthMask);
    saved->state = BeginFullScreen(m_velocityVao);
    glDepthMask(GL_FALSE);
    m_motionState = std::move(saved);
    glBindFramebuffer(GL_FRAMEBUFFER, m_motion.velocity.framebuffer);
    glViewport(0, 0, width, height);
    // Nothing written: alpha 0 -- the pixel takes the camera's motion.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, m_motion.depthTexture);
    return true;
}

void PostChain::UseVelocity(bool instanced, bool exempt, const float depthRange[3]) {
    m_velocityProgram = instanced ? m_velocityInstanced : m_velocity;
    glUseProgram(m_velocityProgram);
    glUniform1i(Uniform(m_velocityProgram, "sceneDepth"), 0);
    glUniform2f(Uniform(m_velocityProgram, "size"), static_cast<float>(m_motion.width),
                static_cast<float>(m_motion.height));
    glUniform1i(Uniform(m_velocityProgram, "exempt"), exempt ? 1 : 0);
    glUniform3f(Uniform(m_velocityProgram, "depthRange"), depthRange[0], depthRange[1], depthRange[2]);
}

void PostChain::DrawVelocity(unsigned int positions, unsigned int indices, int elements, int vertices,
                             const float mvpNow[16], const float mvpBefore[16]) {
    if (positions == 0)
        return;
    glUniformMatrix4fv(Uniform(m_velocityProgram, "mvpNow"), 1, GL_FALSE, mvpNow);
    glUniformMatrix4fv(Uniform(m_velocityProgram, "mvpBefore"), 1, GL_FALSE, mvpBefore);
    BindPositions(positions, indices);
    if (indices != 0)
        glDrawElements(GL_TRIANGLES, elements, GL_UNSIGNED_SHORT, nullptr);
    else
        glDrawArrays(GL_TRIANGLES, 0, vertices);
}

void PostChain::UploadVelocityInstances(const float* placements, int count, bool withBefore) {
    if (placements == nullptr || count <= 0)
        return;
    m_velocityWithBefore = withBefore;
    glBindBuffer(GL_ARRAY_BUFFER, m_velocityInstances);
    // A fresh store each draw: the driver gives a new one rather than wait for the draw still reading the last.
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(float) * (withBefore ? 32 : 16)) * count, placements,
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void PostChain::DrawVelocityInstanced(unsigned int positions, unsigned int indices, int elements, int vertices,
                                      int count, const float viewProjectionNow[16],
                                      const float viewProjectionBefore[16]) {
    if (positions == 0 || count <= 0)
        return;
    glUniformMatrix4fv(Uniform(m_velocityProgram, "viewProjectionNow"), 1, GL_FALSE, viewProjectionNow);
    glUniformMatrix4fv(Uniform(m_velocityProgram, "viewProjectionBefore"), 1, GL_FALSE, viewProjectionBefore);
    BindPositions(positions, indices);
    glBindBuffer(GL_ARRAY_BUFFER, m_velocityInstances);
    // Columns 0-3 are the matrix now (locations 1-4), 4-7 last frame's (5-8); an upload of one matrix a copy gives
    // both from the same four.
    const GLsizei stride = static_cast<GLsizei>(sizeof(float) * (m_velocityWithBefore ? 32 : 16));
    for (GLuint column = 0; column < 8; ++column) {
        const std::size_t source = m_velocityWithBefore ? column : column % 4;
        glEnableVertexAttribArray(1 + column);
        glVertexAttribPointer(1 + column, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(sizeof(float) * 4 * source));
        glVertexAttribDivisor(1 + column, 1);
    }
    if (indices != 0)
        glDrawElementsInstanced(GL_TRIANGLES, elements, GL_UNSIGNED_SHORT, nullptr, count);
    else
        glDrawArraysInstanced(GL_TRIANGLES, 0, vertices, count);
    for (GLuint column = 0; column < 8; ++column) {
        glVertexAttribDivisor(1 + column, 0);
        glDisableVertexAttribArray(1 + column);
    }
}

void PostChain::EndMotion(const MotionBlurSettings& settings, const float reprojection[16], bool cameraMoves,
                          const float depthRange[3]) {
    if (!m_motionState)
        return;
    const MotionTargets& t = m_motion;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // The motion set's own vertex array is still bound, so this clears its index binding, no mesh's.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Each pixel's streak.
    glUseProgram(m_resolve);
    glBindFramebuffer(GL_FRAMEBUFFER, t.motion.framebuffer);
    glViewport(0, 0, t.width, t.height);
    glUniform1i(Uniform(m_resolve, "velocity"), 0);
    glUniform1i(Uniform(m_resolve, "sceneDepth"), 1);
    glUniformMatrix4fv(Uniform(m_resolve, "reprojection"), 1, GL_FALSE, reprojection);
    glUniform1i(Uniform(m_resolve, "cameraMoves"), cameraMoves ? 1 : 0);
    glUniform2f(Uniform(m_resolve, "size"), static_cast<float>(t.width), static_cast<float>(t.height));
    glUniform1f(Uniform(m_resolve, "shutter"), std::clamp(settings.Shutter, 0.0f, 1.0f));
    glUniform1f(Uniform(m_resolve, "maxLength"),
                std::clamp(settings.MaxLength, MotionBlur::LeastMaxLength, MotionBlur::MostMaxLength));
    glUniform3f(Uniform(m_resolve, "depthRange"), depthRange[0], depthRange[1], depthRange[2]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t.depthTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.velocity.colour);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    // The tiles' longest streaks, across the rows then down the columns, then each tile's neighbours'.
    glUseProgram(m_tiles);
    glUniform1i(Uniform(m_tiles, "source"), 0);
    glUniform1i(Uniform(m_tiles, "tile"), t.tile);
    const TargetObjects* steps[3][2] = {{&t.motion, &t.rows}, {&t.rows, &t.tiles}, {&t.tiles, &t.neighbours}};
    for (int step = 0; step < 3; ++step) {
        const TargetObjects& from = *steps[step][0];
        const TargetObjects& to = *steps[step][1];
        glBindFramebuffer(GL_FRAMEBUFFER, to.framebuffer);
        glViewport(0, 0, to.width, to.height);
        glUniform1i(Uniform(m_tiles, "step"), step);
        glUniform2i(Uniform(m_tiles, "sourceSize"), from.width, from.height);
        glBindTexture(GL_TEXTURE_2D, from.colour);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // The gather, into the picture the bloom and the tone map read.
    glUseProgram(m_gather);
    glBindFramebuffer(GL_FRAMEBUFFER, t.blurred.framebuffer);
    glViewport(0, 0, t.width, t.height);
    glUniform1i(Uniform(m_gather, "scene"), 0);
    glUniform1i(Uniform(m_gather, "motion"), 1);
    glUniform1i(Uniform(m_gather, "neighbourMax"), 2);
    glUniform1i(Uniform(m_gather, "tile"), t.tile);
    glUniform1i(Uniform(m_gather, "samples"),
                std::clamp(settings.Samples, MotionBlur::LeastSamples, MotionBlur::MostSamples));
    glUniform2i(Uniform(m_gather, "size"), t.width, t.height);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, t.neighbours.colour);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t.motion.colour);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_scene.colour);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    for (GLenum unit : {GL_TEXTURE2, GL_TEXTURE1}) {
        glActiveTexture(unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Back as BeginMotion found it.
    const SavedMotionState& saved = *m_motionState;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(saved.draw));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(saved.read));
    glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]);
    glDepthMask(saved.depthMask);
    EndFullScreen(saved.state);
    m_motionState.reset();
    m_blurredThisPass = true;
}

bool PostChain::ReadMotionFloats(std::vector<float>& rgba, int& width, int& height) const {
    if (!m_blurredThisPass)
        return false;
    width = m_motion.width;
    height = m_motion.height;
    return ReadTargetFloats(m_motion.motion, rgba);
}

bool PostChain::ReadBlurredFloats(std::vector<float>& rgba, int& width, int& height) const {
    if (!m_blurredThisPass)
        return false;
    width = m_motion.width;
    height = m_motion.height;
    return ReadTargetFloats(m_motion.blurred, rgba);
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

// ---------------------------------------------------------------------------
// The planar reflection's blur
// ---------------------------------------------------------------------------

ReflectionBlurPasses::~ReflectionBlurPasses() {
    if (!m_tried || !::IsWindowReady())
        return;
    if (m_program != 0)
        glDeleteProgram(m_program);
    if (m_vao != 0)
        glDeleteVertexArrays(1, &m_vao);
}

bool ReflectionBlurPasses::Ready() {
    if (m_refuseForTest) {
        if (!m_failureLogged)
            spdlog::error("BRITE: this device cannot blur a planar reflection; reflections are drawn sharp");
        m_failureLogged = true;
        return false;
    }
    if (m_tried)
        return m_ready;
    m_tried = true;
    m_program = MakeProgram(EmbeddedShaders::REFLECTION_BLUR_FRAGMENT, "reflection blur");
    glGenVertexArrays(1, &m_vao);
    m_ready = m_program != 0 && m_vao != 0;
    if (!m_ready && !m_failureLogged)
        spdlog::error("BRITE: this device cannot blur a planar reflection; reflections are drawn sharp");
    m_failureLogged = m_failureLogged || !m_ready;
    return m_ready;
}

bool ReflectionBlurPasses::MakeScratch(int width, int height, RenderTargetFormat format, TargetObjects& out,
                                       std::string& why) {
    out = {};
    if (width <= 0 || height <= 0) {
        why = "its size is not positive";
        return false;
    }
    const SavedBinding saved;
    const ColourFormat colour = ColourFormatOf(format);
    out.width = width;
    out.height = height;
    out.format = format;
    out.colour = MakeTexture(width, height, colour.internal, colour.format, colour.type, GL_LINEAR);
    glGenFramebuffers(1, &out.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, out.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out.colour, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        why = "the device cannot draw into a target of that format";
        DestroyTarget(out);
        return false;
    }
    return true;
}

void ReflectionBlurPasses::Blur(const TargetObjects& reflection, const TargetObjects& scratch,
                                const ReflectionBlur::Plan& plan) {
    ::rlDrawRenderBatchActive();
    const SavedBinding saved;
    const FullScreenState was = BeginFullScreen(m_vao);
    glUseProgram(m_program);
    glUniform1i(Uniform(m_program, "source"), 0);
    // One pass: `from` read at the pass's level -- its mipmaps made first when that is not its own -- into `to`.
    auto pass = [&](unsigned int from, const TargetObjects& to, const ReflectionBlur::Pass& p, bool across) {
        glBindTexture(GL_TEXTURE_2D, from);
        if (p.Level > 0) {
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, to.framebuffer);
        glViewport(0, 0, to.width, to.height);
        glUniform2f(Uniform(m_program, "step"), across ? p.Step : 0.0f, across ? 0.0f : p.Step);
        glUniform1f(Uniform(m_program, "lod"), static_cast<float>(p.Level));
        glUniform1f(Uniform(m_program, "spread"), p.Spread);
        glUniform1i(Uniform(m_program, "taps"), p.Taps);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // Every other reader reads the texture at its own size, as it was made.
        if (p.Level > 0)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    };
    pass(reflection.colour, scratch, plan.AcrossPass, true);
    pass(scratch.colour, reflection, plan.DownPass, false);
    EndFullScreen(was);
}

} // namespace BRITE::Backends::Raylib
