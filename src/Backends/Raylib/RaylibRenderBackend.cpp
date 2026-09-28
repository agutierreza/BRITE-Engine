#include "Backends/Raylib/RaylibRenderBackend.hpp"
#include "Backends/Raylib/PbrShaderSource.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

// Five OpenGL 3.0 calls rlgl does not wrap, the ones a multisampled render
// texture needs. They are reached through raylib's own GL loader, glad, whose
// function pointers raylib compiles in and leaves visible to the program it is
// linked into, already loaded once the window exists. Were raylib to rename or
// hide them, this would fail to link -- loudly -- rather than at run time.
extern "C" {
#if defined(_WIN32) && !defined(_WIN64)
#define BRITE_GL_CALL __stdcall
#else
#define BRITE_GL_CALL
#endif
extern void(BRITE_GL_CALL* glad_glGenRenderbuffers)(int count, unsigned int* renderbuffers);
extern void(BRITE_GL_CALL* glad_glBindRenderbuffer)(unsigned int target, unsigned int renderbuffer);
extern void(BRITE_GL_CALL* glad_glRenderbufferStorageMultisample)(unsigned int target, int samples,
                                                                  unsigned int internalFormat, int width, int height);
extern void(BRITE_GL_CALL* glad_glDeleteRenderbuffers)(int count, const unsigned int* renderbuffers);
extern void(BRITE_GL_CALL* glad_glGetIntegerv)(unsigned int name, int* values);
}

namespace BRITE {
namespace Backends {
namespace Raylib {

namespace {
// How many maps every raylib Material holds: rmodels.c's MAX_MATERIAL_MAPS, which
// raylib.h does not export. Each material's maps array is allocated this long and
// DrawMesh reads this many, so a copy of one must be exactly this long.
constexpr int MATERIAL_MAPS_PER_MATERIAL = 12;

// OpenGL names rlgl does not define, for the multisampled render texture.
constexpr unsigned int GL_RENDERBUFFER_ = 0x8D41;
constexpr unsigned int GL_RGBA8_ = 0x8058;
constexpr unsigned int GL_DEPTH_COMPONENT24_ = 0x81A6;
constexpr unsigned int GL_MAX_SAMPLES_ = 0x8D57;
constexpr int GL_COLOR_BUFFER_BIT_ = 0x4000;

// Tangents for a mesh that has texture coordinates and normals but no tangents
// of its own, so that a normal map can be read on it. raylib's GenMeshTangents
// takes the bitangent's direction from increasing v -- DOWN the texture, since v
// counts rows from the top, in glTF and in MeshData alike -- while a normal
// map's green means UP the texture. So the handedness it computes is flipped
// here, and a map reads the right way up. A mesh whose file gave tangents keeps
// them: their handedness is the file's to say.
void GenerateTangents(::Mesh& mesh) {
    if (mesh.tangents != nullptr || mesh.texcoords == nullptr || mesh.normals == nullptr || mesh.vertices == nullptr)
        return;
    ::GenMeshTangents(&mesh);
    if (mesh.tangents == nullptr)
        return;
    for (int i = 0; i < mesh.vertexCount; ++i)
        mesh.tangents[i * 4 + 3] = -mesh.tangents[i * 4 + 3];
    if (mesh.vboId != nullptr && mesh.vboId[SHADER_LOC_VERTEX_TANGENT] != 0)
        ::rlUpdateVertexBuffer(mesh.vboId[SHADER_LOC_VERTEX_TANGENT], mesh.tangents,
                               mesh.vertexCount * 4 * static_cast<int>(sizeof(float)), 0);
}

// Sets a texture's filters, anisotropy and wrap, generating its mipmaps first
// when the filter reads them. The filters go straight to rlgl rather than
// through raylib's SetTextureFilter, whose "point" and "bilinear" turn into
// mipmapped filters once a texture has mipmaps: here each filter means the same
// whatever was asked of the texture before.
void ApplySampling(::Texture2D& texture, const TextureSampling& sampling) {
    int minFilter = RL_TEXTURE_FILTER_NEAREST;
    int magFilter = RL_TEXTURE_FILTER_NEAREST;
    switch (sampling.Filter) {
    case SamplingFilter::Point:
        break;
    case SamplingFilter::Bilinear:
        minFilter = magFilter = RL_TEXTURE_FILTER_LINEAR;
        break;
    case SamplingFilter::Trilinear:
        if (texture.mipmaps <= 1)
            ::GenTextureMipmaps(&texture);
        minFilter = RL_TEXTURE_FILTER_MIP_LINEAR;
        magFilter = RL_TEXTURE_FILTER_LINEAR;
        break;
    }
    ::rlTextureParameters(texture.id, RL_TEXTURE_MIN_FILTER, minFilter);
    ::rlTextureParameters(texture.id, RL_TEXTURE_MAG_FILTER, magFilter);
    // rlgl resets the level to 1 before setting it, so 1 turns it off.
    ::rlTextureParameters(texture.id, RL_TEXTURE_FILTER_ANISOTROPIC, std::max(1, sampling.Anisotropy));
    const int wrap = sampling.Wrap == SamplingWrap::Clamp ? RL_TEXTURE_WRAP_CLAMP : RL_TEXTURE_WRAP_REPEAT;
    ::rlTextureParameters(texture.id, RL_TEXTURE_WRAP_S, wrap);
    ::rlTextureParameters(texture.id, RL_TEXTURE_WRAP_T, wrap);
}
} // namespace

void RaylibRenderBackend::SubmitRenderPass(const BRITE::RenderPass& pass) {
    if (pass.TargetFramebuffer != BRITE::NullTextureHandle) {
        auto it = m_textures.find(pass.TargetFramebuffer);
        if (it != m_textures.end() && it->second.isRenderTexture) {
            RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
            // A multisampled target is drawn into its multisampled framebuffer,
            // at the same size; the pass is resolved into rt when it ends.
            auto multisampled = m_multisampled.find(pass.TargetFramebuffer);
            if (multisampled != m_multisampled.end()) {
                RenderTexture2D surface = *rt;
                surface.id = multisampled->second.framebuffer;
                ::BeginTextureMode(surface);
            } else {
                ::BeginTextureMode(*rt);
            }
        } else {
            return; // Invalid target
        }
    } else {
        ::BeginDrawing();
    }

    if (pass.ShouldClear) {
        ::ClearBackground({pass.ClearColor.r, pass.ClearColor.g, pass.ClearColor.b, pass.ClearColor.a});
    }

    for (auto& cb : pass.BackgroundDrawCallbacks) {
        if (cb)
            cb();
    }

    if (pass.Shader != BRITE::NullShaderHandle) {
        auto it = m_shaders.find(pass.Shader);
        if (it != m_shaders.end()) {
            ::Shader* shader = static_cast<::Shader*>(it->second);
            ::BeginShaderMode(*shader);
        }
    }

    if (pass.Camera3DPtr) {
        ::Camera3D rlCamera3D = {0};
        rlCamera3D.position = {pass.Camera3DPtr->position.x, pass.Camera3DPtr->position.y,
                               pass.Camera3DPtr->position.z};
        rlCamera3D.target = {pass.Camera3DPtr->target.x, pass.Camera3DPtr->target.y, pass.Camera3DPtr->target.z};
        rlCamera3D.up = {pass.Camera3DPtr->up.x, pass.Camera3DPtr->up.y, pass.Camera3DPtr->up.z};
        rlCamera3D.fovy = pass.Camera3DPtr->fovy;
        rlCamera3D.projection = (pass.Camera3DPtr->projection == BRITE::Math::CameraProjection::Perspective)
                                    ? CAMERA_PERSPECTIVE
                                    : CAMERA_ORTHOGRAPHIC;
        ::BeginMode3D(rlCamera3D);

        // Draw Skybox if requested
        if (pass.DrawSkybox && pass.Environment.Cubemap != BRITE::NullTextureHandle) {
            auto texIt = m_textures.find(pass.Environment.Cubemap);
            if (texIt != m_textures.end() && !texIt->second.isRenderTexture) {
                ::rlDisableBackfaceCulling();
                ::rlDisableDepthMask();

                // Draw a simple cube as a skybox, but sample the HDR spherical map
                // We'll use rlgl directly for a quick spherical mapped cube or rely on standard DrawCube
                // A true skybox shader handles this better, but for now we draw a giant white cube inside out?
                // Actually, Raylib's models_skybox_rendering uses a specific shader for the skybox.
                // We could use pass.Environment.Cubemap and standard drawing.

                ::rlEnableDepthMask();
                ::rlEnableBackfaceCulling();
            }
        }
    }

    for (const auto& cmd : pass.Primitive3DCommands) {
        ::Color rlTint = {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a};

        ::rlPushMatrix();
        ::rlTranslatef(cmd.Position.x, cmd.Position.y, cmd.Position.z);

        ::Vector3 axis;
        float angle;
        ::Quaternion rlQuat = {cmd.Rotation.x, cmd.Rotation.y, cmd.Rotation.z, cmd.Rotation.w};
        ::QuaternionToAxisAngle(rlQuat, &axis, &angle);
        ::rlRotatef(angle * RAD2DEG, axis.x, axis.y, axis.z);

        ::rlScalef(cmd.Scale.x, cmd.Scale.y, cmd.Scale.z);

        switch (cmd.Type) {
        case BRITE::Primitive3DType::Cube:
            ::DrawCube({0, 0, 0}, cmd.Size.x, cmd.Size.y, cmd.Size.z, rlTint);
            break;
        case BRITE::Primitive3DType::CubeWires:
            ::DrawCubeWires({0, 0, 0}, cmd.Size.x, cmd.Size.y, cmd.Size.z, rlTint);
            break;
        case BRITE::Primitive3DType::Sphere:
            ::DrawSphere({0, 0, 0}, cmd.Size.x, rlTint); // Size.x is radius
            break;
        case BRITE::Primitive3DType::SphereWires:
            ::DrawSphereWires({0, 0, 0}, cmd.Size.x, 16, 16, rlTint); // Size.x is radius
            break;
        case BRITE::Primitive3DType::Grid:
            ::DrawGrid((int)cmd.Size.x, cmd.Size.y); // Size.x is slices, Size.y is spacing
            break;
        }

        ::rlPopMatrix();
    }

    if (!pass.ModelCommands.empty()) {
        EnsurePbrShader();
        ApplyPassLighting(pass);
    }

    for (const auto& cmd : pass.ModelCommands) {
        if (cmd.Model == BRITE::NullModelHandle)
            continue;
        auto it = m_models.find(cmd.Model);
        if (it == m_models.end())
            continue;

        const ::Model* rlModel = static_cast<::Model*>(it->second);
        const ::Shader& pbrShader = *static_cast<::Shader*>(m_shaders[m_pbrShader]);

        // A texture the draw command names, or nothing when it names none or the
        // handle is not a plain texture.
        auto commandTexture = [&](TextureHandle handle) -> const ::Texture2D* {
            if (handle == BRITE::NullTextureHandle)
                return nullptr;
            auto texIt = m_textures.find(handle);
            if (texIt == m_textures.end() || texIt->second.isRenderTexture)
                return nullptr;
            return static_cast<const ::Texture2D*>(texIt->second.ptr);
        };
        const std::pair<TextureHandle, int> overrides[] = {
            {cmd.Material.AlbedoMap, MATERIAL_MAP_ALBEDO},       {cmd.Material.NormalMap, MATERIAL_MAP_NORMAL},
            {cmd.Material.RoughnessMap, MATERIAL_MAP_ROUGHNESS}, {cmd.Material.MetallicMap, MATERIAL_MAP_METALNESS},
            {cmd.Material.EmissionMap, MATERIAL_MAP_EMISSION},   {cmd.Material.AOMap, MATERIAL_MAP_OCCLUSION},
        };
        const ::Texture2D* irradiance = commandTexture(pass.Environment.IrradianceMap);
        const ::Texture2D* prefilter = commandTexture(pass.Environment.PrefilterMap);

        // The model's placement, built exactly as DrawModelEx builds it (scale,
        // then rotation, then translation, after the model's own transform), so
        // a model lands where it always has.
        ::Vector3 rlAxis;
        float rlAngle;
        ::Quaternion rlQuat = {cmd.Rotation.x, cmd.Rotation.y, cmd.Rotation.z, cmd.Rotation.w};
        ::QuaternionToAxisAngle(rlQuat, &rlAxis, &rlAngle);
        rlAngle *= RAD2DEG;
        const ::Matrix placement =
            ::MatrixMultiply(::MatrixMultiply(::MatrixScale(cmd.Scale.x, cmd.Scale.y, cmd.Scale.z),
                                              ::MatrixRotate(rlAxis, rlAngle * DEG2RAD)),
                             ::MatrixTranslate(cmd.Position.x, cmd.Position.y, cmd.Position.z));
        const ::Matrix transform = ::MatrixMultiply(rlModel->transform, placement);
        const ::Color tint = {cmd.Material.AlbedoTint.r, cmd.Material.AlbedoTint.g, cmd.Material.AlbedoTint.b,
                              cmd.Material.AlbedoTint.a};

        // Mesh by mesh, each with ITS OWN material: a model loaded from a file
        // keeps its materials in slots 1..n (raylib reserves slot 0 for a
        // default), and every one of them is drawn lit. The material is copied,
        // maps and all, so nothing below changes the model: the draw command's
        // textures replace the model's own for this draw only, and a slot the
        // command leaves empty keeps what the model brought.
        for (int m = 0; m < rlModel->meshCount; ++m) {
            const ::Material& own = rlModel->materials[rlModel->meshMaterial[m]];
            ::MaterialMap maps[MATERIAL_MAPS_PER_MATERIAL];
            std::memcpy(maps, own.maps, sizeof(maps));
            ::Material material = own;
            material.maps = maps;
            material.shader = pbrShader;

            // The loader's placeholder is a 1x1 white texture, which is no texture.
            auto owns = [&](int map) {
                const unsigned int id = maps[map].texture.id;
                return id != 0 && id != ::rlGetTextureIdDefault();
            };
            OwnMaps ownMaps;
            ownMaps.albedo = owns(MATERIAL_MAP_ALBEDO);
            ownMaps.emission = owns(MATERIAL_MAP_EMISSION);
            ownMaps.metalness = owns(MATERIAL_MAP_METALNESS);
            ownMaps.roughness = owns(MATERIAL_MAP_ROUGHNESS);
            ownMaps.occlusion = owns(MATERIAL_MAP_OCCLUSION);
            ownMaps.normal = owns(MATERIAL_MAP_NORMAL);

            for (const auto& [handle, mapIndex] : overrides) {
                if (const ::Texture2D* texture = commandTexture(handle))
                    maps[mapIndex].texture = *texture;
            }
            // The environment belongs to the pass, never to the model.
            maps[MATERIAL_MAP_IRRADIANCE].texture = irradiance ? *irradiance : ::Texture2D{0};
            maps[MATERIAL_MAP_PREFILTER].texture = prefilter ? *prefilter : ::Texture2D{0};

            const bool bothSides =
                ApplyMaterial(cmd.Material, ownMaps, FileMaterial(cmd.Model, rlModel->meshMaterial[m]));

            // The draw tint times the material's own colour, as DrawModelEx does.
            const ::Color ownColour = maps[MATERIAL_MAP_DIFFUSE].color;
            maps[MATERIAL_MAP_DIFFUSE].color = {
                static_cast<unsigned char>((static_cast<int>(ownColour.r) * tint.r) / 255),
                static_cast<unsigned char>((static_cast<int>(ownColour.g) * tint.g) / 255),
                static_cast<unsigned char>((static_cast<int>(ownColour.b) * tint.b) / 255),
                static_cast<unsigned char>((static_cast<int>(ownColour.a) * tint.a) / 255)};

            // Culling off for this mesh alone, and back on before the next.
            if (bothSides)
                ::rlDisableBackfaceCulling();
            ::DrawMesh(rlModel->meshes[m], material, transform);
            if (bothSides)
                ::rlEnableBackfaceCulling();
        }
    }

    if (pass.Camera3DPtr) {
        // After the models, so a segment sharing their depth resolves in front of
        // them rather than behind.
        for (const auto& cmd : pass.Line3DCommands) {
            ::DrawLine3D({cmd.Start.x, cmd.Start.y, cmd.Start.z}, {cmd.End.x, cmd.End.y, cmd.End.z},
                         {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a});
        }

        ::EndMode3D();
    }

    if (pass.Camera) {
        ::Camera2D rlCamera;
        rlCamera.offset = {pass.Camera->offset.x, pass.Camera->offset.y};
        rlCamera.target = {pass.Camera->target.x, pass.Camera->target.y};
        rlCamera.rotation = pass.Camera->rotation;
        rlCamera.zoom = pass.Camera->zoom;
        ::BeginMode2D(rlCamera);
    }

    for (auto& cb : pass.WorldDrawCallbacks) {
        if (cb)
            cb();
    }

    for (const auto& cmd : pass.SpriteCommands) {
        if (cmd.Material.AlbedoMap == BRITE::NullTextureHandle)
            continue;
        auto it = m_textures.find(cmd.Material.AlbedoMap);
        if (it == m_textures.end())
            continue;

        ::Texture2D rlTexture;
        if (it->second.isRenderTexture) {
            RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
            rlTexture = rt->texture;
        } else {
            ::Texture2D* tex = static_cast<::Texture2D*>(it->second.ptr);
            rlTexture = *tex;
        }

        ::Rectangle rlSource = {cmd.SourceRect.x, cmd.SourceRect.y, cmd.SourceRect.width, cmd.SourceRect.height};
        ::Rectangle rlDest = {cmd.DestRect.x, cmd.DestRect.y, cmd.DestRect.width, cmd.DestRect.height};
        ::Vector2 rlOrigin = {cmd.Origin.x, cmd.Origin.y};
        ::Color rlTint = {cmd.Material.AlbedoTint.r, cmd.Material.AlbedoTint.g, cmd.Material.AlbedoTint.b,
                          cmd.Material.AlbedoTint.a};

        ::DrawTexturePro(rlTexture, rlSource, rlDest, rlOrigin, cmd.RotationDeg, rlTint);
    }

    for (const auto& cmd : pass.LineCommands) {
        ::DrawLine(cmd.Start.x, cmd.Start.y, cmd.End.x, cmd.End.y, {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a});
    }

    for (const auto& cmd : pass.RectCommands) {
        ::Rectangle rlDest = {cmd.DestRect.x, cmd.DestRect.y, cmd.DestRect.width, cmd.DestRect.height};
        ::Vector2 rlOrigin = {cmd.Origin.x, cmd.Origin.y};
        ::Color rlTint = {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a};
        if (cmd.IsFilled) {
            ::DrawRectanglePro(rlDest, rlOrigin, cmd.RotationDeg, rlTint);
        } else {
            ::DrawRectangleLines(cmd.DestRect.x - cmd.Origin.x, cmd.DestRect.y - cmd.Origin.y, cmd.DestRect.width,
                                 cmd.DestRect.height, rlTint);
        }
    }

    if (pass.Camera) {
        ::EndMode2D();
    }

    for (auto& cb : pass.UIDrawCallbacks) {
        if (cb)
            cb();
    }

    if (pass.Shader != BRITE::NullShaderHandle) {
        auto it = m_shaders.find(pass.Shader);
        if (it != m_shaders.end()) {
            ::EndShaderMode();
        }
    }

    if (pass.TargetFramebuffer != BRITE::NullTextureHandle) {
        ::EndTextureMode();
        // Resolve a multisampled target: average each pixel's samples into the
        // ordinary render texture that every reader of the handle reads.
        auto multisampled = m_multisampled.find(pass.TargetFramebuffer);
        if (multisampled != m_multisampled.end()) {
            const RenderTexture2D* rt = static_cast<RenderTexture2D*>(m_textures[pass.TargetFramebuffer].ptr);
            const int width = rt->texture.width, height = rt->texture.height;
            ::rlBindFramebuffer(RL_READ_FRAMEBUFFER, multisampled->second.framebuffer);
            ::rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, rt->id);
            ::rlBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT_);
            ::rlBindFramebuffer(RL_READ_FRAMEBUFFER, 0);
            ::rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, 0);
        }
    } else {
        ::EndDrawing();
    }
}

// ---------------------------------------------------------------------------
// The PBR shader: compiled from the embedded source on first use
// ---------------------------------------------------------------------------

void RaylibRenderBackend::EnsurePbrShader() {
    if (m_pbrShader != BRITE::NullShaderHandle)
        return;

    m_pbrShader = LoadShaderFromMemory(EmbeddedShaders::PBR_VERTEX, EmbeddedShaders::PBR_FRAGMENT);
    ::Shader* shader = static_cast<::Shader*>(m_shaders[m_pbrShader]);
    if (shader->id == ::rlGetShaderIdDefault()) {
        // raylib falls back to its default shader when compilation fails, which
        // draws the model unlit and white -- visibly wrong, but not a crash. The
        // compiler's own messages precede this line in the raylib log.
        spdlog::error("BRITE: the embedded PBR shader failed to compile; models will draw with raylib's default "
                      "shader");
    }

    shader->locs[SHADER_LOC_MAP_ALBEDO] = ::GetShaderLocation(*shader, "albedoMap");
    shader->locs[SHADER_LOC_MAP_METALNESS] = ::GetShaderLocation(*shader, "metallicMap");
    shader->locs[SHADER_LOC_MAP_ROUGHNESS] = ::GetShaderLocation(*shader, "roughnessMap");
    shader->locs[SHADER_LOC_MAP_OCCLUSION] = ::GetShaderLocation(*shader, "occlusionMap");
    shader->locs[SHADER_LOC_MAP_NORMAL] = ::GetShaderLocation(*shader, "normalMap");
    shader->locs[SHADER_LOC_MAP_EMISSION] = ::GetShaderLocation(*shader, "emissiveMap");
    shader->locs[SHADER_LOC_MAP_IRRADIANCE] = ::GetShaderLocation(*shader, "irradianceMap");
    shader->locs[SHADER_LOC_MAP_PREFILTER] = ::GetShaderLocation(*shader, "prefilterMap");

    auto loc = [&](const char* name) { return ::GetShaderLocation(*shader, name); };
    m_pbrLocs.numOfLights = loc("numOfLights");
    m_pbrLocs.useIBL = loc("useIBL");
    m_pbrLocs.viewPos = loc("viewPos");
    m_pbrLocs.ambientColor = loc("ambientColor");
    m_pbrLocs.ambient = loc("ambient");
    m_pbrLocs.useTexAlbedo = loc("useTexAlbedo");
    m_pbrLocs.useTexNormal = loc("useTexNormal");
    m_pbrLocs.useTexMetallic = loc("useTexMetallic");
    m_pbrLocs.useTexRoughness = loc("useTexRoughness");
    m_pbrLocs.useTexOcclusion = loc("useTexOcclusion");
    m_pbrLocs.useTexEmissive = loc("useTexEmissive");
    m_pbrLocs.metallicValue = loc("metallicValue");
    m_pbrLocs.roughnessValue = loc("roughnessValue");
    m_pbrLocs.occlusionStrength = loc("occlusionStrength");
    m_pbrLocs.normalScale = loc("normalScale");
    m_pbrLocs.unlit = loc("unlit");
    m_pbrLocs.alphaMask = loc("alphaMask");
    m_pbrLocs.alphaCutoff = loc("alphaCutoff");
    m_pbrLocs.doubleSided = loc("doubleSided");
    m_pbrLocs.emissiveLight = loc("emissiveLight");
    m_pbrLocs.fogEnabled = loc("fogEnabled");
    m_pbrLocs.fogColor = loc("fogColor");
    m_pbrLocs.fogStart = loc("fogStart");
    m_pbrLocs.fogEnd = loc("fogEnd");
    for (std::size_t i = 0; i < BRITE::MaxLightsPerPass; ++i) {
        const std::string prefix = "lights[" + std::to_string(i) + "].";
        auto& l = m_pbrLocs.lights[i];
        l.enabled = loc((prefix + "enabled").c_str());
        l.type = loc((prefix + "type").c_str());
        l.position = loc((prefix + "position").c_str());
        l.direction = loc((prefix + "direction").c_str());
        l.color = loc((prefix + "color").c_str());
        l.intensity = loc((prefix + "intensity").c_str());
    }

    // The texture-tiling uniforms have no per-material data yet; identity so a
    // textured model reads its map once across its UV range.
    const float tiling[2] = {1.0f, 1.0f};
    const float offset[2] = {0.0f, 0.0f};
    ::SetShaderValue(*shader, loc("tiling"), tiling, SHADER_UNIFORM_VEC2);
    ::SetShaderValue(*shader, loc("offset"), offset, SHADER_UNIFORM_VEC2);
}

void RaylibRenderBackend::ApplyPassLighting(const BRITE::RenderPass& pass) {
    ::Shader* shader = static_cast<::Shader*>(m_shaders[m_pbrShader]);

    const int count = static_cast<int>(std::min(pass.Lights.size(), BRITE::MaxLightsPerPass));
    for (std::size_t i = 0; i < BRITE::MaxLightsPerPass; ++i) {
        const auto& locs = m_pbrLocs.lights[i];
        const int enabled = static_cast<int>(i) < count ? 1 : 0;
        ::SetShaderValue(*shader, locs.enabled, &enabled, SHADER_UNIFORM_INT);
        if (!enabled)
            continue;

        const BRITE::Light& light = pass.Lights[i];
        const int type = light.Type == BRITE::LightType::Directional ? 0 : 1;
        ::SetShaderValue(*shader, locs.type, &type, SHADER_UNIFORM_INT);

        const float position[3] = {light.Position.x, light.Position.y, light.Position.z};
        ::SetShaderValue(*shader, locs.position, position, SHADER_UNIFORM_VEC3);

        // Normalised here so the shader can use it as-is. A zero direction is
        // meaningless for a sun; it becomes straight down rather than NaN.
        BRITE::Vector3 dir = BRITE::Math::Normalize(light.Direction);
        if (BRITE::Math::Length(dir) < 0.5f)
            dir = {0.0f, -1.0f, 0.0f};
        const float direction[3] = {dir.x, dir.y, dir.z};
        ::SetShaderValue(*shader, locs.direction, direction, SHADER_UNIFORM_VEC3);

        const float color[4] = {light.Tint.r / 255.0f, light.Tint.g / 255.0f, light.Tint.b / 255.0f,
                                light.Tint.a / 255.0f};
        ::SetShaderValue(*shader, locs.color, color, SHADER_UNIFORM_VEC4);
        ::SetShaderValue(*shader, locs.intensity, &light.Intensity, SHADER_UNIFORM_FLOAT);
    }
    ::SetShaderValue(*shader, m_pbrLocs.numOfLights, &count, SHADER_UNIFORM_INT);

    const float ambientColor[3] = {pass.AmbientColor.r / 255.0f, pass.AmbientColor.g / 255.0f,
                                   pass.AmbientColor.b / 255.0f};
    ::SetShaderValue(*shader, m_pbrLocs.ambientColor, ambientColor, SHADER_UNIFORM_VEC3);
    ::SetShaderValue(*shader, m_pbrLocs.ambient, &pass.AmbientIntensity, SHADER_UNIFORM_FLOAT);

    // Every pass sets the fog, off included: a uniform keeps its value, so a
    // fogged pass would otherwise fog every pass after it.
    const int fogEnabled = pass.Fog.Enabled ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.fogEnabled, &fogEnabled, SHADER_UNIFORM_INT);
    const float fogColor[3] = {pass.Fog.Tint.r / 255.0f, pass.Fog.Tint.g / 255.0f, pass.Fog.Tint.b / 255.0f};
    ::SetShaderValue(*shader, m_pbrLocs.fogColor, fogColor, SHADER_UNIFORM_VEC3);
    ::SetShaderValue(*shader, m_pbrLocs.fogStart, &pass.Fog.Start, SHADER_UNIFORM_FLOAT);
    ::SetShaderValue(*shader, m_pbrLocs.fogEnd, &pass.Fog.End, SHADER_UNIFORM_FLOAT);

    const int useIBL = pass.Environment.IrradianceMap != BRITE::NullTextureHandle ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.useIBL, &useIBL, SHADER_UNIFORM_INT);

    if (pass.Camera3DPtr) {
        const float cameraPos[3] = {pass.Camera3DPtr->position.x, pass.Camera3DPtr->position.y,
                                    pass.Camera3DPtr->position.z};
        ::SetShaderValue(*shader, m_pbrLocs.viewPos, cameraPos, SHADER_UNIFORM_VEC3);
    }
}

bool RaylibRenderBackend::ApplyMaterial(const BRITE::PBRMaterial& material, const OwnMaps& ownMaps,
                                        const BRITE::GltfMaterialInfo* fileMaterial) {
    ::Shader* shader = static_cast<::Shader*>(m_shaders[m_pbrShader]);

    // Which maps are bound. The shader used to assume all of them, so a model
    // with no albedo texture sampled whatever was on texture unit 0 -- black on
    // the first draw -- instead of its colour. An albedo texture is the draw
    // command's or, failing that, the one the model's own material brought. So
    // is each of the metalness, roughness, occlusion and normal maps.
    const bool metalMap = material.MetallicMap != BRITE::NullTextureHandle || ownMaps.metalness;
    const bool roughMap = material.RoughnessMap != BRITE::NullTextureHandle || ownMaps.roughness;
    const bool occlusionMap = material.AOMap != BRITE::NullTextureHandle || ownMaps.occlusion;
    const int useAlbedo = (material.AlbedoMap != BRITE::NullTextureHandle || ownMaps.albedo) ? 1 : 0;
    const int useNormal = (material.NormalMap != BRITE::NullTextureHandle || ownMaps.normal) ? 1 : 0;
    const int useMetallic = metalMap ? 1 : 0;
    const int useRoughness = roughMap ? 1 : 0;
    const int useOcclusion = occlusionMap ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.useTexAlbedo, &useAlbedo, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexNormal, &useNormal, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexMetallic, &useMetallic, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexRoughness, &useRoughness, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexOcclusion, &useOcclusion, SHADER_UNIFORM_INT);

    // The factor each map multiplies, as glTF defines them. A file's own where
    // it wrote one. Where it did not: 1 under a map -- glTF's own default,
    // which means "as the map says" -- and the draw's scalar where there is no
    // map, never glTF's unwritten 1 alone, which is fully metallic and draws
    // dark under anything but an environment map. The draw's scalars are the
    // no-map case and nothing else, as PBRMaterial says.
    auto factor = [](bool written, float fileValue, bool mapped, float drawValue) {
        return written ? fileValue : mapped ? 1.0f : drawValue;
    };
    const bool hasFile = fileMaterial != nullptr;
    const float metallic = factor(hasFile && fileMaterial->HasMetallicFactor,
                                  hasFile ? fileMaterial->MetallicFactor : 0.0f, metalMap, material.Metallic);
    const float roughness = factor(hasFile && fileMaterial->HasRoughnessFactor,
                                   hasFile ? fileMaterial->RoughnessFactor : 0.0f, roughMap, material.Roughness);
    ::SetShaderValue(*shader, m_pbrLocs.metallicValue, &metallic, SHADER_UNIFORM_FLOAT);
    ::SetShaderValue(*shader, m_pbrLocs.roughnessValue, &roughness, SHADER_UNIFORM_FLOAT);
    // The file's occlusion strength for its own map; the whole map for a draw's.
    const float occlusionStrength =
        (material.AOMap == BRITE::NullTextureHandle && hasFile) ? fileMaterial->OcclusionStrength : 1.0f;
    ::SetShaderValue(*shader, m_pbrLocs.occlusionStrength, &occlusionStrength, SHADER_UNIFORM_FLOAT);
    // The file's normal scale for its own map; the map as authored for a draw's.
    const float normalScale =
        (material.NormalMap == BRITE::NullTextureHandle && hasFile) ? fileMaterial->NormalScale : 1.0f;
    ::SetShaderValue(*shader, m_pbrLocs.normalScale, &normalScale, SHADER_UNIFORM_FLOAT);

    // Set on every mesh, lit or not: a uniform keeps its value between draws,
    // so an unlit mesh would otherwise leave every mesh after it unlit.
    // The draw's Unlit, or the file's KHR_materials_unlit.
    const int unlit = (material.Unlit || (fileMaterial != nullptr && fileMaterial->Unlit)) ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.unlit, &unlit, SHADER_UNIFORM_INT);

    // The cut-out: the draw's own if it asks for one, else the file's MASK.
    // BLEND is read but not yet honoured -- it needs translucent draws sorted,
    // which nothing does -- so a BLEND material draws as it always has.
    const bool fileMasks = fileMaterial != nullptr && fileMaterial->AlphaMode == BRITE::GltfAlphaMode::Mask;
    const int alphaMask = (material.AlphaMask || fileMasks) ? 1 : 0;
    const float alphaCutoff = material.AlphaMask ? material.AlphaCutoff : fileMasks ? fileMaterial->AlphaCutoff : 0.0f;
    ::SetShaderValue(*shader, m_pbrLocs.alphaMask, &alphaMask, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.alphaCutoff, &alphaCutoff, SHADER_UNIFORM_FLOAT);

    const bool bothSides = material.DoubleSided || (fileMaterial != nullptr && fileMaterial->DoubleSided);
    const int doubleSided = bothSides ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.doubleSided, &doubleSided, SHADER_UNIFORM_INT);

    // Emission: the draw's if it names any, else the file's, else none; set on
    // every mesh, so one mesh's glow is never left on the next. The draw's
    // colour is sRGB, as the tint is, and is linearised; a file's factor is
    // linear already, as glTF defines it. The strength multiplies either.
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    int useEmissive = 0;
    const bool drawEmits = material.Emission.r != 0 || material.Emission.g != 0 || material.Emission.b != 0 ||
                           material.EmissionMap != BRITE::NullTextureHandle;
    if (drawEmits) {
        const unsigned char channels[3] = {material.Emission.r, material.Emission.g, material.Emission.b};
        for (int c = 0; c < 3; ++c)
            emissive[c] = std::pow(channels[c] / 255.0f, 2.2f) * material.EmissionStrength;
        useEmissive = material.EmissionMap != BRITE::NullTextureHandle ? 1 : 0;
    } else if (fileMaterial != nullptr) {
        for (int c = 0; c < 3; ++c)
            emissive[c] = fileMaterial->EmissiveFactor[c] * fileMaterial->EmissiveStrength;
        useEmissive = ownMaps.emission ? 1 : 0;
    }
    ::SetShaderValue(*shader, m_pbrLocs.emissiveLight, emissive, SHADER_UNIFORM_VEC3);
    ::SetShaderValue(*shader, m_pbrLocs.useTexEmissive, &useEmissive, SHADER_UNIFORM_INT);
    return bothSides;
}

const BRITE::GltfMaterialInfo* RaylibRenderBackend::FileMaterial(BRITE::ModelHandle model, int slot) const {
    auto it = m_fileMaterials.find(model);
    if (it == m_fileMaterials.end() || slot <= 0 || static_cast<std::size_t>(slot) >= it->second.size())
        return nullptr;
    return &it->second[static_cast<std::size_t>(slot)];
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

BRITE::TextureHandle RaylibRenderBackend::LoadRenderTexture(int width, int height) {
    RenderTexture2D* rt = new RenderTexture2D(::LoadRenderTexture(width, height));
    BRITE::TextureHandle handle = m_nextId++;
    m_textures[handle] = {true, rt};
    return handle;
}

BRITE::TextureHandle RaylibRenderBackend::LoadMultisampledRenderTexture(int width, int height, int samples) {
    // The ordinary render texture every reader reads, and the pass resolves into.
    const BRITE::TextureHandle handle = LoadRenderTexture(width, height);
    if (samples <= 1)
        return handle;

    int most = 0;
    glad_glGetIntegerv(GL_MAX_SAMPLES_, &most);
    const int count = std::min(samples, most);
    if (count <= 1) {
        spdlog::warn("BRITE: this device cannot multisample a render texture; {} samples asked, an ordinary one "
                     "given",
                     samples);
        return handle;
    }

    MultisampledTarget target;
    target.samples = count;
    glad_glGenRenderbuffers(1, &target.colour);
    glad_glBindRenderbuffer(GL_RENDERBUFFER_, target.colour);
    glad_glRenderbufferStorageMultisample(GL_RENDERBUFFER_, count, GL_RGBA8_, width, height);
    glad_glGenRenderbuffers(1, &target.depth);
    glad_glBindRenderbuffer(GL_RENDERBUFFER_, target.depth);
    glad_glRenderbufferStorageMultisample(GL_RENDERBUFFER_, count, GL_DEPTH_COMPONENT24_, width, height);
    glad_glBindRenderbuffer(GL_RENDERBUFFER_, 0);

    target.framebuffer = ::rlLoadFramebuffer();
    ::rlFramebufferAttach(target.framebuffer, target.colour, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_RENDERBUFFER,
                          0);
    ::rlFramebufferAttach(target.framebuffer, target.depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    if (!::rlFramebufferComplete(target.framebuffer)) {
        spdlog::warn("BRITE: a {}-sample render texture could not be made; an ordinary one given", count);
        glad_glDeleteRenderbuffers(1, &target.colour);
        ::rlUnloadFramebuffer(target.framebuffer); // and the depth renderbuffer attached to it
        return handle;
    }
    m_multisampled[handle] = target;
    return handle;
}

int RaylibRenderBackend::RenderTextureSamples(BRITE::TextureHandle target) const {
    auto it = m_textures.find(target);
    if (it == m_textures.end() || !it->second.isRenderTexture)
        return 0;
    auto multisampled = m_multisampled.find(target);
    return multisampled != m_multisampled.end() ? multisampled->second.samples : 1;
}

void RaylibRenderBackend::UnloadRenderTexture(BRITE::TextureHandle target) {
    auto multisampled = m_multisampled.find(target);
    if (multisampled != m_multisampled.end()) {
        glad_glDeleteRenderbuffers(1, &multisampled->second.colour);
        ::rlUnloadFramebuffer(multisampled->second.framebuffer); // and the depth renderbuffer attached to it
        m_multisampled.erase(multisampled);
    }
    auto it = m_textures.find(target);
    if (it != m_textures.end() && it->second.isRenderTexture) {
        RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
        ::UnloadRenderTexture(*rt);
        delete rt;
        m_textures.erase(it);
    }
}

bool RaylibRenderBackend::ReadRenderTexture(BRITE::TextureHandle target, int& width, int& height,
                                            std::vector<BRITE::Color>& pixels) {
    auto it = m_textures.find(target);
    if (it == m_textures.end() || !it->second.isRenderTexture)
        return false;

    RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
    ::Image image = ::LoadImageFromTexture(rt->texture);
    if (image.data == nullptr)
        return false;

    ::ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    // A render texture is stored bottom row first (OpenGL's origin); the caller
    // is promised top row first.
    ::ImageFlipVertical(&image);

    width = image.width;
    height = image.height;
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    static_assert(sizeof(BRITE::Color) == 4, "BRITE::Color must be four bytes to copy an RGBA8 image into it");
    std::memcpy(pixels.data(), image.data, pixels.size() * sizeof(BRITE::Color));

    ::UnloadImage(image);
    return true;
}

BRITE::TextureHandle RaylibRenderBackend::LoadTexture(const char* fileName) {
    ::Texture2D* tex = new ::Texture2D(::LoadTexture(fileName));
    BRITE::TextureHandle handle = m_nextId++;
    m_textures[handle] = {false, tex};
    return handle;
}

void RaylibRenderBackend::UnloadTexture(BRITE::TextureHandle texture) {
    auto it = m_textures.find(texture);
    if (it != m_textures.end() && !it->second.isRenderTexture) {
        ::Texture2D* tex = static_cast<::Texture2D*>(it->second.ptr);
        ::UnloadTexture(*tex);
        delete tex;
        m_textures.erase(it);
    }
}

bool RaylibRenderBackend::SetTextureSampling(BRITE::TextureHandle texture, const TextureSampling& sampling) {
    auto it = m_textures.find(texture);
    if (it == m_textures.end() || it->second.isRenderTexture)
        return false;
    ApplySampling(*static_cast<::Texture2D*>(it->second.ptr), sampling);
    return true;
}

bool RaylibRenderBackend::SetModelTextureSampling(BRITE::ModelHandle model, const TextureSampling& sampling) {
    auto it = m_models.find(model);
    if (it == m_models.end())
        return false;
    ::Model* rlModel = static_cast<::Model*>(it->second);

    // The textures the model owns, each once -- the shared placeholder is not
    // the model's to change. A texture bound in several maps is sampled once,
    // and every map holding it learns the mipmap count that produced.
    std::vector<unsigned int> bound;
    for (int m = 0; m < rlModel->materialCount; ++m)
        for (int map = 0; map < MATERIAL_MAPS_PER_MATERIAL; ++map)
            bound.push_back(rlModel->materials[m].maps[map].texture.id);
    for (const unsigned int id : TexturesOwnedByModel(bound, ::rlGetTextureIdDefault())) {
        ::Texture2D* first = nullptr;
        for (int m = 0; m < rlModel->materialCount; ++m) {
            for (int map = 0; map < MATERIAL_MAPS_PER_MATERIAL; ++map) {
                ::Texture2D& held = rlModel->materials[m].maps[map].texture;
                if (held.id != id)
                    continue;
                if (first == nullptr) {
                    first = &held;
                    ApplySampling(held, sampling);
                } else {
                    held.mipmaps = first->mipmaps;
                }
            }
        }
    }
    return true;
}

unsigned int RaylibRenderBackend::NativeTextureId(BRITE::TextureHandle texture) const {
    auto it = m_textures.find(texture);
    if (it == m_textures.end() || it->second.isRenderTexture)
        return 0;
    return static_cast<const ::Texture2D*>(it->second.ptr)->id;
}

BRITE::EnvironmentMap RaylibRenderBackend::LoadEnvironmentMap(const char* hdrFileName) {
    BRITE::EnvironmentMap env;

    // Load HDR Panorama as standard Texture2D
    ::Texture2D panorama = ::LoadTexture(hdrFileName);

    // Generate Mipmaps so that the shader can sample varying roughness
    ::GenTextureMipmaps(&panorama);

    // Set texture filter to trilinear for smooth mipmap interpolation
    ::SetTextureFilter(panorama, TEXTURE_FILTER_TRILINEAR);

    ::Texture2D* envTex = new ::Texture2D(panorama);
    BRITE::TextureHandle handle = m_nextId++;
    m_textures[handle] = {false, envTex};

    // Since we updated pbr.fs to sample spherically from a sampler2D with mipmaps,
    // we use the same texture handle for everything!
    env.Cubemap = handle;
    env.IrradianceMap = handle;
    env.PrefilterMap = handle;

    return env;
}

void RaylibRenderBackend::UnloadEnvironmentMap(BRITE::EnvironmentMap envMap) {
    UnloadTexture(envMap.Cubemap);
}

BRITE::ModelHandle RaylibRenderBackend::LoadModel(const char* fileName) {
    ::Model* model = new ::Model(::LoadModel(fileName));
    BRITE::ModelHandle handle = m_nextModelId++;
    m_models[handle] = model;
    for (int m = 0; m < model->meshCount; ++m)
        GenerateTangents(model->meshes[m]);

    // raylib's glTF loader reads a material's base colour and textures and
    // nothing else, so the rest is read from the file here: the file's material
    // i is raylib's slot i + 1, and slot 0 is raylib's own default. Read the
    // same way raylib reads the file, relative to the working directory.
    if (::IsFileExtension(fileName, ".gltf") || ::IsFileExtension(fileName, ".glb")) {
        // glTF's vertex colours are linear, as its base colours are, and raylib
        // stored each as value x 255 -- as if it were an sRGB byte, which the
        // shader would linearise a second time. Re-encode each as the draw colour
        // it means, as the base colours are below, and re-upload the buffer.
        // (The buffer is indexed by raylib's attribute location for colour, 3,
        // not by the shader-location enum's 5.)
        for (int m = 0; m < model->meshCount; ++m) {
            ::Mesh& mesh = model->meshes[m];
            if (mesh.colors == nullptr)
                continue;
            for (int v = 0; v < mesh.vertexCount; ++v) {
                unsigned char* c = &mesh.colors[v * 4];
                const float linear[4] = {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
                const BRITE::Color drawn = BRITE::DrawColorFromLinear(linear);
                c[0] = drawn.r;
                c[1] = drawn.g;
                c[2] = drawn.b;
                c[3] = drawn.a;
            }
            if (mesh.vboId != nullptr && mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR] != 0)
                ::rlUpdateVertexBuffer(mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR], mesh.colors,
                                       mesh.vertexCount * 4, 0);
        }

        std::ifstream file(fileName, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                               std::istreambuf_iterator<char>());
        std::vector<BRITE::GltfMaterialInfo> materials;
        if (!BRITE::ReadGltfMaterials(bytes, materials)) {
            spdlog::warn("BRITE: {}: its materials could not be read; drawn with their base colours alone", fileName);
        } else if (static_cast<int>(materials.size()) + 1 != model->materialCount) {
            spdlog::warn("BRITE: {}: {} materials in the file against {} loaded; drawn with their base colours alone",
                         fileName, materials.size(), model->materialCount - 1);
        } else {
            materials.insert(materials.begin(), BRITE::GltfMaterialInfo{});
            // raylib stored each base colour factor as factor x 255, as if the
            // linear factor were an sRGB byte, and the shader would linearise it
            // a second time. Replace it with the draw colour the factor means,
            // once, here -- so a drawn model and its read-back meshes agree.
            for (int slot = 1; slot < model->materialCount; ++slot) {
                const BRITE::Color colour =
                    BRITE::DrawColorFromLinear(materials[static_cast<std::size_t>(slot)].BaseColorFactor);
                model->materials[slot].maps[MATERIAL_MAP_ALBEDO].color = {colour.r, colour.g, colour.b, colour.a};
            }
            m_fileMaterials[handle] = std::move(materials);
        }
    }
    return handle;
}

BRITE::ModelHandle RaylibRenderBackend::LoadModelFromMesh(const BRITE::MeshData& data) {
    // The refusals live in CheckMeshData, a pure function with its own tests;
    // this only reports them. Everything below it may assume a well-formed mesh.
    const BRITE::MeshDataProblem problem = BRITE::CheckMeshData(data, MaxVerticesPerMesh);
    if (problem != BRITE::MeshDataProblem::None) {
        spdlog::error("BRITE: LoadModelFromMesh refused a mesh because {} ({} positions, {} normals, {} colours, "
                      "{} texture coordinates, {} indices; at most {} vertices per mesh)",
                      BRITE::Describe(problem), data.Positions.size(), data.Normals.size(), data.Colors.size(),
                      data.TexCoords.size(), data.Indices.size(), MaxVerticesPerMesh);
        return BRITE::NullModelHandle;
    }
    const std::size_t vertexCount = data.Positions.size();

    // raylib owns these arrays from here: UnloadModel frees them with RL_FREE, so
    // they are allocated with its allocator rather than a std::vector's.
    ::Mesh mesh = {0};
    mesh.vertexCount = static_cast<int>(vertexCount);
    mesh.triangleCount = static_cast<int>(data.Indices.size() / 3);
    mesh.vertices = static_cast<float*>(RL_MALLOC(vertexCount * 3 * sizeof(float)));
    mesh.normals = static_cast<float*>(RL_MALLOC(vertexCount * 3 * sizeof(float)));
    mesh.indices = static_cast<unsigned short*>(RL_MALLOC(data.Indices.size() * sizeof(unsigned short)));
    for (std::size_t i = 0; i < vertexCount; ++i) {
        mesh.vertices[i * 3 + 0] = data.Positions[i].x;
        mesh.vertices[i * 3 + 1] = data.Positions[i].y;
        mesh.vertices[i * 3 + 2] = data.Positions[i].z;
        mesh.normals[i * 3 + 0] = data.Normals[i].x;
        mesh.normals[i * 3 + 1] = data.Normals[i].y;
        mesh.normals[i * 3 + 2] = data.Normals[i].z;
    }
    if (!data.Colors.empty()) {
        mesh.colors = static_cast<unsigned char*>(RL_MALLOC(vertexCount * 4));
        for (std::size_t i = 0; i < vertexCount; ++i) {
            mesh.colors[i * 4 + 0] = data.Colors[i].r;
            mesh.colors[i * 4 + 1] = data.Colors[i].g;
            mesh.colors[i * 4 + 2] = data.Colors[i].b;
            mesh.colors[i * 4 + 3] = data.Colors[i].a;
        }
    }
    if (!data.TexCoords.empty()) {
        mesh.texcoords = static_cast<float*>(RL_MALLOC(vertexCount * 2 * sizeof(float)));
        for (std::size_t i = 0; i < vertexCount; ++i) {
            mesh.texcoords[i * 2 + 0] = data.TexCoords[i].x;
            mesh.texcoords[i * 2 + 1] = data.TexCoords[i].y;
        }
    }
    for (std::size_t i = 0; i < data.Indices.size(); ++i) {
        mesh.indices[i] = static_cast<unsigned short>(data.Indices[i]);
    }

    ::UploadMesh(&mesh, false);
    ::Model* model = new ::Model(::LoadModelFromMesh(mesh));
    GenerateTangents(model->meshes[0]);
    BRITE::ModelHandle handle = m_nextModelId++;
    m_models[handle] = model;
    return handle;
}

std::vector<unsigned int> RaylibRenderBackend::TexturesOwnedByModel(const std::vector<unsigned int>& boundTextureIds,
                                                                    unsigned int placeholderTextureId) {
    std::vector<unsigned int> owned;
    for (const unsigned int id : boundTextureIds) {
        if (id == 0 || id == placeholderTextureId)
            continue;
        if (std::find(owned.begin(), owned.end(), id) == owned.end())
            owned.push_back(id);
    }
    return owned;
}

void RaylibRenderBackend::UnloadModel(BRITE::ModelHandle model) {
    auto it = m_models.find(model);
    if (it != m_models.end()) {
        ::Model* rlModel = static_cast<::Model*>(it->second);
        // raylib's UnloadModel frees a model's meshes but leaves its textures to
        // the caller. The textures a loader created for the model's materials are
        // the model's alone -- a draw command's textures are never written into
        // it -- so they go with it.
        std::vector<unsigned int> bound;
        for (int m = 0; m < rlModel->materialCount; ++m) {
            for (int map = 0; map < MATERIAL_MAPS_PER_MATERIAL; ++map)
                bound.push_back(rlModel->materials[m].maps[map].texture.id);
        }
        for (const unsigned int id : TexturesOwnedByModel(bound, ::rlGetTextureIdDefault()))
            ::rlUnloadTexture(id);
        ::UnloadModel(*rlModel);
        delete rlModel;
        m_models.erase(it);
        m_fileMaterials.erase(model);
    }
}

unsigned char RaylibRenderBackend::ModulateChannel(unsigned char a, unsigned char b) {
    return static_cast<unsigned char>((static_cast<int>(a) * static_cast<int>(b) + 127) / 255);
}

bool RaylibRenderBackend::ReadModelMeshes(BRITE::ModelHandle model, std::vector<BRITE::MeshData>& meshes) {
    meshes.clear();
    auto it = m_models.find(model);
    if (it == m_models.end())
        return false;
    const ::Model* rlModel = static_cast<const ::Model*>(it->second);

    // raylib keeps each mesh's arrays on the CPU after uploading them, and its
    // file loaders have already applied every node's transform to them. Both
    // LoadModel and LoadModelFromMesh set the model's own transform to the
    // identity, and nothing in this backend changes it, so the arrays are in
    // the model's space as they stand.
    meshes.resize(static_cast<std::size_t>(rlModel->meshCount));
    for (int m = 0; m < rlModel->meshCount; ++m) {
        const ::Mesh& mesh = rlModel->meshes[m];
        BRITE::MeshData& out = meshes[static_cast<std::size_t>(m)];
        if (mesh.vertices == nullptr || mesh.vertexCount <= 0)
            continue; // a mesh whose file held no positions raylib could read
        const std::size_t vertexCount = static_cast<std::size_t>(mesh.vertexCount);

        out.Positions.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
            out.Positions[i] = {mesh.vertices[i * 3 + 0], mesh.vertices[i * 3 + 1], mesh.vertices[i * 3 + 2]};

        // A node's scale reaches the normals through the inverse-transpose,
        // which raylib does not renormalise afterwards; the shader does, so
        // the model draws right, but a caller is promised unit normals.
        if (mesh.normals != nullptr) {
            out.Normals.resize(vertexCount);
            for (std::size_t i = 0; i < vertexCount; ++i) {
                const BRITE::Vector3 n = {mesh.normals[i * 3 + 0], mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2]};
                const float length = BRITE::Math::Length(n);
                out.Normals[i] = length > 0.0f ? BRITE::Vector3{n.x / length, n.y / length, n.z / length} : n;
            }
        }

        // The draw multiplies the material's colour by the vertex colour; the
        // read-back folds that product into the vertex colour, so a plain white
        // material draws it the same.
        const ::Color base = rlModel->materials[rlModel->meshMaterial[m]].maps[MATERIAL_MAP_DIFFUSE].color;
        out.Colors.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i) {
            const unsigned char* vertex = mesh.colors != nullptr ? &mesh.colors[i * 4] : nullptr;
            auto channel = [&](int c, unsigned char own) {
                return ModulateChannel(vertex != nullptr ? vertex[c] : 255, own);
            };
            out.Colors[i] = {channel(0, base.r), channel(1, base.g), channel(2, base.b), channel(3, base.a)};
        }

        // The first set of texture coordinates, when the mesh has one. A second
        // set (glTF's TEXCOORD_1) has no place in MeshData and is left behind.
        if (mesh.texcoords != nullptr) {
            out.TexCoords.resize(vertexCount);
            for (std::size_t i = 0; i < vertexCount; ++i)
                out.TexCoords[i] = {mesh.texcoords[i * 2 + 0], mesh.texcoords[i * 2 + 1]};
        }

        // Without an index list raylib draws the vertices in order, three to a
        // triangle; a trailing one or two make no triangle and are not drawn.
        const std::size_t indexCount =
            mesh.indices != nullptr ? static_cast<std::size_t>(mesh.triangleCount) * 3 : vertexCount / 3 * 3;
        out.Indices.resize(indexCount);
        for (std::size_t i = 0; i < indexCount; ++i)
            out.Indices[i] = mesh.indices != nullptr ? mesh.indices[i] : static_cast<std::uint32_t>(i);
    }
    return true;
}

BRITE::ShaderHandle RaylibRenderBackend::LoadShader(const char* vsFileName, const char* fsFileName) {
    ::Shader* shader = new ::Shader(::LoadShader(vsFileName, fsFileName));
    BRITE::ShaderHandle handle = m_nextShaderId++;
    m_shaders[handle] = shader;
    return handle;
}

BRITE::ShaderHandle RaylibRenderBackend::LoadShaderFromMemory(const char* vsCode, const char* fsCode) {
    ::Shader* shader = new ::Shader(::LoadShaderFromMemory(vsCode, fsCode));
    BRITE::ShaderHandle handle = m_nextShaderId++;
    m_shaders[handle] = shader;
    return handle;
}

void RaylibRenderBackend::UnloadShader(BRITE::ShaderHandle shader) {
    auto it = m_shaders.find(shader);
    if (it != m_shaders.end()) {
        ::Shader* rlShader = static_cast<::Shader*>(it->second);
        ::UnloadShader(*rlShader);
        delete rlShader;
        m_shaders.erase(it);
    }
}

int RaylibRenderBackend::GetShaderLocation(BRITE::ShaderHandle shader, const char* uniformName) {
    auto it = m_shaders.find(shader);
    if (it != m_shaders.end()) {
        ::Shader* rlShader = static_cast<::Shader*>(it->second);
        return ::GetShaderLocation(*rlShader, uniformName);
    }
    return -1;
}

void RaylibRenderBackend::SetShaderValue(BRITE::ShaderHandle shader, int locIndex, const void* value,
                                         ShaderUniformDataType uniformType) {
    auto it = m_shaders.find(shader);
    if (it != m_shaders.end()) {
        ::Shader* rlShader = static_cast<::Shader*>(it->second);

        int rlUniformType = 0;
        switch (uniformType) {
        case ShaderUniformDataType::Float:
            rlUniformType = SHADER_UNIFORM_FLOAT;
            break;
        case ShaderUniformDataType::Vec2:
            rlUniformType = SHADER_UNIFORM_VEC2;
            break;
        case ShaderUniformDataType::Vec3:
            rlUniformType = SHADER_UNIFORM_VEC3;
            break;
        case ShaderUniformDataType::Vec4:
            rlUniformType = SHADER_UNIFORM_VEC4;
            break;
        case ShaderUniformDataType::Int:
            rlUniformType = SHADER_UNIFORM_INT;
            break;
        case ShaderUniformDataType::IVec2:
            rlUniformType = SHADER_UNIFORM_IVEC2;
            break;
        case ShaderUniformDataType::IVec3:
            rlUniformType = SHADER_UNIFORM_IVEC3;
            break;
        case ShaderUniformDataType::IVec4:
            rlUniformType = SHADER_UNIFORM_IVEC4;
            break;
        case ShaderUniformDataType::Sampler2D:
            rlUniformType = SHADER_UNIFORM_SAMPLER2D;
            break;
        }

        ::SetShaderValue(*rlShader, locIndex, value, rlUniformType);
    }
}

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
