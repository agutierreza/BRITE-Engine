#version 330

// THE SUN-SHADOW DEPTH PASS: only depth is written. A cut-out (a draw's AlphaMask, a file's MASK) casts the shape it
// cuts, by the same test the lit shader cuts it with: the draw's colour times the vertex colour times the albedo
// texture, against the cutoff. Embedded (CMakeLists.txt).

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0; // the mesh's albedo
uniform vec4 colDiffuse;
uniform int alphaMask;
uniform float alphaCutoff;

void main()
{
    if (alphaMask == 1 && colDiffuse.a*fragColor.a*texture(texture0, fragTexCoord).a < alphaCutoff) discard;
    finalColor = vec4(1.0);
}
