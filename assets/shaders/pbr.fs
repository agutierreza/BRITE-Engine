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
uniform sampler2D mraMap;
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
uniform int useTexMRA;
uniform int useTexEmissive;

// raylib's per-draw diffuse colour: the material colour times the draw tint.
// This is how ModelDrawCommand's Material.AlbedoTint reaches the shader.
uniform vec4  colDiffuse;
uniform vec4  emissiveColor;
uniform float metallicValue;
uniform float roughnessValue;
uniform float aoValue;
uniform float emissivePower;

uniform Light lights[MAX_LIGHTS];
uniform vec3 viewPos;

uniform vec3 ambientColor;
uniform float ambient;

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

    float metallic = clamp(metallicValue, 0.0, 1.0);
    float roughness = clamp(roughnessValue, 0.04, 1.0);
    float ao = clamp(aoValue, 0.0, 1.0);

    if (useTexMRA == 1)
    {
        vec4 mra = texture(mraMap, fragTexCoord*tiling + offset);
        metallic = clamp(mra.r + metallicValue, 0.04, 1.0);
        roughness = clamp(mra.g + roughnessValue, 0.04, 1.0);
        ao = (mra.b + aoValue)*0.5;
    }

    vec3 N = normalize(fragNormal);
    if (useTexNormal == 1)
    {
        N = texture(normalMap, fragTexCoord*tiling + offset).rgb;
        N = normalize(N*2.0 - 1.0);
        N = normalize(N*TBN);
    }

    vec3 V = normalize(viewPos - fragPosition);
    vec3 R = reflect(-V, N);

    vec3 emissive = vec3(0);
    if (useTexEmissive == 1) emissive = (texture(emissiveMap, fragTexCoord*tiling + offset).rgb).g*emissiveColor.rgb*emissivePower;

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
    vec3 color = ComputePBR();
    color = color / (color + vec3(1.0)); // Reinhard tonemapping
    color = pow(color, vec3(1.0/2.2));   // Gamma correction
    finalColor = vec4(color, colDiffuse.a*fragColor.a);
}
