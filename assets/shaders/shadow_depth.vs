#version 330

// THE SUN-SHADOW DEPTH PASS (RenderPass.hpp, ShadowSettings): a caster's mesh drawn from the light into a tile of the
// atlas. Position, and what a cut-out needs. Embedded (CMakeLists.txt).

// BRITE_INSTANCED 1: an instanced caster (InstancedModelDrawCommand), each copy's placement and tint an attribute, as
// pbr.vs takes them; mvp is then the light's view times the cascade's projection alone.
#ifndef BRITE_INSTANCED
#define BRITE_INSTANCED 0
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
#if BRITE_INSTANCED
layout(location = 9) in mat4 instanceTransform;
layout(location = 13) in vec4 instanceTint;
#endif

out vec2 fragTexCoord;
out vec4 fragColor;

uniform mat4 mvp;

void main()
{
    fragTexCoord = vertexTexCoord;
#if BRITE_INSTANCED
    fragColor = vertexColor*instanceTint;
    gl_Position = mvp*instanceTransform*vec4(vertexPosition, 1.0);
#else
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
#endif
}
