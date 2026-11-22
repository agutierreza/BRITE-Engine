#pragma once

#include <algorithm>
#include <cmath>

// THE TONE CURVE OF AN HDR PASS (RenderPass.hpp, PostSettings), on the CPU. The backend's shaders carry the same
// functions in GLSL (assets/shaders/tone_curve.glsl); these are the ones a caller and a test can reach, and the
// backend uses them for the one colour it hands the GPU itself, the clear colour.
//
// An HDR pass draws its 3D world in LINEAR light into a float target, and shows it through
//     display = Gamma( AcesFit( linear * exposure ) ),   Gamma(y) = y^(1/2.2)
// -- the colour a pixel shows, 0..1, from the light that reached it, 0..infinity. A colour a caller AUTHORS as what
// the screen shows (a clear colour, an unlit material, the fog's tint, a primitive's or a 3D line's tint) is run
// backwards through that curve first, AuthoredToLinear, so it shows exactly as authored however the exposure is set.
namespace BRITE::ToneCurve {

// The curve's constants: Narkowicz's fit of the ACES filmic curve, f(x) = x (a x + b) / (x (c x + d) + e).
inline constexpr float AcesA = 2.51f;
inline constexpr float AcesB = 0.03f;
inline constexpr float AcesC = 2.43f;
inline constexpr float AcesD = 0.59f;
inline constexpr float AcesE = 0.14f;

// The display gamma the engine authors colours in: a colour channel c of 0..1 is the light c^Gamma, as the lit
// shader has always linearised a Color.
inline constexpr float Gamma = 2.2f;

// The fit, held to 0..1. Rising from f(0) = 0, it reaches 1 at a finite light (about 7.24) and is held there.
inline float AcesFit(float x) {
    if (!(x > 0.0f))
        return 0.0f;
    const float y = (x * (AcesA * x + AcesB)) / (x * (AcesC * x + AcesD) + AcesE);
    return std::min(y, 1.0f);
}

// The light the fit maps to y, for y in 0..1 (held there): y (c x^2 + d x + e) = a x^2 + b x is the quadratic
// (a - c y) x^2 + (b - d y) x - e y = 0, whose root at or above zero is taken. a - c y > 0 for every y <= 1, since
// a > c, so the quadratic never degenerates; at y = 0 the root is exactly 0.
inline float InverseAcesFit(float y) {
    y = std::clamp(y, 0.0f, 1.0f);
    const float qa = AcesA - AcesC * y;
    const float qb = AcesB - AcesD * y;
    const float qc = -AcesE * y;
    return (-qb + std::sqrt(qb * qb - 4.0f * qa * qc)) / (2.0f * qa);
}

// A display value 0..1 as the light it is authored to mean, and back.
inline float DisplayToLight(float display) {
    return std::pow(std::clamp(display, 0.0f, 1.0f), Gamma);
}
inline float LightToDisplay(float light) {
    return std::pow(std::max(light, 0.0f), 1.0f / Gamma);
}

// The colour a pixel of linear light shows after an HDR pass's tone map, 0..1, before the 8-bit target's dither.
inline float ToneMap(float linear, float exposure) {
    return LightToDisplay(AcesFit(linear * exposure));
}

// The linear light that shows as `display` (0..1) after ToneMap at this exposure: the inverse of ToneMap. An
// exposure at or below zero is taken as a tiny one, never a division by zero.
inline float AuthoredToLinear(float display, float exposure) {
    return InverseAcesFit(DisplayToLight(display)) / std::max(exposure, 1.0e-4f);
}

} // namespace BRITE::ToneCurve
