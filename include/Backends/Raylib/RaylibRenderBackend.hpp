#pragma once

#include "Backends/IRenderBackend.hpp"
#include <unordered_map>

namespace BRITE {
namespace Backends {
namespace Raylib {

class RaylibRenderBackend : public IRenderBackend {
  public:
    void SubmitRenderPass(const BRITE::RenderPass& pass) override;

    BRITE::TextureHandle LoadRenderTexture(int width, int height) override;
    void UnloadRenderTexture(BRITE::TextureHandle target) override;

    BRITE::TextureHandle LoadTexture(const char* fileName) override;
    void UnloadTexture(BRITE::TextureHandle texture) override;

    BRITE::ModelHandle LoadModel(const char* fileName) override;
    void UnloadModel(BRITE::ModelHandle model) override;

    BRITE::ShaderHandle LoadShader(const char* vsFileName, const char* fsFileName) override;
    BRITE::ShaderHandle LoadShaderFromMemory(const char* vsCode, const char* fsCode) override;
    void UnloadShader(BRITE::ShaderHandle shader) override;

    BRITE::EnvironmentMap LoadEnvironmentMap(const char* hdrFileName) override;
    void UnloadEnvironmentMap(BRITE::EnvironmentMap envMap) override;

    int GetShaderLocation(BRITE::ShaderHandle shader, const char* uniformName) override;
    void SetShaderValue(BRITE::ShaderHandle shader, int locIndex, const void* value,
                        ShaderUniformDataType uniformType) override;

  private:
    uint64_t m_nextId = 1;
    uint64_t m_nextShaderId = 1;

    BRITE::ShaderHandle m_pbrShader = BRITE::NullShaderHandle;

    // To cleanly separate Texture2D vs RenderTexture2D, we store a boolean.
    // Real implementation would just use a wrapper struct allocated on heap.
    struct TextureData {
        bool isRenderTexture;
        void* ptr; // Points to either a Texture2D or a RenderTexture2D
    };
    std::unordered_map<BRITE::TextureHandle, TextureData> m_textures;

    // Track raylib ::Shader objects by ShaderHandle
    std::unordered_map<BRITE::ShaderHandle, void*> m_shaders;

    uint64_t m_nextModelId = 1;
    // Track raylib ::Model objects by ModelHandle
    std::unordered_map<BRITE::ModelHandle, void*> m_models;
};

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
