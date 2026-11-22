// THE TONE CURVE OF AN HDR PASS, in GLSL: the same functions as include/Backends/ToneCurve.hpp, which says what they
// mean. Not a shader of its own: CMakeLists.txt puts this file in place of the line
//     #include "tone_curve.glsl"
// in every embedded shader that has one, at configure time, so the curve is written once for the GPU.

const float ACES_A = 2.51;
const float ACES_B = 0.03;
const float ACES_C = 2.43;
const float ACES_D = 0.59;
const float ACES_E = 0.14;
const float DISPLAY_GAMMA = 2.2;

vec3 AcesFit(vec3 x)
{
    x = max(x, vec3(0.0));
    return min((x*(ACES_A*x + ACES_B))/(x*(ACES_C*x + ACES_D) + ACES_E), vec3(1.0));
}

vec3 InverseAcesFit(vec3 y)
{
    y = clamp(y, 0.0, 1.0);
    vec3 qa = ACES_A - ACES_C*y;
    vec3 qb = ACES_B - ACES_D*y;
    vec3 qc = -ACES_E*y;
    return (-qb + sqrt(qb*qb - 4.0*qa*qc))/(2.0*qa);
}

// A colour authored as what the screen shows (0..1) as the linear light that shows it at this exposure.
vec3 AuthoredToLinear(vec3 display, float exposure)
{
    return InverseAcesFit(pow(clamp(display, 0.0, 1.0), vec3(DISPLAY_GAMMA)))/max(exposure, 0.0001);
}

// Linear light as the colour it shows, 0..1, before the dither.
vec3 ToneMap(vec3 linear, float exposure)
{
    return pow(AcesFit(linear*exposure), vec3(1.0/DISPLAY_GAMMA));
}
