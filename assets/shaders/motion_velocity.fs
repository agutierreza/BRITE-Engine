#version 330

// MOTION BLUR'S VELOCITY DRAW: what one fragment of a moving draw writes into the velocity target -- its motion over
// the frame in pixels and a 1 in alpha, "an object's own" -- or of an opted-out draw, an EXEMPT mark. Embedded
// (CMakeLists.txt).
//
// A moving draw writes only where it IS the surface the pass drew: where its depth is the depth the scene left, within
// the slack of one fragment's slope. So what stands in front of it keeps its own motion, a cut-out's cut-away part
// writes nothing, and nothing needs a depth attachment or an alpha test. An exempt draw marks wherever it shows -- at
// or in front of the scene's depth -- so a translucent one, which writes no depth, still marks.

#include "motion_depth.glsl"

in vec4 clipNow;
in vec4 clipBefore;
out vec4 o;

uniform sampler2D sceneDepth;
uniform vec2 size;
uniform int exempt;

void main()
{
    float scene = ViewDistance(texelFetch(sceneDepth, ivec2(gl_FragCoord.xy), 0).r);
    float mine = ViewDistance(gl_FragCoord.z);
    // The slack: four steps of the 24-bit depth buffer at this depth, as a distance -- what the scene's stored depth can
    // be off by -- and twice the fragment's slope across a pixel, which a multisampled scene's resolved depth (one
    // sample of the pixel, not its centre) can be off by. So a surface a few depth steps behind another is told apart.
    float steps = ViewDistance(min(gl_FragCoord.z + 4.0/16777216.0, 1.0)) - mine;
    float slack = 2.0*fwidth(mine) + steps;
    if (exempt == 1) {
        if (mine > scene + slack)
            discard;
        o = vec4(0.0, 0.0, 1.0, 1.0);
        return;
    }
    if (abs(mine - scene) > slack)
        discard;
    // From where it was drawn to where it is, in pixels. A point last frame at or behind the camera has no place on
    // the target to have come from: it is given no motion.
    vec2 motion = vec2(0.0);
    if (clipBefore.w > 1.0e-6)
        motion = (clipNow.xy/clipNow.w - clipBefore.xy/clipBefore.w)*0.5*size;
    o = vec4(motion, 0.0, 1.0);
}
