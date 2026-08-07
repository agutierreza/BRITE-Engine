#pragma once

#include "Math/BriteMath.hpp"

#include "Math/BriteMath.hpp"

namespace BRITE {
namespace Backends {

class IRenderBackend {
  public:
    virtual ~IRenderBackend() = default;

    virtual void BeginDrawing() = 0;
    virtual void EndDrawing() = 0;

    virtual void BeginTextureMode(BRITE::TextureHandle renderTarget) = 0;
    virtual void EndTextureMode() = 0;

    virtual void BeginMode2D(const BRITE::Camera2D& camera) = 0;
    virtual void EndMode2D() = 0;

    virtual void ClearBackground(BRITE::Color color) = 0;

    virtual void DrawSprite(BRITE::TextureHandle texture, BRITE::Rectangle source, BRITE::Rectangle dest,
                            BRITE::Vector2 origin, float rotationDeg, BRITE::Color tint) = 0;

    // We also need some way to create/destroy render targets if the application uses internal resolution scaling
    virtual BRITE::TextureHandle LoadRenderTexture(int width, int height) = 0;
    virtual void UnloadRenderTexture(BRITE::TextureHandle target) = 0;

    virtual BRITE::TextureHandle LoadTexture(const char* fileName) = 0;
    virtual void UnloadTexture(BRITE::TextureHandle texture) = 0;
};

} // namespace Backends
} // namespace BRITE
