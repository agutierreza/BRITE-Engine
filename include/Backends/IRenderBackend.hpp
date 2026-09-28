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

    // A model's geometry read back to the CPU, one MeshData per mesh in the
    // order the model draws them: for baking many copies of a model into a few
    // meshes, or deriving bounds or collision from it. Each mesh is in the
    // model's own space -- where a ModelDrawCommand at the origin, unrotated and
    // at unit scale, puts it -- with any node transforms of the file it came
    // from already applied. Loaded back through LoadModelFromMesh and drawn
    // with no tint, the meshes look as the model does under the same draw:
    //
    //   Normals  unit length; empty when the mesh has none, which CheckMeshData
    //            refuses until the caller supplies them.
    //   Colors   always one per vertex: the vertex colour (white where the mesh
    //            has none) times its material's base colour. A material's
    //            textures are not carried; a textured mesh reads back as its
    //            base colour alone, until the caller names the texture as the
    //            draw's AlbedoMap.
    //   TexCoords  the mesh's first set, or empty when it has none.
    //   Indices  always three per triangle; a mesh drawn without an index list
    //            reads back as 0, 1, 2, ...
    //
    // A mesh may hold more vertices than LoadModelFromMesh accepts, if its file
    // drew it without indices; CheckMeshData says so, and the caller splits it.
    // Returns false, with meshes empty, when the handle names no model -- or
    // when the backend cannot read geometry back, which is what this default
    // does, so that an implementation written before it existed still builds.
    virtual bool ReadModelMeshes(BRITE::ModelHandle model, std::vector<BRITE::MeshData>& meshes) {
        (void)model;
        meshes.clear();
        return false;
    }

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
