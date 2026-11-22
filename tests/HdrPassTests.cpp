// An HDR pass (RenderPass.hpp, PostSettings): the 3D world in linear light, tone-mapped onto the pass's target, with
// every colour a caller authors as what the screen shows still showing as authored -- on the GPU under a hidden
// window (HiddenWindowScene.hpp). The curve itself is checked by hand in ToneCurveTests; these cases check that the
// GPU applies it where the header says, and nowhere else.
//
// The 8-bit picture an HDR pass leaves carries a dither of under half a step, so an authored colour reads back within
// one step of itself; the 2D draws after the tone map carry none, and read back exactly. Every pass here draws with
// one sample a pixel, so no edge is averaged. Beside each case is the mutation that turns it red, and each was run
// once.

#include "HiddenWindowScene.hpp"

#include <cmath>
#include <utility>

using HiddenWindow::At;
using HiddenWindow::AtLinear;
using HiddenWindow::Within;

namespace {

class HdrPass : public HiddenWindow::Scene {
  protected:
    static BRITE::RenderPass Hdr(float exposure = 1.0f) {
        BRITE::RenderPass pass;
        pass.Post.Hdr = true;
        pass.Post.Samples = 1;
        pass.Post.Exposure = exposure;
        return pass;
    }
};

constexpr BRITE::Color SKY{51, 102, 204, 255};

} // namespace

// The clear colour, with nothing drawn over it, reads back as authored at exposure 1 and at 3: the backend clears the
// float target to AuthoredToLinear of it, and the tone map undoes exactly that.
// Mutation: the float target cleared to the colour's own 0..1 values -> 51/255 = 0.2 of light shows as
// Gamma(f(0.2)) = Gamma(0.1064 / 0.3552) = 0.29955^(1/2.2) = 0.578, 147 of 255, not 51: red.
TEST_F(HdrPass, TheClearColourShowsAsAuthoredAtAnyExposure) {
    for (float exposure : {1.0f, 3.0f}) {
        BRITE::RenderPass pass = Hdr(exposure);
        pass.ClearColor = SKY;
        const std::vector<BRITE::Color> pixels = Picture(pass);
        EXPECT_TRUE(Within(pixels[At(0.0f, 0.0f)], 51, 102, 204, 1)) << "exposure " << exposure;
    }
}

// Three things authored as what the screen shows, at exposure 2: an unlit quad over the left half (200, 120, 40), a
// primitive cube over the right half (30, 160, 90), and an opaque 3D line across the middle row (250, 40, 200). The
// line lies along y = 0.03125, the centre of row 31 (row r's centre is y = 2 - (r + 0.5) / 16), so it lights that row.
// Mutation: the unlit path writes the authored colour as light (no AuthoredToLinear) -> (200,120,40) shows brighter,
// red. Mutation: the 3D lines drawn outside the authored-colour shader -> the line's 250/255 = 0.98 of light at
// exposure 2
//   shows as Gamma(f(1.96)) = 0.9655^(1/2.2) = 0.984, not as authored: red.
TEST_F(HdrPass, UnlitModelsPrimitivesAndLinesShowAsAuthored) {
    BRITE::RenderPass pass = Hdr(2.0f);
    pass.ModelCommands.push_back(Unlit(Quad(-3.0f, -3.0f, 0.0f, 3.0f, 0.0f), {200, 120, 40, 255}));
    BRITE::Primitive3DDrawCommand cube{};
    cube.Type = BRITE::Primitive3DType::Cube;
    cube.Position = {1.0f, 0.0f, 0.0f};
    cube.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    cube.Scale = {1.0f, 1.0f, 1.0f};
    cube.Size = {2.0f, 4.0f, 0.5f};
    cube.Tint = {30, 160, 90, 255};
    pass.Primitive3DCommands.push_back(cube);
    pass.Line3DCommands.push_back({{-2.0f, 0.03125f, 2.0f}, {2.0f, 0.03125f, 2.0f}, {250, 40, 200, 255}});
    const std::vector<BRITE::Color> pixels = Picture(pass);
    EXPECT_TRUE(Within(pixels[At(-1.0f, 1.0f)], 200, 120, 40, 1));
    EXPECT_TRUE(Within(pixels[At(1.0f, 1.0f)], 30, 160, 90, 1));
    EXPECT_TRUE(Within(pixels[31 * HiddenWindow::SIZE + 16], 250, 40, 200, 1));
}

// A lit quad 5 from the camera under a fog from 1 to 2 is fogged completely, so it shows the fog's tint exactly
// (90, 140, 190), at exposure 0.5, however it is lit.
// Mutation: the HDR fog fades toward fogColor's own values -> 140/255 = 0.55 of light at exposure 0.5 shows as
//   Gamma(f(0.27)) = 0.355^(1/2.2) = 0.625, 159 of 255, not 140: red.
TEST_F(HdrPass, TheFogsTintShowsAsAuthoredBeyondItsEnd) {
    BRITE::RenderPass pass = Hdr(0.5f);
    BRITE::ModelDrawCommand lit = Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.0f), {255, 255, 255, 255});
    lit.Material.Unlit = false;
    pass.ModelCommands.push_back(lit);
    pass.Lights.push_back({});
    pass.AmbientIntensity = 0.3f;
    pass.Fog.Enabled = true;
    pass.Fog.Tint = {90, 140, 190, 255};
    pass.Fog.Start = 1.0f;
    pass.Fog.End = 2.0f;
    const std::vector<BRITE::Color> pixels = Picture(pass);
    EXPECT_TRUE(Within(pixels[At(0.0f, 0.0f)], 90, 140, 190, 1));
}

// Emission past white is kept, on an unlit surface and on a lit one, as linear light in the float target. A white
// emission of strength s adds s x (255/255)^2.2 = s: the unlit quad (left, black albedo, which is 0 of light) holds
// 0 + 20 = 20; the lit quad (right, black albedo, no light and no ambient, so nothing but its emission) holds 6. Both
// are exact in a half float. The same unlit draw in an 8-bit pass ignores its emission, as it always has: black.
// Mutation: the unlit HDR path adds no emission -> the left reads 0, red.
TEST_F(HdrPass, EmissionPastWhiteIsKeptOnUnlitAndLitSurfaces) {
    BRITE::ModelDrawCommand unlit = Unlit(Quad(-3.0f, -3.0f, 0.0f, 3.0f, 0.0f), {0, 0, 0, 255});
    unlit.Material.Emission = {255, 255, 255, 255};
    unlit.Material.EmissionStrength = 20.0f;
    BRITE::ModelDrawCommand lit = Unlit(Quad(0.0f, -3.0f, 3.0f, 3.0f, 0.0f), {0, 0, 0, 255});
    lit.Material.Unlit = false;
    lit.Material.Emission = {255, 255, 255, 255};
    lit.Material.EmissionStrength = 6.0f;

    BRITE::RenderPass pass = Hdr();
    pass.ModelCommands = {unlit, lit};
    Picture(pass);
    const std::vector<float> linear = LinearScene();
    EXPECT_FLOAT_EQ(linear[AtLinear(-1.0f, 0.0f)], 20.0f);
    EXPECT_FLOAT_EQ(linear[AtLinear(1.0f, 0.0f)], 6.0f);

    BRITE::RenderPass eightBit;
    eightBit.ModelCommands = {unlit};
    const std::vector<BRITE::Color> pixels = Picture(eightBit);
    EXPECT_TRUE(Within(pixels[At(-1.0f, 0.0f)], 0, 0, 0, 0));
}

// The exposure scales light and leaves authored colours alone. An unlit black quad with a white emission of strength
// 0.5 (the right half) is 0.5 of light:
//   exposure 1: Gamma(f(0.5)) = Gamma(0.6425 / 1.0425) = 0.616307^(1/2.2) = 0.802519 -> 204.6, 205 of 255;
//   exposure 2: Gamma(f(1.0)) = 0.8037975^(1/2.2) = exp(-0.2184345 / 2.2) = 0.905484 -> 230.9, 231.
// The clear colour on the left shows as authored at both.
// Mutation: the tone map ignores the exposure -> 205 at both, red.
TEST_F(HdrPass, TheExposureScalesLightAndNotWhatIsAuthored) {
    BRITE::ModelDrawCommand glow = Unlit(Quad(0.0f, -3.0f, 3.0f, 3.0f, 0.0f), {0, 0, 0, 255});
    glow.Material.Emission = {255, 255, 255, 255};
    glow.Material.EmissionStrength = 0.5f;
    for (const auto& [exposure, shown] : {std::pair{1.0f, 205}, std::pair{2.0f, 231}}) {
        BRITE::RenderPass pass = Hdr(exposure);
        pass.ClearColor = SKY;
        pass.ModelCommands = {glow};
        const std::vector<BRITE::Color> pixels = Picture(pass);
        EXPECT_TRUE(Within(pixels[At(1.0f, 0.0f)], shown, shown, shown, 1)) << "exposure " << exposure;
        EXPECT_TRUE(Within(pixels[At(-1.0f, 0.0f)], 51, 102, 204, 1)) << "exposure " << exposure;
    }
}

// The 2D draws follow the tone map, onto the pass's target, untouched: a filled rectangle reads back exactly as
// authored, with no dither, over a 3D world that went through the curve.
// Mutation: the 2D draws made inside the float target, before the tone map -> the rectangle is dithered and run
//   through the curve as light (10/255 = 0.039 of light at exposure 1.5 shows as
//   Gamma(f(0.0588)) = 0.0570^(1/2.2) = 0.272, 69 of 255, not 10): red.
TEST_F(HdrPass, TheTwoDimensionalDrawsAreUntouchedByTheCurve) {
    BRITE::RenderPass pass = Hdr(1.5f);
    pass.ClearColor = SKY;
    pass.RectCommands.push_back({{0.0f, 0.0f, 32.0f, 32.0f}, {0.0f, 0.0f}, 0.0f, {10, 200, 30, 255}, true});
    const std::vector<BRITE::Color> pixels = Picture(pass);
    EXPECT_TRUE(Within(pixels[At(-1.0f, 1.0f)], 10, 200, 30, 0));
    EXPECT_TRUE(Within(pixels[At(1.0f, -1.0f)], 51, 102, 204, 1));
}

// BRITE #6's two paths through translucency, under HDR: a translucent quad blends in LINEAR light over what is really
// behind it, whether it is sorted (DepthWrite on) or a layer (off), and whatever order it is listed in. Blue
// (0, 0, 255) at alpha 128 over an opaque red (255, 0, 0), both unlit, listed blue first:
//   red light 7.241658 x (1 - 128/255) = 7.241658 x 0.498039 = 3.606630 -> Gamma(f(3.606630)) = Gamma(32.75777 /
//   33.87685) = 0.966967^(1/2.2) = 0.984847 -> 251.1; blue 7.241658 x 0.501961 = 3.635030 -> 0.985086 -> 251.2.
// So (251, 0, 251), where an 8-bit pass blends to (127, 0, 128).
// Mutation: the translucent quad drawn through the 8-bit path's colours (no AuthoredToLinear on the unlit albedo) ->
//   the blend comes out of the curve far from 251: red.
TEST_F(HdrPass, TranslucentDrawsBlendInLinearLightSortedOrLayered) {
    for (bool layer : {false, true}) {
        BRITE::ModelDrawCommand blue = Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 1.0f), {0, 0, 255, 128});
        blue.Material.DepthWrite = !layer;
        BRITE::RenderPass pass = Hdr();
        pass.ModelCommands = {blue, Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.0f), {255, 0, 0, 255})};
        const std::vector<BRITE::Color> pixels = Picture(pass);
        EXPECT_TRUE(Within(pixels[At(0.0f, 0.0f)], 251, 0, 251, 1)) << (layer ? "layer" : "sorted");
    }
}

// With ShouldClear off the pass's target keeps what it held where the 3D world drew nothing: the target is cleared
// green by an 8-bit pass first, then an HDR pass that does not clear draws an unlit quad over the left half only. The
// left shows the quad as authored; the right is the green, exactly.
// Mutation: the tone map always replaces the target -> the right reads the empty float target, black: red.
TEST_F(HdrPass, WithoutAClearThePictureLiesOverWhatTheTargetHeld) {
    const BRITE::TextureHandle target = m_backend.LoadRenderTexture(HiddenWindow::SIZE, HiddenWindow::SIZE);
    BRITE::RenderPass green;
    green.ClearColor = {0, 255, 0, 255};
    Picture(green, target);
    BRITE::RenderPass pass = Hdr();
    pass.ShouldClear = false;
    pass.ModelCommands.push_back(Unlit(Quad(-3.0f, -3.0f, 0.0f, 3.0f, 0.0f), {200, 120, 40, 255}));
    const std::vector<BRITE::Color> pixels = Picture(pass, target);
    EXPECT_TRUE(Within(pixels[At(-1.0f, 0.0f)], 200, 120, 40, 1));
    EXPECT_TRUE(Within(pixels[At(1.0f, 0.0f)], 0, 255, 0, 0));
    m_backend.UnloadRenderTexture(target);
}

// Off is off. A pass with Hdr unset never makes the chain, and an HDR pass with no 3D camera has no 3D world to draw:
// it draws as an 8-bit pass would (its clear colour exactly, with no dither) and makes no chain either.
// Mutation: HDR taken without a 3D camera -> the chain is made, red.
TEST_F(HdrPass, OffIsOffAndNoCameraMeansNoHdr) {
    BRITE::RenderPass plain;
    plain.ClearColor = SKY;
    plain.ModelCommands.push_back(Unlit(Quad(-3.0f, -3.0f, 0.0f, 3.0f, 0.0f), {200, 120, 40, 255}));
    Picture(plain);
    int width = 0, height = 0;
    std::vector<float> rgba;
    EXPECT_FALSE(m_backend.ReadLastHdrScene(width, height, rgba));

    const BRITE::TextureHandle target = m_backend.LoadRenderTexture(HiddenWindow::SIZE, HiddenWindow::SIZE);
    BRITE::RenderPass noCamera = Hdr();
    noCamera.TargetFramebuffer = target;
    noCamera.ClearColor = SKY;
    m_backend.SubmitRenderPass(noCamera);
    std::vector<BRITE::Color> pixels;
    ASSERT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
    EXPECT_TRUE(Within(pixels[At(0.0f, 0.0f)], 51, 102, 204, 0));
    EXPECT_FALSE(m_backend.ReadLastHdrScene(width, height, rgba));
    m_backend.UnloadRenderTexture(target);
}

// SPECULAR ANTI-ALIASING (PostSettings; pbr.fs). A lit white quad whose normal turns across it -- its corners' normals
// tilted theta = 1.2 rad about y, (-sin, 0, cos) on the left edge and (sin, 0, cos) on the right -- lit by light
// travelling (0, 0, -1), so L = (0, 0, 1). Roughness 0.1, no ambient, one sample a pixel. The pixel read is column 31,
// row 32: x = y = -0.03125.
//
// The normal there: x = (2u - 1) sin 1.2 = -(1/64)(0.932039) = -0.0145631, z = cos 1.2 = 0.362358, normalised:
// N = (-0.0401575, 0, 0.9991934). Its change from column 30 is (0.0795452, 0, 0.0063836) and nothing down the rows, so
// the variance is 0.25 (0.0795452^2 + 0.0063836^2) = 0.0015920 and the kernel 2 x that = 0.0031841: alpha^2 =
// roughness^4 = 1e-4 widens to 0.0032841 (roughness 0.239389).
// The lit shader takes the view from the camera's POSITION, orthographic or not: V = normalize(0.03125, 0.03125, 5) =
// (0.0062498, 0.0062498, 0.9999609), H = normalize(V + L) = (0.0031249, 0.0031249, 0.9999902). Then N.H = 0.9990581,
// N.V = 0.9989034, N.L = 0.9991934, H.V = 0.9999902, so F = 0.04 + 0.96 (1 - H.V)^5 = 0.04.
//   GGX: d = (N.H)^2 (alpha^2 - 1) + 1 = 0.0051608, D = alpha^2 / (pi d^2) = 39.25;
//   G = 0.99963 (k = 1.239389^2 / 8 = 0.19201); specular = D G F / (4 N.V N.L) = 0.3931;
//   diffuse (1 - F) / pi = 0.30558; (0.30558 + 0.3931) x N.L = 0.6981.
// Without the widening alpha^2 stays 1e-4: d = 0.0019827, D = 8.097, specular 0.0811, the light 0.3864. The tolerance,
// 0.05, is 7% of the answer -- the GPU's derivative is a difference across a 2x2 quad, and a half float's step near
// 0.7 is 2^-11 -- and the two answers are 0.31 apart.
// Mutation: the kernel dropped from pbr.fs -> 0.386, red.
TEST_F(HdrPass, AGlossyHighlightIsWidenedWhereTheNormalTurnsFasterThanAPixel) {
    const float s = std::sin(1.2f), c = std::cos(1.2f);
    const BRITE::ModelHandle bent =
        Quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.0f, {{-s, 0.0f, c}, {s, 0.0f, c}, {s, 0.0f, c}, {-s, 0.0f, c}});
    BRITE::ModelDrawCommand glossy = Unlit(bent, {255, 255, 255, 255});
    glossy.Material.Unlit = false;
    glossy.Material.DoubleSided = false;
    glossy.Material.Roughness = 0.1f;
    BRITE::RenderPass pass = Hdr();
    BRITE::Light sun;
    sun.Direction = {0.0f, 0.0f, -1.0f};
    pass.Lights = {sun};
    pass.ModelCommands = {glossy};
    Picture(pass);
    const std::vector<float> linear = LinearScene();
    const std::size_t pixel = static_cast<std::size_t>((HiddenWindow::SIZE - 1 - 32) * HiddenWindow::SIZE + 31) * 4;
    EXPECT_NEAR(linear[pixel], 0.6981f, 0.05f);
}

// THE DEFAULT, MULTISAMPLED HDR PASS (Samples 4) is drawn into the chain's multisampled surface and resolved into the
// float target before anything reads it. A field of white emission of strength 6 over the whole picture: every sample
// of every pixel is 6 of light, so the resolve averages four sixes to 6 exactly.
// Mutation: EndScene resolves nothing -> the float target is never drawn: 0, red.
TEST_F(HdrPass, TheMultisampledPassIsResolvedBeforeItIsRead) {
    BRITE::ModelDrawCommand field = Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.0f), {0, 0, 0, 255});
    field.Material.Emission = {255, 255, 255, 255};
    field.Material.EmissionStrength = 6.0f;
    BRITE::RenderPass pass = Hdr();
    pass.Post.Samples = 4;
    pass.ModelCommands = {field};
    Picture(pass);
    EXPECT_FLOAT_EQ(LinearScene()[AtLinear(0.0f, 0.0f)], 6.0f);
}

// Two HDR passes a frame at different sizes -- a scene and a half-size view of it -- each keep their own float target:
// three frames of both make two targets, not one per pass. And each still draws its own picture.
// Mutation: a set at another size freed instead of parked -> six targets made, red.
TEST_F(HdrPass, PassesAtTwoSizesEachKeepTheirOwnTargets) {
    const BRITE::TextureHandle half = m_backend.LoadRenderTexture(HiddenWindow::SIZE / 2, HiddenWindow::SIZE / 2);
    for (int frame = 0; frame < 3; ++frame) {
        BRITE::RenderPass full = Hdr();
        full.ClearColor = SKY;
        const std::vector<BRITE::Color> big = Picture(full);
        EXPECT_TRUE(Within(big[At(0.0f, 0.0f)], 51, 102, 204, 1));
        BRITE::RenderPass small = Hdr();
        small.ClearColor = {200, 120, 40, 255};
        Picture(small, half);
    }
    EXPECT_EQ(m_backend.HdrSceneTargetsMade(), 2);
    m_backend.UnloadRenderTexture(half);
}

// A SET NO PASS DRAWS INTO IS FREED, so a resize or a change of full screen does not keep the old size's targets for
// the application's life. A full-size and a half-size HDR pass make two sets. Then the half-size one is not drawn
// again: for IdlePasses + 10 rounds, a plain (not HDR) pass and the full-size HDR pass. Counted by hand: the half set
// was last used on pass 2; the rounds are passes 3 to 2 + 2 (IdlePasses + 10), so by the last round it has stood unused
// for 2 (IdlePasses + 10) passes, more than IdlePasses: freed. The full set is drawn into every second pass, never idle
// for more than one: kept, and never remade (two targets made in all). A half-size pass after that makes a third.
// Mutation: ForgetIdle not called -> two sets held, red. Mutation: a set freed after any pass it was not drawn in
// (IdlePasses 0) -> the full set is remade every round, far more than two targets made, red.
TEST_F(HdrPass, ASetNoPassDrawsIntoIsFreed) {
    const BRITE::TextureHandle half = m_backend.LoadRenderTexture(HiddenWindow::SIZE / 2, HiddenWindow::SIZE / 2);
    BRITE::RenderPass full = Hdr();
    full.ClearColor = SKY;
    Picture(full);
    BRITE::RenderPass small = Hdr();
    small.ClearColor = {200, 120, 40, 255};
    Picture(small, half);
    EXPECT_EQ(m_backend.HdrTargetSetsHeld(), 2);
    const std::uint64_t rounds = BRITE::Backends::Raylib::RaylibRenderBackend::HdrIdlePasses() + 10;
    for (std::uint64_t round = 0; round < rounds; ++round) {
        BRITE::RenderPass plain;
        plain.TargetFramebuffer = half;
        m_backend.SubmitRenderPass(plain);
        BRITE::RenderPass again = Hdr();
        again.ClearColor = SKY;
        Picture(again);
    }
    EXPECT_EQ(m_backend.HdrTargetSetsHeld(), 1) << "the half-size set, idle, is freed; the full-size set is kept";
    EXPECT_EQ(m_backend.HdrSceneTargetsMade(), 2) << "the full-size set, drawn into every second pass, is never remade";
    BRITE::RenderPass back = Hdr();
    back.ClearColor = {200, 120, 40, 255};
    const std::vector<BRITE::Color> pixels = Picture(back, half);
    EXPECT_TRUE(Within(pixels[0], 200, 120, 40, 1)) << "the half size draws again, into a set made anew";
    EXPECT_EQ(m_backend.HdrSceneTargetsMade(), 3);
    EXPECT_EQ(m_backend.HdrTargetSetsHeld(), 2);
    m_backend.UnloadRenderTexture(half);
}

// A TRANSLUCENT LAYER IN A PASS THAT DOES NOT CLEAR LIES OVER THE TARGET BY ITS OWN ALPHA, under HDR and under the
// display-referred bloom alike: the chain's target is premultiplied (RenderPass.hpp, PostSettings). The target is
// cleared to grey (128, 128, 128) by an 8-bit pass; then a chained pass that does not clear draws one white unlit layer
// at alpha 26 (a = 26/255 = 0.101961) and nothing else. Worked by hand:
//   the chain's target: colour 1 x a = 0.101961 and, as coverage, alpha a + 0 x (1 - a) = 0.101961;
//   the composite: the colour divided by the coverage is white, through the curve white again (or screened with no
//   bloom, white), written as white x a; blended ONE, ONE_MINUS_SRC_ALPHA over the grey 128/255 = 0.501961:
//   0.101961 + 0.501961 x 0.898039 = 0.552741, 140.95 of 255: 141 -- what an 8-bit pass gives, 128 + 127 a = 140.95.
// Mutation: the chain's alpha blended as raylib blends it (SRC_ALPHA, ONE_MINUS_SRC_ALPHA on the alpha too) -> the
// coverage is a x a = 0.010396. Under HDR the tone map divides 0.101961 by it (9.81, white through the curve),
// multiplies back by 0.010396, and the composite 0.010396 + 0.501961 x 0.989604 = 0.5071: 129, red. Display-referred,
// with no divide, 0.101961 + 0.501961 x 0.989604 = 0.6, 153, red. Mutation: the composite laid over by SRC_ALPHA again
// (the review's old path) -> 0.101961 x 0.101961 + 0.501961 x 0.898039 = 0.4612: 118, red.
TEST_F(HdrPass, ALayerInAPassThatDoesNotClearLiesOverTheTargetByItsAlpha) {
    for (bool hdr : {true, false}) {
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(HiddenWindow::SIZE, HiddenWindow::SIZE);
        BRITE::RenderPass grey;
        grey.ClearColor = {128, 128, 128, 255};
        Picture(grey, target);
        BRITE::RenderPass pass = Hdr();
        pass.Post.Hdr = hdr;
        if (!hdr) {
            pass.Post.Bloom.Enabled = true; // the display-referred road through the chain
            pass.Post.Bloom.Intensity = 0.0f;
        }
        pass.ShouldClear = false;
        pass.ModelCommands.push_back(Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.0f), {255, 255, 255, 26}));
        const BRITE::Color c = Picture(pass, target)[At(0.0f, 0.0f)];
        EXPECT_TRUE(Within(c, 141, 141, 141, 1))
            << (hdr ? "hdr " : "display ") << int(c.r) << " " << int(c.g) << " " << int(c.b);
        m_backend.UnloadRenderTexture(target);
    }
}

// ADDED LIGHT OVER A THIN LAYER, DISPLAY-REFERRED, IS EXACTLY AN 8-BIT PASS'S, laid over a target the pass did not
// clear. The target is cleared dark, (20, 20, 20), by an 8-bit pass; a display-referred pass (Bloom on, no bloom
// reaching: Intensity 0) that does not clear draws a white haze at alpha 3 (a = 0.011765) and over it an ADDITIVE spark
// of (100, 100, 100). By hand, an 8-bit pass: 20/255 = 0.078431, the haze 0.078431 + 0.921569 x 0.011765 = 0.089273,
// the spark adds 100/255 = 0.392157: 0.481430, 122.8 of 255: 123. Through the chain: premultiplied 0.011765 + 0.392157
// = 0.403922 at coverage 0.011765, laid over by ONE, ONE_MINUS_SRC_ALPHA: 0.403922 + 0.078431 x 0.988235 = 0.481430,
// the same 123. Mutation: the display path divides by the coverage again (engine-fixes-2's) -> 0.403922 / 0.011765 is
// clipped to 1 and multiplied back to 0.011765: 0.011765 + 0.077508 = 0.089273, 23, red.
TEST_F(HdrPass, AddedLightOverAThinLayerLiesOverTheTargetAsAnEightBitPassLaysIt) {
    const BRITE::TextureHandle target = m_backend.LoadRenderTexture(HiddenWindow::SIZE, HiddenWindow::SIZE);
    BRITE::RenderPass dark;
    dark.ClearColor = {20, 20, 20, 255};
    Picture(dark, target);
    BRITE::RenderPass pass = Hdr();
    pass.Post.Hdr = false;
    pass.Post.Bloom.Enabled = true;
    pass.Post.Bloom.Intensity = 0.0f;
    pass.ShouldClear = false;
    BRITE::ModelDrawCommand spark = Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.5f), {100, 100, 100, 255});
    spark.Material.Blend = BRITE::BlendMode::Additive;
    pass.ModelCommands = {Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.25f), {255, 255, 255, 3}), spark};
    const BRITE::Color c = Picture(pass, target)[At(0.0f, 0.0f)];
    EXPECT_TRUE(Within(c, 123, 123, 123, 1)) << int(c.r) << " " << int(c.g) << " " << int(c.b);
    m_backend.UnloadRenderTexture(target);
}

// THE GLOW NEVER TAKES LIGHT AWAY: an additive spark of (100, 100, 100) over nothing, in a display-referred pass that
// does not clear, over a dark target (20, 20, 20), with a bloom that reaches it (Threshold 0.2, no knee, Intensity 1:
// the spark's 0.392 is above the threshold everywhere). With no bloom it shows 20/255 + 100/255 = 0.470588, 120. With
// the bloom the spark's premultiplied colour P = 0.392157 stands over coverage A = 0 (added light covers nothing), so
// the screen adds g max(A - P, 0) = 0 and the pixel shows the same 120 -- at least what it shows with no bloom.
// Mutation: the screen as P + g (A - P) (engine-fixes-3's, blocked) -> the spark becomes P (1 - g), darker than with no
// bloom, red.
TEST_F(HdrPass, TheGlowNeverTakesLightAwayFromAnAddedSpark) {
    for (float intensity : {0.0f, 1.0f}) {
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(HiddenWindow::SIZE, HiddenWindow::SIZE);
        BRITE::RenderPass dark;
        dark.ClearColor = {20, 20, 20, 255};
        Picture(dark, target);
        BRITE::RenderPass pass = Hdr();
        pass.Post.Hdr = false;
        pass.Post.Bloom.Enabled = true;
        pass.Post.Bloom.Threshold = 0.2f;
        pass.Post.Bloom.Knee = 0.0f;
        pass.Post.Bloom.Intensity = intensity;
        pass.ShouldClear = false;
        BRITE::ModelDrawCommand spark = Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.5f), {100, 100, 100, 255});
        spark.Material.Blend = BRITE::BlendMode::Additive;
        pass.ModelCommands = {spark};
        const BRITE::Color c = Picture(pass, target)[At(0.0f, 0.0f)];
        EXPECT_TRUE(Within(c, 120, 120, 120, 1))
            << "intensity " << intensity << ": " << int(c.r) << " " << int(c.g) << " " << int(c.b);
        m_backend.UnloadRenderTexture(target);
    }
}

// A BACKGROUND CALLBACK'S BLEND DOES NOT REACH THE 3D WORLD: a callback that ends on raylib's straight alpha blend, in
// a chained pass that does not clear, leaves the white layer at alpha 26 over grey 128 at the 141 of the case above
// (ALayerInAPassThatDoesNotClearLiesOverTheTargetByItsAlpha), HDR and display-referred alike.
// Mutation: the chain's blend not set again after the callbacks -> the layer's alpha builds as a x a, and the composite
// comes out near 129 (the worked value above), red.
TEST_F(HdrPass, ABackgroundCallbacksBlendDoesNotReachTheWorld) {
    for (bool hdr : {true, false}) {
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(HiddenWindow::SIZE, HiddenWindow::SIZE);
        BRITE::RenderPass grey;
        grey.ClearColor = {128, 128, 128, 255};
        Picture(grey, target);
        BRITE::RenderPass pass = Hdr();
        pass.Post.Hdr = hdr;
        if (!hdr) {
            pass.Post.Bloom.Enabled = true;
            pass.Post.Bloom.Intensity = 0.0f;
        }
        pass.ShouldClear = false;
        pass.BackgroundDrawCallbacks.push_back([] { ::BeginBlendMode(BLEND_ALPHA); });
        pass.ModelCommands.push_back(Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.0f), {255, 255, 255, 26}));
        const BRITE::Color c = Picture(pass, target)[At(0.0f, 0.0f)];
        EXPECT_TRUE(Within(c, 141, 141, 141, 1))
            << (hdr ? "hdr " : "display ") << int(c.r) << " " << int(c.g) << " " << int(c.b);
        m_backend.UnloadRenderTexture(target);
    }
}

// A CLEAR COLOUR WITH ALPHA BELOW 1 KEEPS ITS COLOUR: the chain's target is premultiplied, so the clear is stored as
// light x alpha and divided by it again in the tone map; the picture replaces the target with the colour as authored
// and the alpha as authored. SKY at alpha 128 reads back (51, 102, 204) within the dither's step, alpha 128 exactly.
// Mutation: the clear stored straight (not times its alpha) -> the tone map divides the light by 0.502 and shows it
// twice as bright, far from (51, 102, 204): red.
TEST_F(HdrPass, AClearColourWithAlphaBelowOneKeepsItsColour) {
    BRITE::RenderPass pass = Hdr();
    pass.ClearColor = {51, 102, 204, 128};
    const BRITE::Color c = Picture(pass)[At(0.0f, 0.0f)];
    EXPECT_TRUE(Within(c, 51, 102, 204, 1)) << int(c.r) << " " << int(c.g) << " " << int(c.b);
    EXPECT_EQ(int(c.a), 128);
}

// A TRANSLUCENT LAYER UNDER HDR BLENDS AS LIGHT, and this pins how far it is from the 8-bit pass (PostSettings says
// so). A white unlit layer at alpha 26 (a = 0.101961) over an unlit dark (20, 30, 60), at exposure 1:
//   red: 20/255 = 0.078431, ^2.2 = 0.0036966, InverseAcesFit = (-0.027819 + sqrt(0.00077390 + 0.0051773)) / 5.002034
//     = 0.0098610; white is 7.241658; blended 0.0098610 x 0.898039 + 7.241658 x 0.101961 = 0.747222; the fit
//     1.423850 / 1.937629 = 0.734842, ^(1/2.2) = 0.869317: 221.7.
//   green: 30/255 -> 0.018110 of light -> blended 0.754629 -> 0.737421 -> 0.870705: 222.0.
//   blue: 60/255 -> 0.047940 -> blended 0.781418 -> 0.746384 -> 0.875500: 223.3.
// So (222, 222, 223), where the 8-bit pass blends 20 + 235 a = 43.96, 30 + 225 a = 52.94, 60 + 195 a = 79.88: (44, 53,
// 80). Mutation: translucent unlit layers blended through the curve as authored (no AuthoredToLinear) -> near (44, 53,
// 80)
//   under HDR too, red.
TEST_F(HdrPass, ATranslucentLayerBlendsAsLightNotAsPaint) {
    for (bool hdr : {true, false}) {
        BRITE::RenderPass pass = Hdr();
        pass.Post.Hdr = hdr;
        pass.ModelCommands = {Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.0f), {20, 30, 60, 255}),
                              Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.5f), {255, 255, 255, 26})};
        const BRITE::Color c = Picture(pass)[At(0.0f, 0.0f)];
        if (hdr)
            EXPECT_TRUE(Within(c, 222, 222, 223, 1)) << int(c.r) << " " << int(c.g) << " " << int(c.b);
        else
            EXPECT_TRUE(Within(c, 44, 53, 80, 1)) << int(c.r) << " " << int(c.g) << " " << int(c.b);
    }
}
