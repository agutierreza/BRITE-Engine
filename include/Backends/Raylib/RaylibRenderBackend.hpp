#pragma once

#include "Backends/IRenderBackend.hpp"
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace BRITE {
namespace Backends {
namespace Raylib {

class RaylibRenderBackend : public IRenderBackend {
  public:
    // The most vertices LoadModelFromMesh accepts in one mesh. raylib draws
    // every mesh with 16-bit indices, 0 to 65535, and 65535 (0xFFFF) is the
    // primitive-restart index, which WebGL 2 always honours. So the highest
    // index a portable mesh may use is 65534, which addresses 65535 vertices.
    static constexpr std::size_t MaxVerticesPerMesh = 65535;

    void SubmitRenderPass(const BRITE::RenderPass& pass) override;

    BRITE::TextureHandle LoadRenderTexture(int width, int height) override;
    void UnloadRenderTexture(BRITE::TextureHandle target) override;
    bool ReadRenderTexture(BRITE::TextureHandle target, int& width, int& height,
                           std::vector<BRITE::Color>& pixels) override;

    BRITE::TextureHandle LoadTexture(const char* fileName) override;
    void UnloadTexture(BRITE::TextureHandle texture) override;

    BRITE::ModelHandle LoadModel(const char* fileName) override;
    BRITE::ModelHandle LoadModelFromMesh(const BRITE::MeshData& mesh) override;
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
    // Compile the embedded PBR shader on first use and cache its uniform
    // locations. Idempotent.
    void EnsurePbrShader();
    // Hand the pass's lights, ambient and camera position to the PBR shader.
    // Once per pass, before the model commands.
    void ApplyPassLighting(const BRITE::RenderPass& pass);
    // The per-draw material scalars and which texture maps are bound.
    void ApplyMaterial(const BRITE::PBRMaterial& material);

    uint64_t m_nextId = 1;
    uint64_t m_nextShaderId = 1;

    BRITE::ShaderHandle m_pbrShader = BRITE::NullShaderHandle;

    // Uniform locations in the PBR shader, looked up once. -1 means the compiler
    // stripped that uniform (or the shader failed), and raylib ignores a set on
    // -1, so an absent uniform is harmless rather than fatal.
    struct PbrLocations {
        int numOfLights = -1;
        int useIBL = -1;
        int viewPos = -1;
        int ambientColor = -1;
        int ambient = -1;
        int useTexAlbedo = -1;
        int useTexNormal = -1;
        int useTexMRA = -1;
        int useTexEmissive = -1;
        int metallicValue = -1;
        int roughnessValue = -1;
        int aoValue = -1;
        struct LightLocations {
            int enabled = -1;
            int type = -1;
            int position = -1;
            int direction = -1;
            int color = -1;
            int intensity = -1;
        } lights[BRITE::MaxLightsPerPass];
    };
    PbrLocations m_pbrLocs;

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
