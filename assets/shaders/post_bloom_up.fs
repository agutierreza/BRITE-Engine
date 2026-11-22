#version 330

// BLOOM, the way back up: a coarser level blurred by a 3x3 tent (weights 1 2 1 / 2 4 2 / 1 2 1, over 16) and ADDED to
// the finer level it is drawn onto (the blend is one plus one), so each level ends holding its own light plus every
// coarser level's. Embedded (CMakeLists.txt).

in vec2 uv;
out vec4 o;

uniform sampler2D source;
uniform vec2 texel;    // one texel of the source, in uv
uniform float radius;  // the tent's reach, in texels of the source

void main()
{
    vec2 t = texel*radius;
    vec3 s = texture(source, uv).rgb*4.0;
    s += (texture(source, uv + vec2(-t.x, 0.0)).rgb + texture(source, uv + vec2(t.x, 0.0)).rgb +
          texture(source, uv + vec2(0.0, -t.y)).rgb + texture(source, uv + vec2(0.0, t.y)).rgb)*2.0;
    s += texture(source, uv + vec2(-t.x, -t.y)).rgb + texture(source, uv + vec2(t.x, -t.y)).rgb +
         texture(source, uv + vec2(-t.x, t.y)).rgb + texture(source, uv + vec2(t.x, t.y)).rgb;
    o = vec4(s/16.0, 1.0);
}
