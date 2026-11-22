// The tone curve of an HDR pass (Backends/ToneCurve.hpp), on the CPU: the ACES fit, its inverse, and the round trip
// that keeps an authored colour showing as authored. No GPU; the GPU's copy of the curve is checked against the same
// hand-worked values through the passes in HdrPassTests.
//
// Every expected value is worked by hand above its assertion, from the fit f(x) = x (2.51 x + 0.03) / (x (2.43 x +
// 0.59) + 0.14) and the display gamma 2.2. Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/ToneCurve.hpp>
#include <gtest/gtest.h>

namespace Tone = BRITE::ToneCurve;

// f(0) = 0 / 0.14 = 0.
// f(1) = 1 (2.51 + 0.03) / (1 (2.43 + 0.59) + 0.14) = 2.54 / 3.16 = 0.8037975.
// f(0.18), mid-grey: 0.18 (0.4518 + 0.03) / (0.18 (0.4374 + 0.59) + 0.14) = 0.086724 / 0.324932 = 0.2668990.
// f reaches 1 where 2.51 x^2 + 0.03 x = 2.43 x^2 + 0.59 x + 0.14, i.e. 0.08 x^2 - 0.56 x - 0.14 = 0:
//   x = (0.56 + sqrt(0.3136 + 0.0448)) / 0.16 = (0.56 + 0.5986652) / 0.16 = 7.241658; beyond it the fit is held at 1.
// Mutation: AcesB 0.03 -> 0.3 -> f(1) = 2.81 / 3.16 = 0.889, red.
TEST(ToneCurve, TheAcesFitAtHandWorkedPoints) {
    EXPECT_FLOAT_EQ(Tone::AcesFit(0.0f), 0.0f);
    EXPECT_NEAR(Tone::AcesFit(1.0f), 0.8037975f, 1e-6f);
    EXPECT_NEAR(Tone::AcesFit(0.18f), 0.2668990f, 1e-6f);
    EXPECT_NEAR(Tone::AcesFit(7.241658f), 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(Tone::AcesFit(100.0f), 1.0f);
    EXPECT_FLOAT_EQ(Tone::AcesFit(-1.0f), 0.0f);
}

// The inverse at y = 0.5: (2.51 - 1.215) x^2 + (0.03 - 0.295) x - 0.07 = 0, i.e. 1.295 x^2 - 0.265 x - 0.07 = 0:
//   x = (0.265 + sqrt(0.070225 + 0.3626)) / 2.59 = (0.265 + 0.6578944) / 2.59 = 0.3563299.
//   Check: f(0.3563299) = 0.3563299 x 0.9243880 / (0.3563299 x 1.4558817 + 0.14) = 0.3293868 / 0.6587757 = 0.5000.
// At y = 1 the root is the 7.241658 worked above; at y = 0 it is 0.
// Mutation: the root taken with the minus sign (-qb - sqrt) -> at 0.5, (0.265 - 0.658) / 2.59 < 0, red.
TEST(ToneCurve, TheInverseUndoesTheFit) {
    EXPECT_NEAR(Tone::InverseAcesFit(0.5f), 0.3563299f, 1e-6f);
    EXPECT_NEAR(Tone::InverseAcesFit(1.0f), 7.241658f, 1e-4f);
    EXPECT_FLOAT_EQ(Tone::InverseAcesFit(0.0f), 0.0f);
    EXPECT_NEAR(Tone::InverseAcesFit(2.0f), 7.241658f, 1e-4f); // held to 1
}

// AuthoredToLinear(display, exposure) = InverseAcesFit(display^2.2) / exposure.
//   Authored white at exposure 1: InverseAcesFit(1) = 7.241658 -- "about 7.24", as PostSettings says.
//   Authored 0.5 at exposure 2: 0.5^2.2 = exp(2.2 ln 0.5) = exp(-1.5249238) = 0.2176376; the inverse there:
//     qa = 2.51 - 2.43 x 0.2176376 = 1.9811406, qb = 0.03 - 0.59 x 0.2176376 = -0.0984062,
//     qc = -0.14 x 0.2176376 = -0.0304693; root = (0.0984062 + sqrt(0.0096838 + 0.2414564)) / 3.9622812
//     = (0.0984062 + 0.5011389) / 3.9622812 = 0.1513134; over the exposure, 0.0756567.
// Mutation: the division by the exposure dropped -> 0.1513 at exposure 2, red.
TEST(ToneCurve, AnAuthoredColourAsTheLightThatShowsIt) {
    EXPECT_NEAR(Tone::AuthoredToLinear(1.0f, 1.0f), 7.241658f, 1e-4f);
    EXPECT_NEAR(Tone::AuthoredToLinear(0.5f, 2.0f), 0.0756567f, 1e-6f);
    EXPECT_FLOAT_EQ(Tone::AuthoredToLinear(0.0f, 1.0f), 0.0f);
}

// The round trip the HDR pass rests on: every 8-bit authored value, at any exposure, tone-maps back to itself.
// ToneMap(AuthoredToLinear(d, e), e) = Gamma(f(e x f^-1(d^2.2) / e)) = Gamma(d^2.2) = d, exactly in real numbers. In
// floats the error is a few ulps through two powers, a square root and a division: well inside 1e-4, a fortieth of
// an 8-bit step (1/255 = 0.0039).
// Mutation: LightToDisplay's power 1 / 2.2 -> 1 / 2.0 -> 0.5 maps to 0.5^1.1 = 0.467, red.
TEST(ToneCurve, EveryAuthoredValueRoundTripsAtEveryExposure) {
    for (float exposure : {0.25f, 1.0f, 3.0f}) {
        for (int c = 0; c <= 255; ++c) {
            const float display = static_cast<float>(c) / 255.0f;
            EXPECT_NEAR(Tone::ToneMap(Tone::AuthoredToLinear(display, exposure), exposure), display, 1e-4f)
                << "channel " << c << " at exposure " << exposure;
        }
    }
}
