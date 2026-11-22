#version 330

// One triangle that covers the target, from gl_VertexID alone: no vertex buffer, only an empty vertex array bound.
// The post chain's every full-screen draw (bloom, tone map) uses it. Embedded like pbr.vs (CMakeLists.txt).

out vec2 uv;

void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p*2.0 - 1.0, 0.0, 1.0);
}
