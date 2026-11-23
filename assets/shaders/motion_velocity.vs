#version 330

// MOTION BLUR'S VELOCITY DRAW (RenderPass.hpp, MotionBlurSettings): a moving draw's mesh, or an opted-out one's, drawn
// a second time into the velocity target with where each point is now and where it was last frame. Position alone,
// through a vertex array of the backend's own, so no mesh's own arrays are touched. Embedded (CMakeLists.txt).
//
// BRITE_INSTANCED 1: an instanced draw (InstancedModelDrawCommand), each copy's placement now and last frame an
// attribute, a column a location (1 to 4, and 5 to 8); the two view-projections are then uniforms of their own.
#ifndef BRITE_INSTANCED
#define BRITE_INSTANCED 0
#endif

layout(location = 0) in vec3 vertexPosition;
#if BRITE_INSTANCED
layout(location = 1) in mat4 instanceNow;
layout(location = 5) in mat4 instanceBefore;
uniform mat4 viewProjectionNow;
uniform mat4 viewProjectionBefore;
#else
uniform mat4 mvpNow;
uniform mat4 mvpBefore;
#endif

out vec4 clipNow;
out vec4 clipBefore;

void main()
{
#if BRITE_INSTANCED
    clipNow = viewProjectionNow*instanceNow*vec4(vertexPosition, 1.0);
    clipBefore = viewProjectionBefore*instanceBefore*vec4(vertexPosition, 1.0);
#else
    clipNow = mvpNow*vec4(vertexPosition, 1.0);
    clipBefore = mvpBefore*vec4(vertexPosition, 1.0);
#endif
    gl_Position = clipNow;
}
