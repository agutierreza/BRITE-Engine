#version 330

// BLOOM, the way down: one level of the chain from the one above it, at half its size. The 13-tap filter of Jimenez
// (SIGGRAPH 2014, "Next generation post processing in Call of Duty: Advanced Warfare"): five overlapping 2x2 boxes
// weighted 0.5 (the centre box) and 0.125 (the four corner boxes), so a texel's light is spread evenly and the
// weights sum to 1. On the first level only (prefilter): each box is a Karis average, weighted by 1 / (1 + luma), so
// one very bright texel cannot flicker the whole halo as it crosses pixels; that weighting is undone for the sum; and
// only light above the threshold passes, through a soft knee. Embedded (CMakeLists.txt).

in vec2 uv;
out vec4 o;

uniform sampler2D source;
uniform vec2 texel;   // one texel of the source, in uv
uniform int prefilter;
// The knee as Unity's: threshold, threshold - knee, 2 knee, 0.25 / knee (knee in light, never 0).
uniform vec4 knee;

float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 KarisBox(vec3 a, vec3 b, vec3 c, vec3 d)
{
    vec3 s = (a + b + c + d)*0.25;
    return s/(1.0 + Luma(s));
}

// What of c passes the threshold: none below threshold - knee, all of what is above the threshold beyond
// threshold + knee, and a quadratic between, scaling the colour so its hue is kept.
vec3 Threshold(vec3 c)
{
    float brightness = max(c.r, max(c.g, c.b));
    float soft = clamp(brightness - knee.y, 0.0, knee.z);
    soft = knee.w*soft*soft;
    return c*max(soft, brightness - knee.x)/max(brightness, 0.0001);
}

void main()
{
    vec3 a = texture(source, uv + texel*vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(source, uv + texel*vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(source, uv + texel*vec2( 2.0, -2.0)).rgb;
    vec3 d = texture(source, uv + texel*vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(source, uv).rgb;
    vec3 f = texture(source, uv + texel*vec2( 2.0,  0.0)).rgb;
    vec3 g = texture(source, uv + texel*vec2(-2.0,  2.0)).rgb;
    vec3 h = texture(source, uv + texel*vec2( 0.0,  2.0)).rgb;
    vec3 i = texture(source, uv + texel*vec2( 2.0,  2.0)).rgb;
    vec3 j = texture(source, uv + texel*vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(source, uv + texel*vec2( 1.0, -1.0)).rgb;
    vec3 l = texture(source, uv + texel*vec2(-1.0,  1.0)).rgb;
    vec3 m = texture(source, uv + texel*vec2( 1.0,  1.0)).rgb;

    vec3 r;
    if (prefilter == 1)
    {
        r = KarisBox(j, k, l, m)*0.5 + (KarisBox(a, b, d, e) + KarisBox(b, c, e, f) + KarisBox(d, e, g, h) +
                                        KarisBox(e, f, h, i))*0.125;
        // Undo the weighting for the whole: s / (1 + luma s) is r, so s is r / (1 - luma r).
        r = r/(1.0 - min(Luma(r), 0.999));
        // A half float's light is finite but a sum of them need not be: hold the chain well inside it.
        r = min(Threshold(r), vec3(65000.0));
    }
    else
    {
        r = (j + k + l + m)*0.125 + e*0.125 + (b + d + f + h)*0.0625 + (a + c + g + i)*0.03125;
    }
    o = vec4(r, 1.0);
}
