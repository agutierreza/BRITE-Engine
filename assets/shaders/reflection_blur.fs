#version 330

// THE PLANAR REFLECTION'S BLUR, one pass (RenderPass.hpp, ReflectionBlurSettings; ReflectionBlur.hpp): a Gaussian along
// one axis, `taps` reads either side of the pixel, each one texel of the level read apart, weighted exp(-i^2 / 2 s^2)
// and normalised by their sum, so the light is kept whatever the spread. Light is read as it is stored -- linear and
// beyond 1 in an HDR pass's float target -- held finite, as the bloom holds it, so one pixel that is infinite or not a
// number does not poison the streak. Embedded (CMakeLists.txt).

in vec2 uv;
out vec4 o;

uniform sampler2D source;
uniform vec2 step;     // one tap, in uv: a texel of the level read, along the pass's axis
uniform float lod;     // the level read
uniform float spread;  // the Gaussian's standard deviation, in taps
uniform int taps;      // reads either side

vec4 Sample(vec2 at)
{
    vec4 c = textureLod(source, at, lod);
    if (any(isnan(c))) return vec4(0.0);
    return clamp(c, 0.0, 65000.0);
}

void main()
{
    vec4 sum = Sample(uv);
    float total = 1.0;
    float falloff = -0.5/max(spread*spread, 1.0e-6);
    for (int i = 1; i <= taps; ++i)
    {
        float w = exp(falloff*float(i*i));
        sum += w*(Sample(uv + step*float(i)) + Sample(uv - step*float(i)));
        total += 2.0*w;
    }
    o = sum/total;
}
