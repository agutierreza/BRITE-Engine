#include "Backends/Raylib/RaylibRenderBackend.hpp"
#include <raylib.h>

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
        if (cmd.Texture == BRITE::NullTextureHandle)
            continue;
        auto it = m_textures.find(cmd.Texture);
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
        ::Color rlTint = {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a};

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

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
