// Sun shadows (RenderPass.hpp, ShadowSettings) and the lit shader's variants, on the GPU under a hidden window
// (HiddenWindowScene.hpp).
//
// The picture: an orthographic camera 10 above the origin looks straight down, the top of the picture toward -z, the
// view 4 units across: on the 64 pixel target, column (x + 2) x 16 and row (z + 2) x 16. A white ground in the plane
// y = 0 faces up. A caster -- a flat white sheet 1 unit across at height 1, over x and z -0.5..0.5 -- hides the ground
// beneath it from the camera, and a sun travelling (1, -1, 0) throws its shadow 1 unit along +x, onto x 0.5..1.5. So
// the ground at (1, 0) is in shadow and the ground at (-1.5, 0) is not; both are far from any edge.
//
// Read as linear light from an HDR pass (one sample a pixel), the ground in shadow is lit by the ambient alone: an
// ambient of 0.25 on a white albedo, 0.25. In the sun it also takes the sun's diffuse term, (1 - 0.04) / pi x N.L,
// N.L = cos 45 = 0.7071, 0.2161 -- plus a little specular -- so at least 0.46. Beside each case is the mutation that
// turns it red, and each was run once.

#include "HiddenWindowScene.hpp"

#include <cmath>

using HiddenWindow::SIZE;

namespace {

constexpr float SHADOWED = 0.25f;
constexpr float SUNLIT_AT_LEAST = 0.45f;

class SunShadow : public HiddenWindow::Scene {
  protected:
    /// A rectangle in the plane y = height, x0..x1 by z0..z1, facing up.
    BRITE::ModelHandle Flat(float x0, float z0, float x1, float z1, float height) {
        BRITE::MeshData mesh;
        mesh.Positions = {{x0, height, z1}, {x1, height, z1}, {x1, height, z0}, {x0, height, z0}};
        mesh.Normals.assign(4, {0.0f, 1.0f, 0.0f});
        mesh.TexCoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        mesh.Indices = {0, 1, 2, 0, 2, 3};
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
        m_models.push_back(model);
        return model;
    }
    /// A cube of half-size h centred at (x, y, z).
    BRITE::ModelHandle Cube(float x, float y, float z, float h) {
        BRITE::MeshData mesh;
        const BRITE::Vector3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const BRITE::Vector3& N : n) {
            const BRITE::Vector3 u = std::fabs(N.y) > 0.5f ? BRITE::Vector3{1, 0, 0} : BRITE::Vector3{0, 1, 0};
            const BRITE::Vector3 v{N.y * u.z - N.z * u.y, N.z * u.x - N.x * u.z, N.x * u.y - N.y * u.x};
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.Positions.size());
            for (auto [a, b] : {std::pair{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}}) {
                mesh.Positions.push_back({x + (N.x + a * u.x + b * v.x) * h, y + (N.y + a * u.y + b * v.y) * h,
                                          z + (N.z + a * u.z + b * v.z) * h});
                mesh.Normals.push_back(N);
            }
            mesh.Indices.insert(mesh.Indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3, base, base + 2,
                                                     base + 1, base, base + 3, base + 2});
        }
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
        m_models.push_back(model);
        return model;
    }
    static BRITE::ModelDrawCommand Lit(BRITE::ModelHandle model, BRITE::Color tint = {255, 255, 255, 255}) {
        BRITE::ModelDrawCommand draw = Unlit(model, tint);
        draw.Material.Unlit = false;
        draw.Material.DoubleSided = false;
        return draw;
    }

    /// The pass over the ground and `caster`, lit by the sun (light 0, casting) and the ambient, shadows on.
    BRITE::RenderPass Pass(const BRITE::ModelDrawCommand& caster, bool hdr = true) {
        BRITE::RenderPass pass;
        pass.Post.Hdr = hdr;
        pass.Post.Samples = 1;
        BRITE::Light sun;
        sun.Direction = {1.0f, -1.0f, 0.0f};
        pass.Lights = {sun};
        pass.AmbientColor = {255, 255, 255, 255};
        pass.AmbientIntensity = SHADOWED;
        pass.Shadows.Enabled = true;
        pass.Shadows.MapSize = 512;
        pass.Shadows.Cascades = 1;
        pass.Shadows.Distance = 20.0f;
        pass.ModelCommands = {Lit(Flat(-3, -3, 3, 3, 0)), caster};
        return pass;
    }

    /// Submit under the camera above; the linear light at (x, z), from the HDR scene.
    std::vector<float> Draw(BRITE::RenderPass& pass) {
        BRITE::Camera3D camera{{0, 10, 0}, {0, 0, 0}, {0, 0, -1}, 4.0f, BRITE::CameraProjection::Orthographic};
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
        pass.TargetFramebuffer = target;
        pass.Camera3DPtr = &camera;
        m_backend.SubmitRenderPass(pass);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        m_lastPixels = pixels;
        m_backend.UnloadRenderTexture(target);
        pass.Camera3DPtr = nullptr;
        std::vector<float> linear;
        if (pass.Post.Hdr)
            linear = LinearScene();
        return linear;
    }
    static float At(const std::vector<float>& linear, float x, float z) {
        const int column = static_cast<int>((x + 2.0f) * 16.0f);
        const int row = SIZE - 1 - static_cast<int>((z + 2.0f) * 16.0f);
        return linear[static_cast<std::size_t>(row * SIZE + column) * 4];
    }

    std::vector<BRITE::Color> m_lastPixels;
};

} // namespace

// The sheet's shadow lies on the ground at (1, 0); the ground at (-1.5, 0) is in the sun. And the shadow darkens the
// CASTING light alone: with a second sun, the same but not casting (light 1), the shaded ground takes that light's
// diffuse term again, so it reads as sunlit.
// Mutation: SunShadow applied to every light's term -> with two suns the shaded ground is still the ambient, red.
TEST_F(SunShadow, ASheetShadowsTheGroundOnTheCastingLightAlone) {
    BRITE::RenderPass pass = Pass(Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f)));
    std::vector<float> linear = Draw(pass);
    EXPECT_NEAR(At(linear, 1.0f, 0.0f), SHADOWED, 0.01f);
    EXPECT_GT(At(linear, -1.5f, 0.0f), SUNLIT_AT_LEAST);

    pass.Lights.push_back(pass.Lights.front());
    linear = Draw(pass);
    EXPECT_GT(At(linear, 1.0f, 0.0f), SUNLIT_AT_LEAST);
}

// Shadows need no HDR. In an 8-bit pass the shaded ground is the ambient, 0.25, through the lit shader's own curve:
// Reinhard 0.25 / 1.25 = 0.2, gamma 0.2^(1/2.2) = exp(-1.609438 / 2.2) = 0.481157, 122.7 of 255.
// Mutation: no shadow variant chosen without HDR -> the shaded ground is sunlit, far brighter, red.
TEST_F(SunShadow, AnEightBitPassCastsShadowsToo) {
    BRITE::RenderPass pass = Pass(Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f)), false);
    Draw(pass);
    const BRITE::Color shaded = m_lastPixels[static_cast<std::size_t>(32 * SIZE + 48)]; // (1, 0)
    EXPECT_TRUE(HiddenWindow::Within(shaded, 123, 123, 123, 2)) << int(shaded.r);
}

// What does not cast: a draw that says so (CastsShadow off), a translucent one, an additive one, a layer (DepthWrite
// off) and an unlit one (a sky dome would otherwise shade the world). Under each the ground at (1, 0) is sunlit.
// Mutation: unlit draws cast -> the unlit sheet shades the ground, red.
// Mutation: CastsShadow not read -> the first is shaded, red.
TEST_F(SunShadow, WhatDoesNotCast) {
    const BRITE::ModelHandle sheet = Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f);
    BRITE::ModelDrawCommand off = Lit(sheet);
    off.CastsShadow = false;
    BRITE::ModelDrawCommand translucent = Lit(sheet, {255, 255, 255, 128});
    BRITE::ModelDrawCommand additive = Lit(sheet);
    additive.Material.Blend = BRITE::BlendMode::Additive;
    BRITE::ModelDrawCommand layer = Lit(sheet);
    layer.Material.DepthWrite = false;
    BRITE::ModelDrawCommand unlit = Lit(sheet);
    unlit.Material.Unlit = true;
    for (const BRITE::ModelDrawCommand& caster : {off, translucent, additive, layer, unlit}) {
        BRITE::RenderPass pass = Pass(caster);
        EXPECT_GT(At(Draw(pass), 1.0f, 0.0f), SUNLIT_AT_LEAST);
    }
}

// A cut-out casts what it cuts. A sheet cut out at 0.5 whose alpha is 100/255 = 0.39 is cut away entirely and casts
// nothing; at 200/255 = 0.78 it stays, and casts.
// Mutation: the depth pass ignores the cut-out (no discard) -> the 0.39 sheet casts, red.
TEST_F(SunShadow, ACutOutCastsWhatItCuts) {
    const BRITE::ModelHandle sheet = Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f);
    for (const auto& [alpha, shaded] : {std::pair{100, false}, std::pair{200, true}}) {
        BRITE::ModelDrawCommand cut = Lit(sheet, {255, 255, 255, static_cast<unsigned char>(alpha)});
        cut.Material.AlphaMask = true;
        cut.Material.AlphaCutoff = 0.5f;
        BRITE::RenderPass pass = Pass(cut);
        const float light = At(Draw(pass), 1.0f, 0.0f);
        if (shaded)
            EXPECT_NEAR(light, SHADOWED, 0.01f);
        else
            EXPECT_GT(light, SUNLIT_AT_LEAST);
    }
}

// CASTERS CULLED PER CASCADE. Two cascades with the first edge set at 8 (Splits), out to 20, the sun straight down. The
// camera looks down from y = 10, so a cascade's depth along the view is 10 - y, and the light's view, looking down too,
// has its depth axis z = y. The orthographic view is 2 either side at every depth, so:
//   cascade 0, depths 0.05..8: centre at depth 4.025 (y 5.975), radius sqrt(2^2 + 2^2 + 3.975^2) = 4.8788, up to 79/16
//     = 4.9375; it spans light z 1.0375..10.9125;
//   cascade 1, depths 8..20: centre at depth 14 (y -4), radius sqrt(4 + 4 + 36) = 6.6332, up to 107/16 = 6.6875; it
//     spans z -10.6875..2.6875.
// Three cube casters (CastsShadow, one mesh each; the cube of half-size h has a sphere of h sqrt 3):
//   A, h 0.1 at y 0.1: z + 0.173 = 0.273 is below cascade 0's far side 1.0375 -- culled there; inside cascade 1;
//   B, h 0.1 at y 7: inside cascade 0; above cascade 1, toward the sun, so it can shade it -- kept;
//   C, at x 30: outside both squares (light x = -30, far beyond 4.9 + 0.17 and 6.7 + 0.17).
// The ground casts too, a sheet 6 across at y 0 (sphere radius 4.243, z 0 + 4.243 >= 1.0375): in both. So cascade 0
// draws 2 (B and the ground) and cascade 1 draws 3 (A, B and the ground).
// Mutation: CasterReachesCascade always true -> 4 and 4, red. Always false -> 0 and 0, red.
TEST_F(SunShadow, CastersAreCulledPerCascade) {
    BRITE::RenderPass pass;
    pass.Post.Hdr = true;
    pass.Post.Samples = 1;
    BRITE::Light sun;
    sun.Direction = {0.0f, -1.0f, 0.0f};
    pass.Lights = {sun};
    pass.AmbientIntensity = SHADOWED;
    pass.Shadows.Enabled = true;
    pass.Shadows.MapSize = 256;
    pass.Shadows.Cascades = 2;
    pass.Shadows.Distance = 20.0f;
    pass.Shadows.Splits[0] = 8.0f;
    pass.ModelCommands = {Lit(Flat(-3, -3, 3, 3, 0)), Lit(Cube(0, 0.1f, 0, 0.1f)), Lit(Cube(0, 7, 0, 0.1f)),
                          Lit(Cube(30, 1, 0, 0.1f))};
    Draw(pass);
    const auto& cascades = m_backend.LastShadowCascades();
    ASSERT_EQ(cascades.size(), 2u);
    EXPECT_FLOAT_EQ(cascades[0].Radius, 4.9375f);
    EXPECT_FLOAT_EQ(cascades[1].Radius, 6.6875f);
    EXPECT_EQ(cascades[0].CasterDraws, 2);
    EXPECT_EQ(cascades[1].CasterDraws, 3);
}

// THE TEXEL SNAP. However the camera slides, each cascade's square sits on whole texels of its own size in the light's
// view, so the map's texels cover the same ground from one frame to the next. Its centre over its texel is a whole
// number (to the float's precision: a few thousand texels at most, so well within 1e-2 of one).
// Mutation: the snap left out -> the centre falls between texels, red.
TEST_F(SunShadow, EachCascadeSitsOnWholeTexels) {
    for (float slide : {0.0f, 0.0137f, 0.31f, 1.777f}) {
        BRITE::RenderPass pass = Pass(Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f)));
        pass.Shadows.Cascades = 2;
        BRITE::Camera3D camera{{slide, 10, 0.5f * slide},
                               {slide, 0, 0.5f * slide},
                               {0, 0, -1},
                               4.0f,
                               BRITE::CameraProjection::Orthographic};
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
        pass.TargetFramebuffer = target;
        pass.Camera3DPtr = &camera;
        m_backend.SubmitRenderPass(pass);
        m_backend.UnloadRenderTexture(target);
        for (const auto& cascade : m_backend.LastShadowCascades()) {
            const float x = cascade.CentreX / cascade.Texel, y = cascade.CentreY / cascade.Texel;
            EXPECT_NEAR(x, std::round(x), 1e-2f) << "slide " << slide;
            EXPECT_NEAR(y, std::round(y), 1e-2f) << "slide " << slide;
        }
    }
}

// THE VARIANTS. The lit shader of a pass without shadows has no shadow code -- not a uniform of it -- and the variant
// with cascades has it; each feature set is compiled once.
// Mutation: every variant compiled with BRITE_SHADOW_CASCADES 0 -> the shadow variant has no shadowMap, red.
TEST_F(SunShadow, APassWithoutShadowsRunsNoShadowCode) {
    EXPECT_FALSE(m_backend.PbrVariantHasUniform(false, 0, "shadowMap"));
    EXPECT_TRUE(m_backend.PbrVariantHasUniform(false, 2, "shadowMap"));
    EXPECT_TRUE(m_backend.PbrVariantHasUniform(false, 2, "cascadeFar[1]"));
    EXPECT_FALSE(m_backend.PbrVariantHasUniform(true, 0, "shadowMap"));
    const std::size_t variants = m_backend.PbrVariantCount();
    m_backend.PbrVariantHasUniform(false, 2, "shadowMap");
    EXPECT_EQ(m_backend.PbrVariantCount(), variants);
}

// THE FADE over the last tenth of the distance. The shaded ground is 10 along the view from the camera (straight down).
// With Distance 10 / 0.95 = 10.5263 the shadow fades by (10.5263 - 10) / (0.1 x 10.5263) = 0.5: half the sun's term
// reaches it, so it reads halfway between the ambient alone (0.25) and the same ground with no shadow at all.
// Mutation: the fade dropped (the shadow full to the distance's end) -> 0.25, red.
TEST_F(SunShadow, TheShadowFadesOverTheLastTenthOfTheDistance) {
    const BRITE::ModelDrawCommand sheet = Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f));
    BRITE::RenderPass unshadowed = Pass(sheet);
    unshadowed.Shadows.Enabled = false;
    const float sunlit = At(Draw(unshadowed), 1.0f, 0.0f);
    BRITE::RenderPass fading = Pass(sheet);
    fading.Shadows.Distance = 10.0f / 0.95f;
    EXPECT_NEAR(At(Draw(fading), 1.0f, 0.0f), (SHADOWED + sunlit) / 2.0f, 0.01f);
}

// THE NORMAL BIAS at a grazing sun. The sun 10 degrees above the horizon, travelling (cos 10, -sin 10, 0), lights the
// bare ground at N.L = sin 10 = 0.1736 -- a surface the map sees edge on, where a shadow map shadows its own receiver
// in stripes (acne). With the default NormalBias (1.5 texels) the bare ground reads as it does with no shadow at all;
// the mean over a patch of 16 x 16 pixels well away from the sheet's shadow is the test.
// It guards the OUTCOME, not the normal bias alone: the depth pass's slope-scaled polygon offset also keeps this
// receiver clean, and the mutant that drops the normal bias from pbr.fs SURVIVES this case (run once). Where the normal
// bias alone decides -- a thin caster close to its receiver -- has no case yet.
TEST_F(SunShadow, TheNormalBiasKeepsAGrazingSunFromShadowingItsOwnReceiver) {
    auto patchMean = [&](BRITE::RenderPass pass) {
        pass.Lights.front().Direction = {std::cos(10.0f * BRITE::Deg2Rad), -std::sin(10.0f * BRITE::Deg2Rad), 0.0f};
        pass.Shadows.DepthBias = 0.0f;
        const std::vector<float> linear = Draw(pass);
        float sum = 0.0f;
        for (int row = 40; row < 56; ++row)
            for (int column = 4; column < 20; ++column)
                sum += linear[static_cast<std::size_t>((SIZE - 1 - row) * SIZE + column) * 4];
        return sum / 256.0f;
    };
    BRITE::RenderPass unshadowed = Pass(Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f)));
    unshadowed.Shadows.Enabled = false;
    const float bare = patchMean(unshadowed);
    EXPECT_NEAR(patchMean(Pass(Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f)))), bare, 0.01f);
}

// THE NORMAL BIAS DECIDES, where the filter reaches farther than the depth pass's offset covers: the same grazing sun
// under ShadowFilter::Poisson, whose sixteen taps reach 2.5 texels from the point (pbr.fs), at 10 and at 5 degrees. A
// tap 2.5 texels across a receiver the light meets at angle t sits 2.5 x tan(90 - t) texels deeper in the map: at 10
// degrees 2.5 x 5.67 = 14.2 texels of depth slope, where the polygon offset (1.5 x slope + 2 units) covers about 8.5
// -- so without the normal bias the bare ground shadows itself. The default NormalBias (1.5 texels, more where the
// light grazes) pushes the point off its surface far enough.
// JUDGED AS A SHARE OF THE SUN'S OWN TERM, since at a grazing sun that term is small: the bare ground takes the ambient
// 0.25 (SHADOWED) and the sun's diffuse (1 - 0.04) / pi x sin t -- at 10 degrees 0.3056 x 0.1736 = 0.0531, at 5 degrees
// 0.0266 -- so the sun's term is what the patch reads above 0.25, and acne is how much of it is lost. At each angle:
//   with the default bias at most a twentieth of the sun's term is lost;
//   with NormalBias 0 more than a tenth is -- the witness that this setup has acne to keep away, so the first check
//   cannot pass for want of any. (Measured on this machine, 22 Nov: a fifth lost at both angles with no bias.)
// Mutation: the normal bias dropped from pbr.fs -> the default loses what NormalBias 0 loses: red.
TEST_F(SunShadow, TheNormalBiasKeepsAPoissonFilteredGrazingSunFromShadowingItsOwnReceiver) {
    for (const float degrees : {10.0f, 5.0f}) {
        auto patchMean = [&](BRITE::RenderPass pass) {
            pass.Lights.front().Direction = {std::cos(degrees * BRITE::Deg2Rad), -std::sin(degrees * BRITE::Deg2Rad),
                                             0.0f};
            pass.Shadows.DepthBias = 0.0f;
            pass.Shadows.Filter = BRITE::ShadowFilter::Poisson;
            const std::vector<float> linear = Draw(pass);
            float sum = 0.0f;
            for (int row = 40; row < 56; ++row)
                for (int column = 4; column < 20; ++column)
                    sum += linear[static_cast<std::size_t>((SIZE - 1 - row) * SIZE + column) * 4];
            return sum / 256.0f;
        };
        BRITE::RenderPass unshadowed = Pass(Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f)));
        unshadowed.Shadows.Enabled = false;
        const float bare = patchMean(unshadowed);
        const float biased = patchMean(Pass(Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f))));
        BRITE::RenderPass unbiased = Pass(Lit(Flat(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f)));
        unbiased.Shadows.NormalBias = 0.0f;
        const float acne = patchMean(unbiased);
        const float sun = bare - SHADOWED;
        ASSERT_GT(sun, 0.01f) << degrees << " degrees: the sun reaches the bare ground (" << bare << ")";
        const float lostBiased = (bare - biased) / sun;
        const float lostUnbiased = (bare - acne) / sun;
        EXPECT_LT(lostBiased, 0.05f) << degrees << " degrees: the default bias keeps the bare ground clean (bare "
                                     << bare << ", biased " << biased << ", " << lostBiased * 100.0f
                                     << "% of the sun lost)";
        EXPECT_GT(lostUnbiased, 0.10f) << degrees << " degrees: with no normal bias the ground shadows itself (bare "
                                       << bare << ", unbiased " << acne << ", " << lostUnbiased * 100.0f
                                       << "% of the sun lost)";
    }
}
