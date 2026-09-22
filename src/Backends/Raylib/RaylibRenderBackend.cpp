#include "Backends/Raylib/RaylibRenderBackend.hpp"
#include "Backends/Raylib/PbrShaderSource.hpp"
#include <algorithm>
#include <cstring>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace BRITE {
namespace Backends {
namespace Raylib {

namespace {
// How many maps every raylib Material holds: rmodels.c's MAX_MATERIAL_MAPS, which
// raylib.h does not export. Each material's maps array is allocated this long and
// DrawMesh reads this many, so a copy of one must be exactly this long.
constexpr int MATERIAL_MAPS_PER_MATERIAL = 12;
} // namespace

void RaylibRenderBackend::SubmitRenderPass(const BRITE::RenderPass& pass) {
    if (pass.TargetFramebuffer != BRITE::NullTextureHandle) {
        auto it = m_textures.find(pass.TargetFramebuffer);
        if (it != m_textures.end() && it->second.isRenderTexture) {
            RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
            ::BeginTextureMode(*rt);
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
            const unsigned int ownAlbedo = maps[MATERIAL_MAP_ALBEDO].texture.id;
            const bool modelHasAlbedo = ownAlbedo != 0 && ownAlbedo != ::rlGetTextureIdDefault();

            for (const auto& [handle, mapIndex] : overrides) {
                if (const ::Texture2D* texture = commandTexture(handle))
                    maps[mapIndex].texture = *texture;
            }
            // The environment belongs to the pass, never to the model.
            maps[MATERIAL_MAP_IRRADIANCE].texture = irradiance ? *irradiance : ::Texture2D{0};
            maps[MATERIAL_MAP_PREFILTER].texture = prefilter ? *prefilter : ::Texture2D{0};

            ApplyMaterial(cmd.Material, modelHasAlbedo);

            // The draw tint times the material's own colour, as DrawModelEx does.
            const ::Color ownColour = maps[MATERIAL_MAP_DIFFUSE].color;
            maps[MATERIAL_MAP_DIFFUSE].color = {
                static_cast<unsigned char>((static_cast<int>(ownColour.r) * tint.r) / 255),
                static_cast<unsigned char>((static_cast<int>(ownColour.g) * tint.g) / 255),
                static_cast<unsigned char>((static_cast<int>(ownColour.b) * tint.b) / 255),
                static_cast<unsigned char>((static_cast<int>(ownColour.a) * tint.a) / 255)};

            ::DrawMesh(rlModel->meshes[m], material, transform);
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
    shader->locs[SHADER_LOC_MAP_METALNESS] = ::GetShaderLocation(*shader, "mraMap");
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
    m_pbrLocs.useTexMRA = loc("useTexMRA");
    m_pbrLocs.useTexEmissive = loc("useTexEmissive");
    m_pbrLocs.metallicValue = loc("metallicValue");
    m_pbrLocs.roughnessValue = loc("roughnessValue");
    m_pbrLocs.aoValue = loc("aoValue");
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

    const int useIBL = pass.Environment.IrradianceMap != BRITE::NullTextureHandle ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.useIBL, &useIBL, SHADER_UNIFORM_INT);

    if (pass.Camera3DPtr) {
        const float cameraPos[3] = {pass.Camera3DPtr->position.x, pass.Camera3DPtr->position.y,
                                    pass.Camera3DPtr->position.z};
        ::SetShaderValue(*shader, m_pbrLocs.viewPos, cameraPos, SHADER_UNIFORM_VEC3);
    }
}

void RaylibRenderBackend::ApplyMaterial(const BRITE::PBRMaterial& material, bool modelHasAlbedo) {
    ::Shader* shader = static_cast<::Shader*>(m_shaders[m_pbrShader]);

    // Which maps are bound. The shader used to assume all of them, so a model
    // with no albedo texture sampled whatever was on texture unit 0 -- black on
    // the first draw -- instead of its colour. An albedo texture is the draw
    // command's or, failing that, the one the model's own material brought; the
    // other maps come from the draw command alone.
    const int useAlbedo = (material.AlbedoMap != BRITE::NullTextureHandle || modelHasAlbedo) ? 1 : 0;
    const int useNormal = material.NormalMap != BRITE::NullTextureHandle ? 1 : 0;
    const int useMRA =
        (material.MetallicMap != BRITE::NullTextureHandle || material.RoughnessMap != BRITE::NullTextureHandle) ? 1 : 0;
    const int useEmissive = material.EmissionMap != BRITE::NullTextureHandle ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.useTexAlbedo, &useAlbedo, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexNormal, &useNormal, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexMRA, &useMRA, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexEmissive, &useEmissive, SHADER_UNIFORM_INT);

    ::SetShaderValue(*shader, m_pbrLocs.metallicValue, &material.Metallic, SHADER_UNIFORM_FLOAT);
    ::SetShaderValue(*shader, m_pbrLocs.roughnessValue, &material.Roughness, SHADER_UNIFORM_FLOAT);
    // No occlusion map means nothing is occluded. Left unset this read as zero
    // and multiplied the whole ambient term away.
    const float ao = 1.0f;
    ::SetShaderValue(*shader, m_pbrLocs.aoValue, &ao, SHADER_UNIFORM_FLOAT);
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

void RaylibRenderBackend::UnloadRenderTexture(BRITE::TextureHandle target) {
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
    return handle;
}

BRITE::ModelHandle RaylibRenderBackend::LoadModelFromMesh(const BRITE::MeshData& data) {
    // The refusals live in CheckMeshData, a pure function with its own tests;
    // this only reports them. Everything below it may assume a well-formed mesh.
    const BRITE::MeshDataProblem problem = BRITE::CheckMeshData(data, MaxVerticesPerMesh);
    if (problem != BRITE::MeshDataProblem::None) {
        spdlog::error("BRITE: LoadModelFromMesh refused a mesh because {} ({} positions, {} normals, {} colours, "
                      "{} indices; at most {} vertices per mesh)",
                      BRITE::Describe(problem), data.Positions.size(), data.Normals.size(), data.Colors.size(),
                      data.Indices.size(), MaxVerticesPerMesh);
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
    for (std::size_t i = 0; i < data.Indices.size(); ++i) {
        mesh.indices[i] = static_cast<unsigned short>(data.Indices[i]);
    }

    ::UploadMesh(&mesh, false);
    ::Model* model = new ::Model(::LoadModelFromMesh(mesh));
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
    }
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
