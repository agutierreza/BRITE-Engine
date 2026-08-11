#pragma once

#include "Math/BriteMath.hpp"
#include "RenderPass.hpp"

namespace BRITE {
namespace Backends {

class IRenderBackend {
  public:
    virtual ~IRenderBackend() = default;

    virtual void SubmitRenderPass(const BRITE::RenderPass& pass) = 0;

    // We also need some way to create/destroy render targets if the application uses internal resolution scaling
    virtual BRITE::TextureHandle LoadRenderTexture(int width, int height) = 0;
    virtual void UnloadRenderTexture(BRITE::TextureHandle target) = 0;

    virtual BRITE::TextureHandle LoadTexture(const char* fileName) = 0;
    virtual void UnloadTexture(BRITE::TextureHandle texture) = 0;

    virtual BRITE::ShaderHandle LoadShader(const char* vsFileName, const char* fsFileName) = 0;
    virtual BRITE::ShaderHandle LoadShaderFromMemory(const char* vsCode, const char* fsCode) = 0;
    virtual void UnloadShader(BRITE::ShaderHandle shader) = 0;
};

} // namespace Backends
} // namespace BRITE
