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

// Jimenez's interleaved gradient noise: a dither pattern with no visible structure, 0..1.
float InterleavedGradientNoise(vec2 p)
{
    return fract(52.9829189*fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main()
{
    vec4 linear = texture(scene, uv);
    if (displayReferred == 1)
    {
        vec3 glow = min(bloomScale*texture(bloom, uv).rgb, vec3(1.0));
        o = vec4(1.0 - (1.0 - clamp(linear.rgb, 0.0, 1.0))*(1.0 - glow), clamp(linear.a, 0.0, 1.0));
        return;
    }
    vec3 c = linear.rgb + bloomScale*texture(bloom, uv).rgb;
    c = ToneMap(c, exposure);
    c += (InterleavedGradientNoise(gl_FragCoord.xy) - 0.5)/255.0;
    // The scene's alpha: the clear colour's where the pass cleared, and where it did not, how much the 3D world drew.
    o = vec4(c, clamp(linear.a, 0.0, 1.0));
}
