#include "Backends/Raylib/RaylibRenderBackend.hpp"
#include <raylib.h>

namespace BRITE {
namespace Backends {
namespace Raylib {

void RaylibRenderBackend::BeginDrawing() {
    ::BeginDrawing();
}

void RaylibRenderBackend::EndDrawing() {
    ::EndDrawing();
}

void RaylibRenderBackend::BeginTextureMode(BRITE::TextureHandle renderTarget) {
    if (renderTarget == BRITE::NullTextureHandle)
        return;
    auto it = m_textures.find(renderTarget);
    if (it != m_textures.end() && it->second.isRenderTexture) {
        RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
        ::BeginTextureMode(*rt);
    }
}

void RaylibRenderBackend::EndTextureMode() {
    ::EndTextureMode();
}

void RaylibRenderBackend::BeginMode2D(const BRITE::Camera2D& camera) {
    ::Camera2D rlCamera;
    rlCamera.offset = {camera.offset.x, camera.offset.y};
    rlCamera.target = {camera.target.x, camera.target.y};
    rlCamera.rotation = camera.rotation;
    rlCamera.zoom = camera.zoom;
    ::BeginMode2D(rlCamera);
}

void RaylibRenderBackend::EndMode2D() {
    ::EndMode2D();
}

void RaylibRenderBackend::ClearBackground(BRITE::Color color) {
    ::ClearBackground({color.r, color.g, color.b, color.a});
}

void RaylibRenderBackend::DrawSprite(BRITE::TextureHandle texture, BRITE::Rectangle source, BRITE::Rectangle dest,
                                     BRITE::Vector2 origin, float rotationDeg, BRITE::Color tint) {
    if (texture == BRITE::NullTextureHandle)
        return;

    auto it = m_textures.find(texture);
    if (it == m_textures.end())
        return;

    ::Texture2D rlTexture;
    if (it->second.isRenderTexture) {
        RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
        rlTexture = rt->texture;
    } else {
        ::Texture2D* tex = static_cast<::Texture2D*>(it->second.ptr);
        rlTexture = *tex;
    }

    ::Rectangle rlSource = {source.x, source.y, source.width, source.height};
    ::Rectangle rlDest = {dest.x, dest.y, dest.width, dest.height};
    ::Vector2 rlOrigin = {origin.x, origin.y};
    ::Color rlTint = {tint.r, tint.g, tint.b, tint.a};

    ::DrawTexturePro(rlTexture, rlSource, rlDest, rlOrigin, rotationDeg, rlTint);
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
