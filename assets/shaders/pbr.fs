#version 330

// BRITE's PBR surface shader: Cook-Torrance with up to MAX_LIGHTS analytic
// lights, plus either image-based lighting from the pass's environment map or
// a flat ambient term when the pass has none.
//
// COMPILED INTO THE ENGINE. CMakeLists.txt reads this file and pbr.vs at
// configure time into a generated header of string literals, and the backend
// loads them with LoadShaderFromMemory -- so a client stages nothing to draw a
// lit model. This file is the source of truth; editing it triggers a
// reconfigure. (Keep it free of a byte-order mark. GLSL wants "#version"
// first, and the whole file becomes the embedded string, so a mark would be
// the first thing the compiler sees. Some drivers accept that; a stricter
// one need not.)

#define MAX_LIGHTS              4
#define LIGHT_DIRECTIONAL       0
#define LIGHT_POINT             1
#define PI 3.14159265358979323846

struct Light {
    int enabled;
    int type;
    vec3 position;   // point lights: where the light is
    vec3 direction;  // directional lights: the direction the light TRAVELS, unit length
    vec4 color;
    float intensity;
};

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in mat3 TBN;

out vec4 finalColor;

uniform int numOfLights;
uniform sampler2D albedoMap;
// Metalness, roughness and occlusion, each its own map and each read from the
// channel glTF packs it in: metal in blue, roughness in green, occlusion in red.
// A greyscale map -- raylib's split of a file's metal-roughness texture, or a
// caller's own -- has the value in every channel, so it reads the same.
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D occlusionMap;
uniform sampler2D normalMap;
uniform sampler2D emissiveMap;

// IBL Maps - using sampler2D for equirectangular panoramas
uniform sampler2D irradianceMap;
uniform sampler2D prefilterMap;
uniform int useIBL;

uniform vec2 tiling;
uniform vec2 offset;

uniform int useTexAlbedo;
uniform int useTexNormal;
uniform int useTexMetallic;
uniform int useTexRoughness;
uniform int useTexOcclusion;
uniform int useTexEmissive;

// raylib's per-draw diffuse colour: the material colour times the draw tint.
// This is how ModelDrawCommand's Material.AlbedoTint reaches the shader.
uniform vec4  colDiffuse;
uniform float metallicValue;
uniform float roughnessValue;
// glTF's occlusionTexture.strength: 0 leaves the ambient untouched, 1 is the map
// in full.
uniform float occlusionStrength;

uniform Light lights[MAX_LIGHTS];
uniform vec3 viewPos;

uniform vec3 ambientColor;
uniform float ambient;

// 1: the material is unlit, and main() writes its albedo as authored.
uniform int unlit;

// 1: alpha is a cut-out at alphaCutoff (glTF's MASK); 0: it blends as the tint
// and vertex colour say, and the texture's alpha is ignored (glTF's OPAQUE).
uniform int alphaMask;
uniform float alphaCutoff;

// The light a surface gives off, linear RGB, strength included; black is none.
uniform vec3 emissiveLight;

// 1: the back face is drawn too, and lit with its normal turned to face the
// viewer (glTF's doubleSided).
uniform int doubleSided;

// Distance fog, the pass's: fogColor is sRGB, as a Color is authored, and the
// distances are world units from viewPos.
uniform int fogEnabled;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

vec3 SchlickFresnel(float hDotV,vec3 refl)
{
    return refl + (1.0 - refl)*pow(clamp(1.0 - hDotV, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float GgxDistribution(float nDotH,float roughness)
{
    float a = roughness*roughness*roughness*roughness;
    float d = nDotH*nDotH*(a - 1.0) + 1.0;
    d = PI*d*d;
    return (a/max(d,0.0000001));
}

float GeomSmith(float nDotV,float nDotL,float roughness)
{
    float r = roughness + 1.0;
    float k = r*r/8.0;
    float ik = 1.0 - k;
    float ggx1 = nDotV/(nDotV*ik + k);
    float ggx2 = nDotL/(nDotL*ik + k);
    return ggx1*ggx2;
}

vec2 EnvBRDFApprox(float Roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = Roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return AB;
}

vec3 ComputePBR()
{
    // Albedo is the draw tint times the vertex colour, times the albedo texture
    // when there is one. Colours are authored in sRGB -- a Color of (255, 0, 0)
    // is meant to LOOK pure red -- so they are linearised here and the result is
    // gamma-encoded again in main(). Without this a mid-grey vertex colour
    // renders far too dark.
    vec3 albedo = pow(colDiffuse.rgb*fragColor.rgb, vec3(2.2));
    if (useTexAlbedo == 1) albedo *= pow(texture(albedoMap, fragTexCoord*tiling + offset).rgb, vec3(2.2));

    // Factor times map, as glTF defines them: the factor alone where there is
    // no map. Occlusion darkens only the ambient light below -- the light no
    // single source casts -- and never the lights'.
    float metallic = metallicValue;
    float roughness = roughnessValue;
    float ao = 1.0;
    if (useTexMetallic == 1) metallic *= texture(metallicMap, fragTexCoord*tiling + offset).b;
    if (useTexRoughness == 1) roughness *= texture(roughnessMap, fragTexCoord*tiling + offset).g;
    if (useTexOcclusion == 1) ao = 1.0 + occlusionStrength*(texture(occlusionMap, fragTexCoord*tiling + offset).r - 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.04, 1.0);
    ao = clamp(ao, 0.0, 1.0);

    vec3 N = normalize(fragNormal);
    // A back face seen from behind is lit as the surface facing the viewer is,
    // which is the face whose normal points the other way.
    if (doubleSided == 1 && !gl_FrontFacing) N = -N;
    if (useTexNormal == 1)
    {
        N = texture(normalMap, fragTexCoord*tiling + offset).rgb;
        N = normalize(N*2.0 - 1.0);
        N = normalize(N*TBN);
    }

    vec3 V = normalize(viewPos - fragPosition);
    vec3 R = reflect(-V, N);

    // The light the surface gives off, added to what falls on it. emissiveLight
    // is linear and already carries the strength; the map, like the albedo, is
    // authored in sRGB and is linearised.
    vec3 emissive = emissiveLight;
    if (useTexEmissive == 1) emissive *= pow(texture(emissiveMap, fragTexCoord*tiling + offset).rgb, vec3(2.2));

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < numOfLights; i++)
    {
        if (lights[i].enabled == 0) continue;

        vec3 L;
        vec3 radiance;
        if (lights[i].type == LIGHT_DIRECTIONAL)
        {
            // A sun: parallel rays and no falloff. L points TOWARD the light,
            // which is against the direction it travels.
            L = normalize(-lights[i].direction);
            radiance = lights[i].color.rgb*lights[i].intensity;
        }
        else
        {
            vec3 toLight = lights[i].position - fragPosition;
            float dist = length(toLight);
            L = toLight/max(dist, 0.0001);
            float attenuation = 1.0/(dist*dist*0.23);
            radiance = lights[i].color.rgb*lights[i].intensity*attenuation;
        }
        vec3 H = normalize(V + L);

        float nDotV = max(dot(N,V), 0.0000001);
        float nDotL = max(dot(N,L), 0.0000001);
        float hDotV = max(dot(H,V), 0.0);
        float nDotH = max(dot(N,H), 0.0);

        float D = GgxDistribution(nDotH, roughness);
        float G = GeomSmith(nDotV, nDotL, roughness);
        vec3 F = SchlickFresnel(hDotV, F0);

        vec3 numerator    = D * G * F;
        float denominator = 4.0 * nDotV * nDotL;
        vec3 specular     = numerator / max(denominator, 0.001);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        Lo += (kD * albedo / PI + specular) * radiance * nDotL;
    }

    vec3 ambientFinal = vec3(0.0);
    if (useIBL == 1) {
        vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kS = F;
        vec3 kD = 1.0 - kS;
        kD *= 1.0 - metallic;

        // Sample equirectangular panorama
        vec2 envUV = SampleSphericalMap(N);
        vec2 refUV = SampleSphericalMap(R);

        // Approximate irradiance by sampling a high mip level
        const float MAX_REFLECTION_LOD = 8.0;
        vec3 irradiance = textureLod(irradianceMap, envUV, MAX_REFLECTION_LOD * 0.8).rgb;
        vec3 diffuse      = irradiance * albedo;

        // Sample prefilter map at mip level based on roughness
        vec3 prefilteredColor = textureLod(prefilterMap, refUV, roughness * MAX_REFLECTION_LOD).rgb;

        // Use analytical BRDF instead of texture LUT
        vec2 brdf = EnvBRDFApprox(roughness, max(dot(N, V), 0.0));
        vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

        ambientFinal = (kD * diffuse + specular) * ao;
    } else {
        // The flat ambient: the pass's ambient colour and strength, on the
        // albedo. The old form ADDED ambientColor to the albedo, which lit a
        // black surface as if it were grey.
        ambientFinal = ambientColor*ambient*albedo*ao;
    }

    return (ambientFinal + Lo + emissive);
}

void main()
{
    // The cut-out comes first, lit or unlit: a masked fragment is not drawn at
    // all, and one that stays is solid. The texture's alpha counts only here.
    float alpha = colDiffuse.a*fragColor.a;
    if (alphaMask == 1)
    {
        if (useTexAlbedo == 1) alpha *= texture(albedoMap, fragTexCoord*tiling + offset).a;
        if (alpha < alphaCutoff) discard;
        alpha = 1.0;
    }

    // Unlit: the albedo in the sRGB it was authored in, straight to the target.
    // No linearising, because nothing is lit; no tone mapping, because nothing
    // went above 1; no gamma, because nothing was linearised.
    if (unlit == 1)
    {
        vec3 albedo = colDiffuse.rgb*fragColor.rgb;
        if (useTexAlbedo == 1) albedo *= texture(albedoMap, fragTexCoord*tiling + offset).rgb;
        finalColor = vec4(albedo, alpha);
        return;
    }

    vec3 color = ComputePBR();
    color = color / (color + vec3(1.0)); // Reinhard tonemapping
    color = pow(color, vec3(1.0/2.2));   // Gamma correction

    // Fog on the finished colour, so fogColor is exactly what a surface beyond
    // fogEnd shows. The floor on the span makes an end at or before the start a
    // hard edge at the start, rather than a division by zero or a fade that
    // runs backwards.
    if (fogEnabled == 1)
    {
        float fromCamera = length(viewPos - fragPosition);
        float fade = clamp((fromCamera - fogStart)/max(fogEnd - fogStart, 0.0001), 0.0, 1.0);
        color = mix(color, fogColor, fade);
    }
    finalColor = vec4(color, alpha);
}
