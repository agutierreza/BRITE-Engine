#version 330

// PRIMITIVES AND 3D LINES UNDER AN HDR PASS: raylib's default fragment stage -- texture times colDiffuse times the
// vertex colour -- with the colour run back through the tone curve, so a tint authored as what the screen shows still
// shows so after the pass's tone map (tone_curve.glsl). Alpha is kept as authored. Embedded (CMakeLists.txt).

#include "tone_curve.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float exposure;

void main()
{
    vec4 c = texture(texture0, fragTexCoord)*colDiffuse*fragColor;
    finalColor = vec4(AuthoredToLinear(c.rgb, exposure), c.a);
}
