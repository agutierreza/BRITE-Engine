#version 330

// MOTION BLUR'S MOTION: each pixel's streak, full-screen -- its object's motion where a moving draw wrote one, else the
// camera's, from the depth the scene left: the point drawn there now, taken back through last frame's view-projection
// (MotionBlur.hpp does the same on the CPU). Then times the shutter and held to the longest streak. Writes the streak
// (pixels, +y up), 1 for an exempt pixel, and the distance along the view the gather weighs by. Embedded
// (CMakeLists.txt).

#include "motion_depth.glsl"

in vec2 uv;
out vec4 o;

uniform sampler2D velocity;
uniform sampler2D sceneDepth;
// Last frame's view-projection times the inverse of this frame's: a point's clip position now to its clip position
// then. Used only when cameraMoves.
uniform mat4 reprojection;
uniform int cameraMoves;
uniform vec2 size;
uniform float shutter;
uniform float maxLength;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 own = texelFetch(velocity, p, 0);
    float depth = texelFetch(sceneDepth, p, 0).r;
    vec2 motion = vec2(0.0);
    float exempt = 0.0;
    if (own.a > 0.5) {
        if (own.b > 0.5)
            exempt = 1.0;
        else
            motion = own.xy;
    } else {
        // An exempt draw's edge: a pixel it covers only in part holds some of its colour but no mark -- its centre lies
        // outside the draw -- so a pixel no draw wrote motion for, beside a marked one, is exempt too. Else that fringe
        // streaks the draw's colour out a pixel's width.
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                vec4 beside = texelFetch(velocity, clamp(p + ivec2(x, y), ivec2(0), ivec2(size) - 1), 0);
                if (beside.a > 0.5 && beside.b > 0.5)
                    exempt = 1.0;
            }
        if (exempt < 0.5 && cameraMoves == 1) {
            vec4 now = vec4((gl_FragCoord.xy/size)*2.0 - 1.0, depth*2.0 - 1.0, 1.0);
            vec4 before = reprojection*now;
            if (before.w > 1.0e-6)
                motion = (now.xy - before.xy/before.w)*0.5*size;
        }
    }
    motion *= shutter;
    float length2 = dot(motion, motion);
    if (length2 > maxLength*maxLength)
        motion *= maxLength/sqrt(length2);
    o = vec4(motion, exempt, ViewDistance(depth));
}
