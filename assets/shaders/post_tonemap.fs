#version 330

// THE TONE MAP of an HDR pass, onto the pass's own target: the linear picture plus its bloom, through the curve
// (tone_curve.glsl), then a dither of under half an 8-bit step so a sky's gradient does not band. Embedded
// (CMakeLists.txt).

#include "tone_curve.glsl"

in vec2 uv;
out vec4 o;

uniform sampler2D scene;
uniform sampler2D bloom;
// Intensity over the number of levels: the chain's first level holds every level's light summed, so this adds
// Intensity times their average. 0 when the pass has no bloom.
uniform float bloomScale;
uniform float exposure;
// 1: a DISPLAY-REFERRED pass (bloom with Hdr off): the scene holds the colours the screen shows, 0..1, and the bloom is
// SCREENED over them -- 1 - (1 - scene)(1 - bloom), a painter's glow layer -- with no curve and no dither, so a colour
// the bloom does not reach comes out exactly as it went in.
uniform int displayReferred;
// 1: the picture is laid OVER the pass's target (the pass did not clear), and is written premultiplied -- its colour times
// the scene's coverage -- for the chain's ONE, ONE_MINUS_SRC_ALPHA blend; 0: it replaces the target, straight.
uniform int overlay;

// THE SCENE IS PREMULTIPLIED (RaylibRenderBackend.cpp: every draw into it keeps its alpha as coverage). UNDER HDR its
// colour is divided by its alpha before the curve -- the curve is not linear, and must see the light itself -- and
// multiplied back after, when laid over; below this alpha the scene holds only added light -- an additive draw over
// nothing, or the bloom's spill past the world's edge -- which covers nothing: taken as it is and, laid over, ADDED. A
// display-referred pass divides by nothing (below).
const float LEAST_COVERAGE = 1.0/512.0;

vec4 Out(vec3 colour, float coverage)
{
    return overlay == 1 ? vec4(colour*coverage, coverage) : vec4(colour, coverage);
}

// Jimenez's interleaved gradient noise: a dither pattern with no visible structure, 0..1.
float InterleavedGradientNoise(vec2 p)
{
    return fract(52.9829189*fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main()
{
    vec4 held = texture(scene, uv);
    // The scene's alpha: the clear colour's where the pass cleared, and where it did not, how much the 3D world drew.
    float coverage = clamp(held.a, 0.0, 1.0);
    bool covered = coverage >= LEAST_COVERAGE;
    vec3 linear = covered ? held.rgb/coverage : held.rgb;
    if (displayReferred == 1)
    {
        // A DISPLAY-REFERRED pass composes on the premultiplied colour itself, with no divide: with no bloom the held
        // value is exactly what an 8-bit pass would have laid over the target -- added light included -- and the glow is
        // screened within the coverage, P + g max(A - P, 0), the premultiplied form of 1 - (1 - c)(1 - g). So the glow
        // does not reach past what the world covered, and it never takes light away: where added light leaves more
        // colour than coverage (P > A, a spark over nothing or over a thin haze) the glow adds nothing there.
        vec3 glow = min(bloomScale*texture(bloom, uv).rgb, vec3(1.0));
        vec3 premultiplied = clamp(held.rgb, 0.0, 1.0);
        vec3 shown = clamp(premultiplied + glow*max(vec3(coverage) - premultiplied, vec3(0.0)), 0.0, 1.0);
        if (overlay == 1)
            o = vec4(shown, coverage);
        else
            o = vec4(covered ? min(shown/coverage, vec3(1.0)) : shown, coverage);
        return;
    }
    vec3 c = linear + bloomScale*texture(bloom, uv).rgb;
    c = ToneMap(c, exposure);
    // Dithered wherever the picture is written; not where an overlay adds only light over nothing, so a pixel the world
    // left empty adds exactly nothing to the target (ToneMap(0) is 0).
    if (covered || overlay == 0)
        c += (InterleavedGradientNoise(gl_FragCoord.xy) - 0.5)/255.0;
    o = covered ? Out(c, coverage) : vec4(c, coverage);
}
