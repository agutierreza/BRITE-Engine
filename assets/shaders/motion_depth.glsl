// MOTION BLUR'S DEPTH, shared by its shaders (CMakeLists.txt puts this file in place of `#include "motion_depth.glsl"`).
//
// depthRange: the projection's near and far distances, and 1 for a perspective projection, 0 for an orthographic one.
uniform vec3 depthRange;

// A depth as the depth buffer holds it, 0 at the near plane and 1 at the far, turned back into the distance along the
// view: the perspective projection's z_ndc = (f + n - 2 f n / z) / (f - n) solved for z, or the orthographic one's
// straight line from n to f.
float ViewDistance(float depth)
{
    float n = depthRange.x, f = depthRange.y;
    if (depthRange.z < 0.5)
        return n + depth*(f - n);
    float ndc = depth*2.0 - 1.0;
    return 2.0*n*f/(f + n - ndc*(f - n));
}
