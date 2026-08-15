#include "Backends/Raylib/RaylibRenderBackend.hpp"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

namespace BRITE {
namespace Backends {
namespace Raylib {

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

    for (const auto& cmd : pass.ModelCommands) {
        if (cmd.Model == BRITE::NullModelHandle)
            continue;
        auto it = m_models.find(cmd.Model);
        if (it == m_models.end())
            continue;

        ::Model* rlModel = static_cast<::Model*>(it->second);

        if (rlModel->materialCount > 0) {
            auto applyTex = [&](TextureHandle handle, int mapIndex) {
                if (handle != BRITE::NullTextureHandle) {
                    auto texIt = m_textures.find(handle);
                    if (texIt != m_textures.end() && !texIt->second.isRenderTexture) {
                        rlModel->materials[0].maps[mapIndex].texture = *static_cast<::Texture2D*>(texIt->second.ptr);
                    }
                }
            };
            applyTex(cmd.Material.AlbedoMap, MATERIAL_MAP_ALBEDO);
            applyTex(cmd.Material.NormalMap, MATERIAL_MAP_NORMAL);
            applyTex(cmd.Material.RoughnessMap, MATERIAL_MAP_ROUGHNESS);
            applyTex(cmd.Material.MetallicMap, MATERIAL_MAP_METALNESS);
            applyTex(cmd.Material.EmissionMap, MATERIAL_MAP_EMISSION);
            applyTex(cmd.Material.AOMap, MATERIAL_MAP_OCCLUSION);
        }

        ::Vector3 rlAxis;
        float rlAngle;
        ::Quaternion rlQuat = {cmd.Rotation.x, cmd.Rotation.y, cmd.Rotation.z, cmd.Rotation.w};
        ::QuaternionToAxisAngle(rlQuat, &rlAxis, &rlAngle);
        rlAngle *= RAD2DEG;

        ::Vector3 rlPos = {cmd.Position.x, cmd.Position.y, cmd.Position.z};
        ::Vector3 rlScale = {cmd.Scale.x, cmd.Scale.y, cmd.Scale.z};
        ::Color rlTint = {cmd.Material.AlbedoTint.r, cmd.Material.AlbedoTint.g, cmd.Material.AlbedoTint.b,
                          cmd.Material.AlbedoTint.a};

        ::DrawModelEx(*rlModel, rlPos, rlAxis, rlAngle, rlScale, rlTint);
    }

    if (pass.Camera3DPtr) {
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

BRITE::ModelHandle RaylibRenderBackend::LoadModel(const char* fileName) {
    ::Model* model = new ::Model(::LoadModel(fileName));
    BRITE::ModelHandle handle = m_nextModelId++;
    m_models[handle] = model;
    return handle;
}

void RaylibRenderBackend::UnloadModel(BRITE::ModelHandle model) {
    auto it = m_models.find(model);
    if (it != m_models.end()) {
        ::Model* rlModel = static_cast<::Model*>(it->second);
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
