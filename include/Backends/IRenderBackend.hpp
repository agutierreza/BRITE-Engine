#pragma once

#include "Math/BriteMath.hpp"
#include "MeshData.hpp"
#include "RenderPass.hpp"
#include <vector>

namespace BRITE {
namespace Backends {

enum class ShaderUniformDataType { Float = 0, Vec2, Vec3, Vec4, Int, IVec2, IVec3, IVec4, Sampler2D };

class IRenderBackend {
  public:
    virtual ~IRenderBackend() = default;

    virtual void SubmitRenderPass(const BRITE::RenderPass& pass) = 0;

    // We also need some way to create/destroy render targets if the application uses internal resolution scaling
    virtual BRITE::TextureHandle LoadRenderTexture(int width, int height) = 0;
    virtual void UnloadRenderTexture(BRITE::TextureHandle target) = 0;

    // Read a render texture back to the CPU, top row first, as RGBA8. For tools
    // that write a picture to disk; nothing a frame should do.
    virtual bool ReadRenderTexture(BRITE::TextureHandle target, int& width, int& height,
                                   std::vector<BRITE::Color>& pixels) = 0;

    virtual BRITE::TextureHandle LoadTexture(const char* fileName) = 0;
    virtual void UnloadTexture(BRITE::TextureHandle texture) = 0;

    virtual BRITE::ModelHandle LoadModel(const char* fileName) = 0;
    // A model from geometry generated in code. Returns NullModelHandle, and
    // logs why, if the data is malformed: mismatched array lengths, an index
    // out of range, or more vertices than the backend can index in one mesh.
    virtual BRITE::ModelHandle LoadModelFromMesh(const BRITE::MeshData& mesh) = 0;
    virtual void UnloadModel(BRITE::ModelHandle model) = 0;

    virtual BRITE::ShaderHandle LoadShader(const char* vsFileName, const char* fsFileName) = 0;
    virtual BRITE::ShaderHandle LoadShaderFromMemory(const char* vsCode, const char* fsCode) = 0;
    virtual void UnloadShader(BRITE::ShaderHandle shader) = 0;

    virtual BRITE::EnvironmentMap LoadEnvironmentMap(const char* hdrFileName) = 0;
    virtual void UnloadEnvironmentMap(BRITE::EnvironmentMap envMap) = 0;

    virtual int GetShaderLocation(BRITE::ShaderHandle shader, const char* uniformName) = 0;
    virtual void SetShaderValue(BRITE::ShaderHandle shader, int locIndex, const void* value,
                                ShaderUniformDataType uniformType) = 0;
};

} // namespace Backends
} // namespace BRITE
