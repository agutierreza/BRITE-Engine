// PBRMaterial::Blend = Additive: a draw that adds its colour, times its alpha, to what is behind it -- on the GPU under
// a hidden window (HiddenWindowScene.hpp). Two unlit quads of (100, 0, 0) overlap over the middle: x -2..0.5 and
// -0.5..2, so the strip -0.5..0.5 is under both. Beside each case is the mutation that turns it red, and each was run
// once.

#include "HiddenWindowScene.hpp"

using HiddenWindow::At;
using HiddenWindow::AtLinear;
using HiddenWindow::Within;

namespace {

class AdditiveBlend : public HiddenWindow::Scene {
  protected:
    BRITE::ModelDrawCommand Glow(float x0, float x1, float z, BRITE::Color tint) {
        BRITE::ModelDrawCommand draw = Unlit(Quad(x0, -3.0f, x1, 3.0f, z), tint);
        draw.Material.Blend = BRITE::BlendMode::Additive;
        return draw;
    }
};

} // namespace

// In an 8-bit pass over black: one quad is 0 + 100 = 100, the overlap 100 + 100 = 200, outside 0. An alpha of 128
// adds 100 x 128/255 = 50.2: 50 alone and 100 together.
// Mutation: the draw made with the ordinary blend (rlSetBlendMode left out) -> the overlap is 100, red.
TEST_F(AdditiveBlend, AdditiveDrawsAddTheirColourTimesTheirAlpha) {
    BRITE::RenderPass pass;
    pass.ModelCommands = {Glow(-2.0f, 0.5f, 0.0f, {100, 0, 0, 255}), Glow(-0.5f, 2.0f, 0.1f, {100, 0, 0, 255})};
    std::vector<BRITE::Color> pixels = Picture(pass);
    EXPECT_TRUE(Within(pixels[At(-1.0f, 0.0f)], 100, 0, 0, 1));
    EXPECT_TRUE(Within(pixels[At(0.0f, 0.0f)], 200, 0, 0, 1));
    EXPECT_TRUE(Within(pixels[At(1.0f, 0.0f)], 100, 0, 0, 1));

    pass.ModelCommands = {Glow(-2.0f, 0.5f, 0.0f, {100, 0, 0, 128}), Glow(-0.5f, 2.0f, 0.1f, {100, 0, 0, 128})};
    pixels = Picture(pass);
    EXPECT_TRUE(Within(pixels[At(-1.0f, 0.0f)], 50, 0, 0, 1));
    EXPECT_TRUE(Within(pixels[At(0.0f, 0.0f)], 100, 0, 0, 1));
}

// An additive draw is translucent whatever its alpha: drawn after every opaque draw and writing no depth. Here it is
// NEARER the camera and listed FIRST, over an opaque blue (0, 0, 120) behind it: the blue is drawn first and the glow
// adds onto it, (100, 0, 120). Drawn where it is listed and writing its depth, it would hide the blue: (100, 0, 0).
// Mutation: an additive mesh not counted translucent -> (100, 0, 0), red.
TEST_F(AdditiveBlend, AnAdditiveDrawIsTranslucentWhateverItsAlpha) {
    BRITE::RenderPass pass;
    pass.ModelCommands = {Glow(-3.0f, 3.0f, 1.0f, {100, 0, 0, 255}),
                          Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.0f), {0, 0, 120, 255})};
    const std::vector<BRITE::Color> pixels = Picture(pass);
    EXPECT_TRUE(Within(pixels[At(0.0f, 0.0f)], 100, 0, 120, 1));
}

// Under an HDR pass the glows add LINEAR light, past white: two black unlit quads, each with a white emission of
// strength 3, hold 3 alone and 3 + 3 = 6 where they overlap, exactly in a half float.
// Mutation: the same as the first case's -> the overlap holds 3, red.
TEST_F(AdditiveBlend, UnderHdrGlowsAddLightPastWhite) {
    BRITE::ModelDrawCommand left = Glow(-2.0f, 0.5f, 0.0f, {0, 0, 0, 255});
    BRITE::ModelDrawCommand right = Glow(-0.5f, 2.0f, 0.1f, {0, 0, 0, 255});
    for (BRITE::ModelDrawCommand* glow : {&left, &right}) {
        glow->Material.Emission = {255, 255, 255, 255};
        glow->Material.EmissionStrength = 3.0f;
    }
    BRITE::RenderPass pass;
    pass.Post.Hdr = true;
    pass.Post.Samples = 1;
    pass.ModelCommands = {left, right};
    Picture(pass);
    const std::vector<float> linear = LinearScene();
    EXPECT_FLOAT_EQ(linear[AtLinear(-1.0f, 0.0f)], 3.0f);
    EXPECT_FLOAT_EQ(linear[AtLinear(0.0f, 0.0f)], 6.0f);
}
