#version 330

// BRITE's PBR vertex stage. Compiled into the engine from this file; see the
// note at the top of pbr.fs.
//
// THE VARIANT: BRITE_INSTANCED 1 draws an instanced draw (RenderPass.hpp,
// InstancedModelDrawCommand). Each copy's placement and tint come in as
// attributes, one per instance; matModel and matNormal are then unused and mvp is
// the view times the projection alone.
#ifndef BRITE_INSTANCED
#define BRITE_INSTANCED 0
#endif

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;
in vec4 vertexColor;
#if BRITE_INSTANCED
// raylib binds instanceTransform to location 9 (9 to 12, a column each); the
// tint takes the next free one. The backend fills both from its instance buffer.
layout(location = 9) in mat4 instanceTransform;
layout(location = 13) in vec4 instanceTint;
#endif

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
#if BRITE_INSTANCED
    // A copy's normal matrix without an inverse: the inverse-transpose of the
    // placement's 3x3 is its cofactor matrix over its determinant, and the
    // cofactor's columns are the cross products of the placement's. The normals
    // are normalised below, so only the determinant's sign is kept: a mirrored
    // copy (a negative scale) turns its normals back out.
    mat4 model = instanceTransform;
    mat3 placement = mat3(model);
    mat3 normalMatrix = mat3(cross(placement[1], placement[2]), cross(placement[2], placement[0]),
                             cross(placement[0], placement[1]));
    normalMatrix *= sign(dot(placement[0], cross(placement[1], placement[2])));
#else
    mat4 model = matModel;
    mat3 normalMatrix = mat3(matNormal);
#endif

    fragPosition = vec3(model*vec4(vertexPosition, 1.0));
    // As the mesh gives it: a texture spans its UV range once. It was doubled
    // here, which drew every texture twice across and twice down; repeating a
    // texture is the fragment stage's tiling uniform's job.
    fragTexCoord = vertexTexCoord;
    // The vertex colour was declared and never passed on, which is why a model
    // with baked colours drew as if it had none.
    fragColor = vertexColor;
#if BRITE_INSTANCED
    fragColor *= instanceTint;
#endif
    fragNormal = normalize(normalMatrix*vertexNormal);

    // Tangent frame, only meaningful when a normal map is bound. A mesh without
    // tangents gets the attribute default (all zero), so nothing here may be
    // relied on unless useTexNormal is set.
    vec3 fragTangent = normalize(normalMatrix*vertexTangent.xyz);
    fragTangent = normalize(fragTangent - dot(fragTangent, fragNormal)*fragNormal);
    // The tangent's w is its handedness, +1 or -1, as glTF defines it: the
    // bitangent is cross(normal, tangent) times w. Without it, a mirrored UV
    // layout reads its normal map's green channel upside down.
    vec3 fragBinormal = cross(fragNormal, fragTangent)*vertexTangent.w;
    TBN = transpose(mat3(fragTangent, fragBinormal, fragNormal));

    // Calculate final vertex position
#if BRITE_INSTANCED
    gl_Position = mvp*model*vec4(vertexPosition, 1.0);
#else
    gl_Position = mvp*vec4(vertexPosition, 1.0);
#endif
}
