#pragma once

#include "Math/BriteMath.hpp"
#include "MeshData.hpp"
#include "RenderPass.hpp"
#include <vector>

namespace BRITE {
namespace Backends {

enum class ShaderUniformDataType { Float = 0, Vec2, Vec3, Vec4, Int, IVec2, IVec3, IVec4, Sampler2D };

// How a texture is read between and across its texels.
enum class SamplingFilter {
    Point,     // the nearest texel: crisp up close, grainy and shimmering far away
    Bilinear,  // a blend of the four nearest texels, from the full-size texture only
    Trilinear, // mipmaps are generated, and the two levels nearest the surface's
               // distance are blended: for a texture repeated across a large surface
};

// What a texture coordinate outside 0..1 reads.
enum class SamplingWrap {
    Repeat, // the texture again, as many times as the coordinates run
    Clamp,  // the texel at the nearest edge
};

// The defaults are what a texture has when it is loaded, so a texture no one
// sets reads exactly as it always has.
struct TextureSampling {
    SamplingFilter Filter = SamplingFilter::Point;
    // Anisotropic filtering: how many samples a surface seen at a grazing angle
    // may take along its slope. 1 is none; 4 to 16 are usual. A device honours
    // up to its own maximum.
    int Anisotropy = 1;
    SamplingWrap Wrap = SamplingWrap::Repeat;
};

class IRenderBackend {
  public:
    virtual ~IRenderBackend() = default;

    virtual void SubmitRenderPass(const BRITE::RenderPass& pass) = 0;

    // We also need some way to create/destroy render targets if the application uses internal resolution scaling
    virtual BRITE::TextureHandle LoadRenderTexture(int width, int height) = 0;
    // A render texture drawn with `samples` samples per pixel -- multisample
    // anti-aliasing for a pass that targets it. Each pass drawn into it is
    // resolved to one colour per pixel when the pass ends, so everything that
    // reads it -- a sprite, a post-process pass, ReadRenderTexture -- reads the
    // smoothed picture. A count the device cannot give is lowered to its most;
    // 1 or less is an ordinary render texture. A backend that cannot
    // multisample returns an ordinary one, which is what this default does, so
    // that an implementation written before it existed still builds. Unloaded
    // with UnloadRenderTexture.
    virtual BRITE::TextureHandle LoadMultisampledRenderTexture(int width, int height, int samples) {
        (void)samples;
        return LoadRenderTexture(width, height);
    }
    virtual void UnloadRenderTexture(BRITE::TextureHandle target) = 0;

    // Read a render texture back to the CPU, top row first, as RGBA8. For tools
    // that write a picture to disk; nothing a frame should do.
    virtual bool ReadRenderTexture(BRITE::TextureHandle target, int& width, int& height,
                                   std::vector<BRITE::Color>& pixels) = 0;

    virtual BRITE::TextureHandle LoadTexture(const char* fileName) = 0;
    virtual void UnloadTexture(BRITE::TextureHandle texture) = 0;

    // How a texture is sampled from now on, by every draw that names it.
    // Returns false when the handle names no texture, or names a render
    // texture (whose mipmaps would go stale with every pass drawn into it) --
    // or when the backend cannot, which is what these defaults do, so that an
    // implementation written before they existed still builds.
    virtual bool SetTextureSampling(BRITE::TextureHandle texture, const TextureSampling& sampling) {
        (void)texture;
        (void)sampling;
        return false;
    }
    // The same for every texture a loaded model brought with it: its
    // materials' own textures. A texture a draw names in its material is a
    // handle of its own, set through SetTextureSampling. Returns false when the
    // handle names no model; a model with no textures of its own is true.
    virtual bool SetModelTextureSampling(BRITE::ModelHandle model, const TextureSampling& sampling) {
        (void)model;
        (void)sampling;
        return false;
    }

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
