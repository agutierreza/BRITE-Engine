#pragma once

#include "Backends/GltfMaterials.hpp"
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

    // Which of the texture ids bound in a model's materials belong to the model
    // and are freed with it: every id once, except 0 (no texture) and the
    // placeholder a loader puts in a slot it has nothing for.
    static std::vector<unsigned int> TexturesOwnedByModel(const std::vector<unsigned int>& boundTextureIds,
                                                          unsigned int placeholderTextureId);

    void SubmitRenderPass(const BRITE::RenderPass& pass) override;

    BRITE::TextureHandle LoadRenderTexture(int width, int height) override;
    BRITE::TextureHandle LoadMultisampledRenderTexture(int width, int height, int samples) override;
    void UnloadRenderTexture(BRITE::TextureHandle target) override;

    // How many samples per pixel a render texture is drawn with: 1 for an
    // ordinary one, 0 when the handle names no render texture. For tests and
    // tools; a draw never needs it.
    int RenderTextureSamples(BRITE::TextureHandle target) const;
    bool ReadRenderTexture(BRITE::TextureHandle target, int& width, int& height,
                           std::vector<BRITE::Color>& pixels) override;

    BRITE::TextureHandle LoadTexture(const char* fileName) override;
    void UnloadTexture(BRITE::TextureHandle texture) override;
    bool SetTextureSampling(BRITE::TextureHandle texture, const TextureSampling& sampling) override;
    bool SetModelTextureSampling(BRITE::ModelHandle model, const TextureSampling& sampling) override;

    // The OpenGL name behind a texture handle, or 0 when it names no plain
    // texture: for tests and tools that inspect a texture's state in the
    // graphics API itself.
    unsigned int NativeTextureId(BRITE::TextureHandle texture) const;

    BRITE::ModelHandle LoadModel(const char* fileName) override;
    BRITE::ModelHandle LoadModelFromMesh(const BRITE::MeshData& mesh) override;
    void UnloadModel(BRITE::ModelHandle model) override;
    bool ReadModelMeshes(BRITE::ModelHandle model, std::vector<BRITE::MeshData>& meshes) override;

    // One 8-bit colour channel times another, as the lit shader multiplies a
    // material's colour by a vertex colour, stored back in 8 bits: a * b / 255,
    // rounded to the nearest. (a * b / 255 is never exactly half-way, since 255
    // is odd, so "nearest" needs no tie rule.)
    static unsigned char ModulateChannel(unsigned char a, unsigned char b);

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
    // The per-mesh material scalars and which texture maps are bound.
    // modelHasAlbedo, modelHasEmission: the mesh's own material brought an
    // albedo texture, an emissive texture.
    // fileMaterial: what the mesh's file said of its material, or null.
    // Returns whether the mesh is drawn double-sided.
    bool ApplyMaterial(const BRITE::PBRMaterial& material, bool modelHasAlbedo, bool modelHasEmission,
                       const BRITE::GltfMaterialInfo* fileMaterial);
    // What a loaded model's file said of the material in raylib slot `slot`,
    // or null: a model built in code, a file that is not glTF, slot 0 (raylib's
    // own default material), or a file whose materials did not read.
    const BRITE::GltfMaterialInfo* FileMaterial(BRITE::ModelHandle model, int slot) const;

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
        int unlit = -1;
        int alphaMask = -1;
        int alphaCutoff = -1;
        int doubleSided = -1;
        int emissiveLight = -1;
        int fogEnabled = -1;
        int fogColor = -1;
        int fogStart = -1;
        int fogEnd = -1;
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

    // A multisampled render texture's drawing surface. The handle's own entry
    // in m_textures is an ordinary render texture, the one a pass is resolved
    // into and every reader reads; passes draw into this framebuffer instead.
    struct MultisampledTarget {
        unsigned int framebuffer = 0;
        unsigned int colour = 0; // renderbuffers, `samples` samples a pixel
        unsigned int depth = 0;
        int samples = 0;
    };
    std::unordered_map<BRITE::TextureHandle, MultisampledTarget> m_multisampled;

    // Track raylib ::Shader objects by ShaderHandle
    std::unordered_map<BRITE::ShaderHandle, void*> m_shaders;

    uint64_t m_nextModelId = 1;
    // Track raylib ::Model objects by ModelHandle
    std::unordered_map<BRITE::ModelHandle, void*> m_models;
    // What a glTF model's file said of each material raylib does not read,
    // indexed by raylib's material slot: slot 0 is raylib's own default and the
    // file's material i is slot i + 1. Absent for any other model.
    std::unordered_map<BRITE::ModelHandle, std::vector<BRITE::GltfMaterialInfo>> m_fileMaterials;
};

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
