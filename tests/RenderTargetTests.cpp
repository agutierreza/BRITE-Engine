// Render targets with a colour format, samples and a readable depth (IRenderBackend::LoadRenderTarget), on the GPU
// under a hidden window (HiddenWindowScene.hpp).
//
// Light above 1 is written by a sprite drawn through a shader that writes a fixed colour, (3, 0.5, 2): each of the
// three is exactly representable in a half float (RGBA16F) and in the 11- and 10-bit floats of R11G11B10F (3 is
// 1.1 binary x 2, 0.5 and 2 powers of two), so a float target must read them back exactly.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include "HiddenWindowScene.hpp"

#include <Backends/IRenderBackend.hpp>

#include <cmath>

using BRITE::Backends::RenderTargetDescription;
using BRITE::Backends::RenderTargetFormat;

namespace {

const char* FIXED_LIGHT_FS = R"(#version 330
out vec4 finalColor;
void main() { finalColor = vec4(3.0, 0.5, 2.0, 1.0); })";

class RenderTarget : public HiddenWindow::Scene {
  protected:
    /// A full-target sprite drawn through the fixed-light shader into `target`.
    void DrawFixedLight(BRITE::TextureHandle target) {
        const BRITE::ShaderHandle shader = m_backend.LoadShaderFromMemory(nullptr, FIXED_LIGHT_FS);
        const BRITE::TextureHandle anything = m_backend.LoadRenderTexture(4, 4);
        BRITE::RenderPass pass;
        pass.TargetFramebuffer = target;
        pass.Shader = shader;
        BRITE::SpriteDrawCommand sprite{};
        sprite.Material.AlbedoMap = anything;
        sprite.SourceRect = {0.0f, 0.0f, 4.0f, 4.0f};
        sprite.DestRect = {0.0f, 0.0f, static_cast<float>(HiddenWindow::SIZE), static_cast<float>(HiddenWindow::SIZE)};
        pass.SpriteCommands.push_back(sprite);
        m_backend.SubmitRenderPass(pass);
        m_backend.UnloadRenderTexture(anything);
        m_backend.UnloadShader(shader);
    }

    std::vector<float> Linear(BRITE::TextureHandle target) {
        int width = 0, height = 0;
        std::vector<float> rgba;
        EXPECT_TRUE(m_backend.ReadRenderTargetLinear(target, width, height, rgba));
        EXPECT_EQ(width, HiddenWindow::SIZE);
        EXPECT_EQ(height, HiddenWindow::SIZE);
        return rgba;
    }
};

} // namespace

// Each float format reads back (3, 0.5, 2) exactly, and through ReadRenderTexture held to 0..1 as 8 bits: (255, 128,
// 255), 0.5 x 255 = 127.5 rounding to 128. An RGBA8 target made the same way clamps when it is drawn: 1, 0.5 stored
// as 127 or 128 of 255 (127.5 is a tie, whose rounding the GL leaves to the device), 1. RGBA16F keeps the alpha
// written, 1; R11G11B10F has none and reads 1. Mutation: ColourFormatOf gives RGBA8's internal format for RGBA16F ->
// the float target reads 1, not 3, red.
TEST_F(RenderTarget, AFloatTargetKeepsLightAboveOne) {
    for (RenderTargetFormat format : {RenderTargetFormat::RGBA16F, RenderTargetFormat::R11G11B10F}) {
        const BRITE::TextureHandle target =
            m_backend.LoadRenderTarget({HiddenWindow::SIZE, HiddenWindow::SIZE, format, 1, false});
        ASSERT_NE(target, BRITE::NullTextureHandle);
        DrawFixedLight(target);
        const std::vector<float> rgba = Linear(target);
        const std::size_t middle = HiddenWindow::AtLinear(0.0f, 0.0f);
        EXPECT_FLOAT_EQ(rgba[middle + 0], 3.0f);
        EXPECT_FLOAT_EQ(rgba[middle + 1], 0.5f);
        EXPECT_FLOAT_EQ(rgba[middle + 2], 2.0f);
        EXPECT_FLOAT_EQ(rgba[middle + 3], 1.0f);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        ASSERT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        EXPECT_TRUE(HiddenWindow::Within(pixels[HiddenWindow::At(0.0f, 0.0f)], 255, 128, 255, 0));
        m_backend.UnloadRenderTexture(target);
    }
    const BRITE::TextureHandle eightBit =
        m_backend.LoadRenderTarget({HiddenWindow::SIZE, HiddenWindow::SIZE, RenderTargetFormat::RGBA8, 1, false});
    ASSERT_NE(eightBit, BRITE::NullTextureHandle);
    DrawFixedLight(eightBit);
    const std::vector<float> rgba = Linear(eightBit);
    const std::size_t middle = HiddenWindow::AtLinear(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(rgba[middle + 0], 1.0f);
    EXPECT_NEAR(rgba[middle + 1], 0.5f, 0.5f / 255.0f + 1e-6f);
    EXPECT_FLOAT_EQ(rgba[middle + 2], 1.0f);
    m_backend.UnloadRenderTexture(eightBit);
}

// A multisampled float target is drawn into its multisampled surface and resolved when the pass ends, so its colour
// texture holds what the pass drew: here only the clear, (51, 102, 204) as 8-bit values, 0.2, 0.4 and 0.8 exactly --
// read through a half float, whose step near 0.8 is 2^-11 = 0.0005.
// Mutation: SubmitRenderPass skips ResolveTarget for a formatted target -> the colour texture is never drawn: 0, red.
TEST_F(RenderTarget, AMultisampledFloatTargetIsResolvedWhenThePassEnds) {
    const BRITE::TextureHandle target =
        m_backend.LoadRenderTarget({HiddenWindow::SIZE, HiddenWindow::SIZE, RenderTargetFormat::RGBA16F, 4, false});
    ASSERT_NE(target, BRITE::NullTextureHandle);
    EXPECT_GE(m_backend.RenderTextureSamples(target), 2);
    BRITE::RenderPass pass;
    pass.TargetFramebuffer = target;
    pass.ClearColor = {51, 102, 204, 255};
    m_backend.SubmitRenderPass(pass);
    const std::vector<float> rgba = Linear(target);
    EXPECT_NEAR(rgba[0], 0.2f, 1e-3f);
    EXPECT_NEAR(rgba[1], 0.4f, 1e-3f);
    EXPECT_NEAR(rgba[2], 0.8f, 1e-3f);
    m_backend.UnloadRenderTexture(target);
}

// The readable depth holds the last pass's depth, as a texture another pass can draw. An orthographic camera's depth
// is linear in distance: depth = (distance - near) / (far - near), raylib's near 0.05 and far 4000. A quad at a
// distance of 0.05 + 0.25 x 3999.95 = 1000.0375 from the camera at z = 5 -- at z = -995.0375 -- therefore holds 0.25
// wherever it is drawn. The depth texture is then drawn as a sprite into a float target, where a depth texture
// samples as its depth in red: 0.25, to the 24-bit depth's step (6e-8) and the half float's (2^-13 near 0.25).
// With 4 samples the depth is copied in the resolve too.
// Mutation: ResolveTarget copies no depth -> the 4-sample target's depth texture keeps its clear (1.0), red.
TEST_F(RenderTarget, AReadableDepthHoldsTheLastPassesDepth) {
    const BRITE::ModelHandle quad = Quad(-3.0f, -3.0f, 3.0f, 3.0f, -995.0375f);
    for (int samples : {1, 4}) {
        const BRITE::TextureHandle target = m_backend.LoadRenderTarget(
            {HiddenWindow::SIZE, HiddenWindow::SIZE, RenderTargetFormat::RGBA8, samples, true});
        ASSERT_NE(target, BRITE::NullTextureHandle);
        const BRITE::TextureHandle depth = m_backend.RenderTargetDepth(target);
        ASSERT_NE(depth, BRITE::NullTextureHandle);

        BRITE::RenderPass pass;
        pass.ModelCommands.push_back(Unlit(quad, {255, 255, 255, 255}));
        Picture(pass, target);

        const BRITE::TextureHandle reader =
            m_backend.LoadRenderTarget({HiddenWindow::SIZE, HiddenWindow::SIZE, RenderTargetFormat::RGBA16F, 1, false});
        BRITE::RenderPass read;
        read.TargetFramebuffer = reader;
        BRITE::SpriteDrawCommand sprite{};
        sprite.Material.AlbedoMap = depth;
        sprite.SourceRect = {0.0f, 0.0f, static_cast<float>(HiddenWindow::SIZE),
                             static_cast<float>(HiddenWindow::SIZE)};
        sprite.DestRect = sprite.SourceRect;
        read.SpriteCommands.push_back(sprite);
        m_backend.SubmitRenderPass(read);
        const std::vector<float> rgba = Linear(reader);
        EXPECT_NEAR(rgba[HiddenWindow::AtLinear(0.0f, 0.0f)], 0.25f, 2e-4f) << samples << " samples";
        m_backend.UnloadRenderTexture(reader);
        m_backend.UnloadRenderTexture(target);
    }
}

// Only a target made with ReadableDepth has a depth to give, and the depth goes with its target: an UnloadTexture of
// it alone is refused, and once the target is unloaded RenderTargetDepth names nothing. A size that is not positive
// makes no target.
// Mutation: UnloadTexture frees a target's depth -> the target's depth handle stops naming a texture, red.
TEST_F(RenderTarget, TheDepthBelongsToItsTarget) {
    const BRITE::TextureHandle plain = m_backend.LoadRenderTexture(HiddenWindow::SIZE, HiddenWindow::SIZE);
    EXPECT_EQ(m_backend.RenderTargetDepth(plain), BRITE::NullTextureHandle);
    m_backend.UnloadRenderTexture(plain);
    const BRITE::TextureHandle noDepth =
        m_backend.LoadRenderTarget({HiddenWindow::SIZE, HiddenWindow::SIZE, RenderTargetFormat::RGBA16F, 1, false});
    EXPECT_EQ(m_backend.RenderTargetDepth(noDepth), BRITE::NullTextureHandle);
    m_backend.UnloadRenderTexture(noDepth);

    const BRITE::TextureHandle target =
        m_backend.LoadRenderTarget({HiddenWindow::SIZE, HiddenWindow::SIZE, RenderTargetFormat::RGBA8, 1, true});
    const BRITE::TextureHandle depth = m_backend.RenderTargetDepth(target);
    ASSERT_NE(depth, BRITE::NullTextureHandle);
    m_backend.UnloadTexture(depth);
    EXPECT_NE(m_backend.NativeTextureId(depth), 0u);
    m_backend.UnloadRenderTexture(target);
    EXPECT_EQ(m_backend.RenderTargetDepth(target), BRITE::NullTextureHandle);
    EXPECT_EQ(m_backend.NativeTextureId(depth), 0u);

    EXPECT_EQ(m_backend.LoadRenderTarget({0, HiddenWindow::SIZE, RenderTargetFormat::RGBA8, 1, false}),
              BRITE::NullTextureHandle);
}
