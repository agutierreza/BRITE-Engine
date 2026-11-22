// Bloom of several passes (RenderPass.hpp, BloomSettings) in an HDR pass, on the GPU under a hidden window
// (HiddenWindowScene.hpp).
//
// The hand-worked cases draw a UNIFORM field of light over the whole target: an unlit black quad with a white emission
// of strength v, which is v of light everywhere (HdrPassTests shows emission is kept exactly). Every filter in the
// chain sums its weights to 1 -- the prefilter's five Karis boxes (0.5 + 4 x 0.125), the plain 13-tap (4 x 0.125 +
// 0.125 + 4 x 0.0625 + 4 x 0.03125), the 3x3 tent (4 + 4 x 2 + 4) / 16 -- and the edges are clamped, so a uniform field
// stays uniform through it: each level holds the light the threshold passes, P(v), and on the way up each level adds
// the one below, so the first level holds Levels x P(v). The Karis weight of a grey box of v is undone exactly:
// v / (1 + v) / (1 - v / (1 + v)) = v. With Threshold T = 1 and Knee 0.5 (0.5 of light):
//   P(v) = 0                                  for v <= T - 0.5 = 0.5;
//   P(v) = 0.25 / 0.5 x (v - 0.5)^2           between, so P(1) = 0.5 x 0.25 = 0.125;
//   P(v) = v - T                              from T + 0.5 = 1.5 on, so P(4) = 3.
// The tone map adds Intensity / Levels of the first level, Intensity times the levels' average. Three levels on a 64
// pixel target are 32, 16 and 8 a side; each value below is exact in the chain's 11-bit floats.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include "HiddenWindowScene.hpp"

using HiddenWindow::At;
using HiddenWindow::Within;

namespace {

class Bloom : public HiddenWindow::Scene {
  protected:
    /// An HDR pass of a uniform field of `light`, blooming as above when `bloom`.
    std::vector<BRITE::Color> Field(float light, bool bloom, float exposure) {
        BRITE::ModelDrawCommand field = Unlit(Quad(-3.0f, -3.0f, 3.0f, 3.0f, 0.0f), {0, 0, 0, 255});
        field.Material.Emission = {255, 255, 255, 255};
        field.Material.EmissionStrength = light;
        BRITE::RenderPass pass;
        pass.Post.Hdr = true;
        pass.Post.Samples = 1;
        pass.Post.Exposure = exposure;
        pass.Post.Bloom.Enabled = bloom;
        pass.Post.Bloom.Levels = 3;
        pass.Post.Bloom.Threshold = 1.0f;
        pass.Post.Bloom.Knee = 0.5f;
        pass.Post.Bloom.Intensity = 0.25f;
        pass.ModelCommands = {field};
        return Picture(pass);
    }

    /// The bloom's first level at its middle texel: every level's light summed.
    float FirstLevel() {
        int width = 0, height = 0;
        std::vector<float> rgba;
        EXPECT_TRUE(m_backend.ReadLastBloom(width, height, rgba));
        EXPECT_EQ(width, HiddenWindow::SIZE / 2);
        return rgba[static_cast<std::size_t>((height / 2) * width + width / 2) * 4];
    }
};

} // namespace

// A field of 4: the first level holds 3 levels x P(4) = 3 x 3 = 9. The picture is 4 + 0.25 / 3 x 9 = 4.75 of light;
// at exposure 0.1, Gamma(f(0.475)) = Gamma(0.580569 / 0.968519) = 0.599440^(1/2.2) = exp(-0.511758 / 2.2) = 0.792471,
// 202.1 of 255. Without bloom, 4 of light: Gamma(f(0.4)) = Gamma(0.4136 / 0.7648) = 0.540795^(1/2.2) = 0.756233, 192.8.
// Mutation: the corner boxes weighted 0.25 instead of 0.125 -> the prefilter sums to 1.5: 4.5 a level, red.
// Mutation: the levels' sum added whole (no division by Levels) -> 4 + 0.25 x 9 = 6.25 of light: Gamma(f(0.625)) =
// 0.842, 215, red.
TEST_F(Bloom, AUniformFieldAboveTheThresholdBloomsByHand) {
    const std::vector<BRITE::Color> bloomed = Field(4.0f, true, 0.1f);
    EXPECT_FLOAT_EQ(FirstLevel(), 9.0f);
    EXPECT_TRUE(Within(bloomed[At(0.0f, 0.0f)], 202, 202, 202, 1));
    const std::vector<BRITE::Color> plain = Field(4.0f, false, 0.1f);
    EXPECT_TRUE(Within(plain[At(0.0f, 0.0f)], 193, 193, 193, 1));
}

// The knee: a field of exactly the threshold, 1, passes P(1) = 0.125 a level, 0.375 summed over three -- light
// switching on softly, not at the threshold. A field of 0.4, under the knee's start at 0.5, passes nothing: the
// picture is what it is without bloom, Gamma(f(0.4)) at exposure 1 = 0.756233, 192.8.
// Mutation: the knee's quadratic coefficient 0.25 / knee taken as 0.5 / knee -> 0.25 a level, 0.75 summed: red.
TEST_F(Bloom, TheKneeStartsBloomSoftlyAndNothingBelowItBlooms) {
    Field(1.0f, true, 1.0f);
    EXPECT_FLOAT_EQ(FirstLevel(), 0.375f);
    const std::vector<BRITE::Color> dim = Field(0.4f, true, 1.0f);
    EXPECT_FLOAT_EQ(FirstLevel(), 0.0f);
    EXPECT_TRUE(Within(dim[At(0.0f, 0.0f)], 193, 193, 193, 1));
}

// A bright point spreads. A quad 4 x 4 pixels at the middle, of 50 of light, on black: 12 pixels from it the picture
// is black without bloom (the dither cannot lift 0 to half a step) and lit with it, by the halo of the coarse levels.
// Mutation: the tone map adds none of the bloom (its scale 0) -> black 12 pixels out, red.
TEST_F(Bloom, ABrightPointSpreadsIntoAHalo) {
    BRITE::ModelDrawCommand point = Unlit(Quad(-0.125f, -0.125f, 0.125f, 0.125f, 0.0f), {0, 0, 0, 255});
    point.Material.Emission = {255, 255, 255, 255};
    point.Material.EmissionStrength = 50.0f;
    for (bool bloom : {false, true}) {
        BRITE::RenderPass pass;
        pass.Post.Hdr = true;
        pass.Post.Samples = 1;
        pass.Post.Bloom.Enabled = bloom;
        pass.Post.Bloom.Levels = 5;
        pass.Post.Bloom.Intensity = 0.5f;
        pass.ModelCommands = {point};
        const std::vector<BRITE::Color> pixels = Picture(pass);
        const BRITE::Color away = pixels[At(0.75f, 0.0f)];
        if (bloom)
            EXPECT_GT(away.r, 2) << "a halo 12 pixels out";
        else
            EXPECT_EQ(away.r, 0);
    }
}
