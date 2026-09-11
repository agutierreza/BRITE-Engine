#version 330

// BRITE's PBR vertex stage. Compiled into the engine from this file; see the
// note at the top of pbr.fs.

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;
in vec4 vertexColor;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

// Output vertex attributes (to fragment shader)
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out mat3 TBN;

void main()
{
    // matNormal is the inverse-transpose of matModel, supplied by the backend
    // per draw, so a non-uniformly scaled model still gets normals perpendicular
    // to its faces. It used to be recomputed here per vertex with inverse(),
    // which is the one thing a vertex shader should never do.
    mat3 normalMatrix = mat3(matNormal);

    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord*2.0;
    // The vertex colour was declared and never passed on, which is why a model
    // with baked colours drew as if it had none.
    fragColor = vertexColor;
    fragNormal = normalize(normalMatrix*vertexNormal);

    // Tangent frame, only meaningful when a normal map is bound. A mesh without
    // tangents gets the attribute default (all zero), so nothing here may be
    // relied on unless useTexNormal is set.
    vec3 fragTangent = normalize(normalMatrix*vertexTangent.xyz);
    fragTangent = normalize(fragTangent - dot(fragTangent, fragNormal)*fragNormal);
    vec3 fragBinormal = cross(fragNormal, fragTangent);
    TBN = transpose(mat3(fragTangent, fragBinormal, fragNormal));

    // Calculate final vertex position
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
