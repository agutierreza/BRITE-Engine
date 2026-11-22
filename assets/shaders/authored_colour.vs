#version 330

// The vertex stage of raylib's own default shader, unchanged: what every primitive and 3D line is drawn through. Paired
// with authored_colour.fs for an HDR pass. Embedded (CMakeLists.txt).

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
