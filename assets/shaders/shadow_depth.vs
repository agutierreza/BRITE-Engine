#version 330

// THE SUN-SHADOW DEPTH PASS (RenderPass.hpp, ShadowSettings): a caster's mesh drawn from the light into a tile of the
// atlas. Position, and what a cut-out needs. Embedded (CMakeLists.txt).

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

out vec2 fragTexCoord;
out vec4 fragColor;

uniform mat4 mvp;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
