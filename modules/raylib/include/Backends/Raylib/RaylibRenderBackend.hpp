#pragma once

#include "Backends/GltfMaterials.hpp"
#include "Backends/IRenderBackend.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace BRITE {
namespace Backends {
namespace Raylib {

class PostChain;
class ShadowAtlas;
class InstanceStream;
class ReflectionBlurPasses;

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
    BRITE::TextureHandle LoadRenderTarget(const RenderTargetDescription& description) override;
    BRITE::TextureHandle RenderTargetDepth(BRITE::TextureHandle target) override;
    // A target made by LoadRenderTarget read back as it holds its colour: linear, unclamped floats, RGBA (an
    // R11G11B10F target's alpha reads 1), BOTTOM row first, as OpenGL holds it. For tests and tools.
    bool ReadRenderTargetLinear(BRITE::TextureHandle target, int& width, int& height, std::vector<float>& rgba);
    // The last HDR pass's float scene target, resolved, and its bloom's first level (every level's light summed), read
    // back the same way. The scene's colour is PREMULTIPLIED -- light times the alpha, the alpha the coverage the pass
    // built (RenderPass.hpp, PostSettings) -- so where it covered fully it is the light itself. False when no HDR pass
    // (or no bloom) has been drawn. For tests and tools.
    bool ReadLastHdrScene(int& width, int& height, std::vector<float>& rgba) const;
    bool ReadLastBloom(int& width, int& height, std::vector<float>& rgba) const;
    // How many float scene targets the chain has made: passes at sizes it already holds make none. For tests.
    int HdrSceneTargetsMade() const;
    // How many sets of the chain's targets (a scene target and its bloom levels, one size each) are held now, and how
    // many passes a set may stand unused before it is freed. For tests.
    int HdrTargetSetsHeld() const;
    static std::uint64_t HdrIdlePasses();
    // The last pass's planar reflection (RenderPass.hpp, ReflectionSettings) read back as floats, bottom row first,
    // and the size it was drawn at; false when the last pass drew none. And how many planar reflection targets (one a
    // size) are held now: they are freed after as many unused passes as the chain's sets. For tests and tools.
    bool ReadLastPlanarReflection(int& width, int& height, std::vector<float>& rgba) const;
    int PlanarReflectionTargetsHeld() const;
    // ITS BLUR (ReflectionBlurSettings): whether the last pass's planar reflection was blurred --
    // ReadLastPlanarReflection then reads it blurred, as the reflecting draws read it; how many of the blur's scratch
    // targets are held (one a reflection target blurred, freed with it) and how many have been made or tried; whether
    // its program has been asked for at all. And, RefuseReflectionBlurForTest, its program refused from now on, as on a
    // device whose compiler refuses it; RefuseReflectionBlurTargetsForTest, its targets, as on a device that cannot
    // make them. For tests and tools.
    bool LastPlanarReflectionBlurred() const;
    int ReflectionBlurTargetsHeld() const;
    int ReflectionBlurTargetsMade() const;
    bool ReflectionBlurProgramAsked() const {
        return m_reflectionBlur != nullptr;
    }
    // The last pass's planar reflection's minifying filter, as GL holds it -- what the reflecting draws read it with --
    // or 0 when the last pass drew none. For tests.
    int LastPlanarReflectionMinFilter() const;
    void RefuseReflectionBlurForTest();
    // ITS MULTISAMPLING (ReflectionSettings::Samples): how many samples a texel the last pass's planar reflection was
    // drawn with (1 for none, 0 when the last pass drew none); how many multisampled reflection targets have been made
    // or tried. And, RefuseReflectionMultisampleForTest, every multisampled reflection target refused from now on, as
    // on a device that cannot make one. For tests and tools.
    int LastPlanarReflectionSamples() const;
    int PlanarReflectionMultisampleTargetsTried() const;
    void RefuseReflectionMultisampleForTest() {
        m_refuseReflectionMultisampleForTest = true;
    }
    // For tests: every target the backend makes for itself -- the planar reflection's, the HDR chain's -- asked for
    // more samples than `most` is made with `most`, as on a device whose most that is; 0 lifts the cap. Process-wide,
    // as the device is: a test that sets it lifts it.
    static void CapMostSamplesForTest(int most);
    void RefuseReflectionBlurTargetsForTest() {
        m_refuseReflectionBlurTargetsForTest = true;
    }
    // MOTION BLUR (RenderPass.hpp, MotionBlurSettings): the last pass's streaks -- x and y in pixels, +y up, 1 in blue
    // for an exempt pixel, the distance along the view in alpha -- and its blurred picture before bloom and the tone
    // map (premultiplied, as the scene is), each read back bottom row first; false when the last pass drew no blur. How
    // many sets of its targets are held (0 or 1). And, RefuseMotionBlurForTest, its shaders refused from now on, as on
    // a device whose compiler refuses them; RefuseMotionBlurTargetsForTest, its targets, as on a device that cannot
    // make them. For tests and tools.
    bool ReadLastMotionVectors(int& width, int& height, std::vector<float>& rgba) const;
    bool ReadLastMotionBlurred(int& width, int& height, std::vector<float>& rgba) const;
    int MotionBlurTargetSetsHeld() const;
    // How many times a set of them has been made or tried at a new size: a size it holds, a size refused before, or a
    // change of MaxLength alone makes none. For tests.
    int MotionBlurTargetSetsMade() const;
    void RefuseMotionBlurForTest() {
        m_refuseMotionBlurForTest = true;
    }
    void RefuseMotionBlurTargetsForTest() {
        m_refuseMotionBlurTargetsForTest = true;
    }

    // What the last pass with shadows drew, one entry per cascade, nearest first; empty when the last pass drew none.
    // For tests and tools: the cascade's square in the light's view (its centre, snapped to whole texels, and its
    // half-size), a texel's size in world units, its far edge along the view, and how many caster draws it took: one
    // a mesh of a model draw, one a mesh of an instanced draw for all its copies that reach the cascade.
    struct ShadowCascadeReport {
        float CentreX = 0.0f;
        float CentreY = 0.0f;
        float Radius = 0.0f;
        float Texel = 0.0f;
        float FarEdge = 0.0f;
        int CasterDraws = 0;
    };
    const std::vector<ShadowCascadeReport>& LastShadowCascades() const {
        return m_shadowReport;
    }
    // How many draw calls the last pass's lit model meshes took -- a ModelDrawCommand's one per mesh, an
    // InstancedModelDrawCommand's one per mesh for all its copies -- its planar reflection's included, its shadows'
    // not. For tests and measurements.
    int LastPassModelDrawCalls() const {
        return m_modelDrawCalls;
    }
    // For tests: every instanced variant of the lit shader that has shadows fails to compile from now on, as on a
    // device that refuses it, so what a refusal does to the other draws can be seen.
    void RefuseInstancedShadowsForTest() {
        m_refuseInstancedShadowsForTest = true;
    }
    // THE LIT SHADER'S VARIANTS (pbr.fs): one program per feature set a pass asks for, compiled on first use. How many
    // exist, and whether the variant for these features has a uniform of this name -- a variant without shadows has
    // no shadow code at all. For tests and tools.
    std::size_t PbrVariantCount() const {
        return m_pbrVariants.size();
    }
    bool PbrVariantHasUniform(bool hdr, int shadowCascades, const char* uniformName);
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

    // SKELETAL ANIMATION, as raylib does it (ModelPose.hpp for the contract).
    //
    // CPU SKINNING: a posed draw transforms every vertex of the model by each of its up to four bones, and uploads the
    // posed positions and normals into the model's own vertex buffers just before its meshes are drawn. So a pass
    // pays vertices x posed draws in skinning and upload -- two draws of one model in one pass each pose and upload
    // in turn, and each draws its own pose -- and a draw at rest after a posed one uploads the rest geometry once.
    // A model with no skin costs nothing. Tangents stay as they were bound, so a normal map on a strongly bent part
    // reads slightly off.
    //
    // Clips are read from glTF and GLB files, which raylib samples at ClipSamplesPerSecond. A skinned IQM or M3D
    // model loads its bones and takes a pose from code, but no clips: raylib keeps their keyframes and not their
    // rate, so a time in seconds could not be honoured. raylib applies a skinned mesh's own node transform to its
    // vertices, which glTF says to ignore: export skinned meshes with an identity node, as exporters do by default.
    bool ReadModelSkeleton(BRITE::ModelHandle model, BRITE::ModelSkeleton& skeleton) override;
    int AddModelClips(BRITE::ModelHandle model, const char* fileName) override;
    bool ReadPosedModelMeshes(BRITE::ModelHandle model, const BRITE::ModelPose& pose,
                              std::vector<BRITE::MeshData>& meshes) override;

    // raylib samples every glTF clip at this many keyframes a second (GLTF_FRAMERATE in its rmodels.c, which no
    // header exports).
    static constexpr float ClipSamplesPerSecond = 60.0f;
    // The keyframe, fractional, that a time names on a clip of `keyframeCount` keyframes: the time in seconds times
    // the sample rate, held to the first and the last keyframe. Not a number is the first.
    static float ClipFrame(float seconds, int keyframeCount);
    // A blend's weight held to 0..1; not a number is 0, clip A alone.
    static float HeldBlendWeight(float weight);

    // One 8-bit colour channel times another, as the lit shader multiplies a
    // material's colour by a vertex colour, stored back in 8 bits: a * b / 255,
    // rounded to the nearest. (a * b / 255 is never exactly half-way, since 255
    // is odd, so "nearest" needs no tie rule.)
    static unsigned char ModulateChannel(unsigned char a, unsigned char b);

    // The material slot a mesh is drawn with, given the slot its model names
    // for it and how many materials the model holds: that slot when it names
    // one of them, else 0, raylib's own default material. Every read of a
    // mesh's material goes through it -- the material drawn, its file's
    // material info, the translucency test and the read-back -- so a bad slot
    // is slot 0 to all of them alike, and nothing reads out of range.
    static int MeshMaterialSlot(int slot, int materialCount);

    BRITE::ShaderHandle LoadShader(const char* vsFileName, const char* fsFileName) override;
    BRITE::ShaderHandle LoadShaderFromMemory(const char* vsCode, const char* fsCode) override;
    void UnloadShader(BRITE::ShaderHandle shader) override;

    BRITE::EnvironmentMap LoadEnvironmentMap(const char* hdrFileName) override;
    void UnloadEnvironmentMap(BRITE::EnvironmentMap envMap) override;

    int GetShaderLocation(BRITE::ShaderHandle shader, const char* uniformName) override;
    void SetShaderValue(BRITE::ShaderHandle shader, int locIndex, const void* value,
                        ShaderUniformDataType uniformType) override;

  private:
    // The feature set a lit draw's program is compiled for (pbr.fs, THE VARIANTS).
    struct PbrVariant {
        bool hdr = false;
        int shadowCascades = 0; // 0: no shadows
        int shadowFilter = 1;   // ShadowFilter
        bool reflections = false;
        bool instanced = false; // the vertex stage's: an instanced draw (pbr.vs)
        int Key() const {
            return (hdr ? 1 : 0) | (shadowCascades << 1) | (shadowFilter << 4) | (reflections ? 64 : 0) |
                   (instanced ? 128 : 0);
        }
    };
    // Compile the variant of the embedded PBR shader on first use, cache its uniform locations, and make it the one
    // m_pbrShader and m_pbrLocs name. A variant with shadows that fails to compile falls back to the same features
    // without them, logged once.
    void EnsurePbrShader(const PbrVariant& variant);
    // The pass's sun shadows drawn into the atlas, before any target is bound; false, and nothing drawn, when the pass
    // casts none. Fills m_shadowFrame for the lit draws that follow.
    bool RenderShadows(const BRITE::RenderPass& pass, int targetWidth, int targetHeight);
    // Whether one mesh of a model draw is translucent (RenderPass.hpp): its slot's file material, its own colour's
    // alpha.
    bool MeshTranslucent(const BRITE::ModelDrawCommand& cmd, int slot, unsigned char ownAlpha) const;
    // Hand the pass's lights, ambient and camera position to the PBR shader.
    // Once per pass, before the model commands.
    void ApplyPassLighting(const BRITE::RenderPass& pass);
    // The pass's 3D world, into whatever is bound; `hdr` for an HDR pass's float target (SubmitRenderPass).
    void Draw3D(const BRITE::RenderPass& pass, bool hdr);
    // The blend of the draw that follows: alpha (over) or additive -- into the chain's premultiplied scene with the
    // alpha kept as coverage (m_premultipliedScene), elsewhere raylib's own two modes.
    void UseBlend(bool additive) const;
    // The shader primitives and 3D lines are drawn through under an HDR pass, compiled on first use; false when the
    // device refused it.
    bool EnsureAuthoredColourShader();
    // The per-mesh material scalars and which texture maps are bound.
    // Which textures a mesh's own material brought with it, each a real
    // texture and not the loader's placeholder.
    struct OwnMaps {
        bool albedo = false;
        bool emission = false;
        bool metalness = false;
        bool roughness = false;
        bool occlusion = false;
        bool normal = false;
    };
    // ownMaps: what the mesh's own material brought.
    // fileMaterial: what the mesh's file said of its material, or null.
    // Returns whether the mesh is drawn double-sided.
    bool ApplyMaterial(const BRITE::PBRMaterial& material, const OwnMaps& ownMaps,
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
        int useTexMetallic = -1;
        int useTexRoughness = -1;
        int useTexOcclusion = -1;
        int useTexEmissive = -1;
        int metallicValue = -1;
        int roughnessValue = -1;
        int occlusionStrength = -1;
        int normalScale = -1;
        int unlit = -1;
        int alphaMask = -1;
        int alphaCutoff = -1;
        int alphaBlend = -1;
        int doubleSided = -1;
        int emissiveLight = -1;
        int fogEnabled = -1;
        int fogColor = -1;
        int fogStart = -1;
        int fogEnd = -1;
        int exposure = -1;
        int shadowLight = -1;
        int shadowMap = -1;
        int shadowTexelUV = -1;
        int shadowDepthBias = -1;
        int shadowNormalBias = -1;
        int viewForward = -1;
        int shadowMatrix[4] = {-1, -1, -1, -1};
        int shadowRect[4] = {-1, -1, -1, -1};
        int cascadeFar[4] = {-1, -1, -1, -1};
        int cascadeTexel[4] = {-1, -1, -1, -1};
        int reflection = -1;
        int reflectionF0 = -1;
        int reflectionDistortion = -1;
        int skyColor = -1;
        int planarMap = -1;
        int planarMatrix = -1;
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
    // Every variant compiled so far, by PbrVariant::Key: its shader and its locations.
    std::unordered_map<int, std::pair<BRITE::ShaderHandle, PbrLocations>> m_pbrVariants;

    // SUN SHADOWS: the atlas (RaylibPostChain.hpp), the depth pass's shader, each model's meshes' bounding spheres in
    // model space (found on first use), and what this pass's shadow pass left for its lit draws.
    std::shared_ptr<ShadowAtlas> m_shadowAtlas;
    BRITE::ShaderHandle m_shadowDepthShader = BRITE::NullShaderHandle;
    bool m_shadowDepthWorks = false;
    // The shadow features (cascade count and filter, ShadowVariantKey) the device refused the lit shader with: no later
    // pass asking for one of them draws a shadow map it cannot read. Another count or filter is still tried.
    std::vector<int> m_shadowVariantsRefused;
    // HDR is left out of the key on purpose: a refusal with HDR on also stops the same shadows with it off -- the
    // conservative side, since the shadow code is the same in both.
    static int ShadowVariantKey(int cascades, int filter) {
        return cascades | (filter << 3);
    }
    int m_shadowDepthAlphaMask = -1;
    int m_shadowDepthAlphaCutoff = -1;
    // The depth pass's instanced variant (shadow_depth.vs, BRITE_INSTANCED), compiled beside it, and its locations.
    BRITE::ShaderHandle m_shadowDepthInstancedShader = BRITE::NullShaderHandle;
    bool m_shadowDepthInstancedWorks = false;
    int m_shadowDepthInstancedAlphaMask = -1;
    int m_shadowDepthInstancedAlphaCutoff = -1;
    struct MeshSphere {
        BRITE::Vector3 centre;
        float radius;
    };
    std::unordered_map<BRITE::ModelHandle, std::vector<MeshSphere>> m_meshSpheres;
    struct ShadowFrame;
    std::shared_ptr<ShadowFrame> m_shadowFrame;
    std::vector<ShadowCascadeReport> m_shadowReport;

    // INSTANCED DRAWS (RenderPass.hpp, InstancedModelDrawCommand): the stream their copies' attributes go through
    // (RaylibPostChain.hpp), made on the first, and the draw calls the pass's lit model meshes took.
    std::shared_ptr<InstanceStream> m_instances;
    int m_modelDrawCalls = 0;
    bool m_refuseInstancedShadowsForTest = false;
    // The pose index a skinned model takes in an instanced draw -- at rest, and never one a ModelDrawCommand's index
    // reaches -- so PoseForDraw keeps the two apart.
    static constexpr std::size_t InstancedPoseIndex = static_cast<std::size_t>(-1);

    // THE PLANAR REFLECTION (ReflectionSettings): its targets, kept by size, and the mirrored camera's view and
    // projection for the reflecting draws of the pass being drawn. m_planarThisPass: the pass drew one, and its draws
    // read it.
    struct PlanarState;
    std::shared_ptr<PlanarState> m_planar;
    bool m_planarThisPass = false;
    // Its blur (ReflectionBlurSettings): the program, made on the first pass that blurs, and whether this pass's
    // reflection was blurred.
    std::shared_ptr<ReflectionBlurPasses> m_reflectionBlur;
    bool m_planarBlurredThisPass = false;
    bool m_refuseReflectionBlurTargetsForTest = false;
    bool m_refuseReflectionMultisampleForTest = false;
    // The pass's lit draws have its environment map bound (ApplyPassLighting), to be unbound when they are done.
    bool m_environmentThisPass = false;
    // A projection Draw3D puts in place of its camera's own once BeginMode3D has made it: the mirrored camera's, with
    // its near plane on the reflecting plane.
    bool m_projectionOverride = false;
    float m_projection[16] = {};
    bool RenderPlanarReflection(const BRITE::RenderPass& pass, int targetWidth, int targetHeight, bool hdr);

    // MOTION BLUR (MotionBlurSettings): whether the pass blurs this frame -- enabled, not a cut, and something moves --
    // and the blur itself, drawn after the scene is resolved and before its bloom: the moving and exempt draws'
    // velocity re-drawn, then the chain's full-screen passes.
    bool WantsMotionBlur(const BRITE::RenderPass& pass, int targetWidth, int targetHeight) const;
    void RenderMotionBlur(const BRITE::RenderPass& pass, int targetWidth, int targetHeight);
    bool m_refuseMotionBlurForTest = false;
    bool m_refuseMotionBlurTargetsForTest = false;
    // The instanced copies' placements as the last Draw3D streamed them, for the velocity draw.
    struct DrawnInstances;
    std::shared_ptr<DrawnInstances> m_drawnInstances;

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

    // The HDR chain (RaylibPostChain.hpp), made on the first HDR pass; defined beside the code that uses it.
    std::shared_ptr<PostChain> m_post;
    // While a pass draws into the chain's scene target, which holds PREMULTIPLIED colour (RaylibPostChain.hpp): every
    // blend then keeps the target's alpha as the coverage it is, so the picture lies over a target that was not cleared
    // as an 8-bit pass would lay it.
    bool m_premultipliedScene = false;
    BRITE::ShaderHandle m_authoredColourShader = BRITE::NullShaderHandle;
    bool m_authoredColourWorks = false;
    int m_authoredColourExposure = -1;
    // The render targets LoadRenderTarget made, which own their objects.
    struct FormattedTarget;
    std::unordered_map<BRITE::TextureHandle, std::shared_ptr<FormattedTarget>> m_formattedTargets;

    // Track raylib ::Shader objects by ShaderHandle
    std::unordered_map<BRITE::ShaderHandle, void*> m_shaders;

    uint64_t m_nextModelId = 1;
    // Track raylib ::Model objects by ModelHandle
    std::unordered_map<BRITE::ModelHandle, void*> m_models;
    // What a glTF model's file said of each material raylib does not read,
    // indexed by raylib's material slot: slot 0 is raylib's own default and the
    // file's material i is slot i + 1. Absent for any other model.
    std::unordered_map<BRITE::ModelHandle, std::vector<BRITE::GltfMaterialInfo>> m_fileMaterials;
    // The centre of each mesh's bounding box in model space, found the first time a mesh is drawn translucent and
    // read to sort it; absent until then.
    std::unordered_map<BRITE::ModelHandle, std::vector<BRITE::Vector3>> m_meshCentres;

    // A skinned model's clips, its bones' rest transforms, and whether its vertex buffers hold a pose. Present only
    // for a model with a skin; defined beside the code that uses it, so this header needs no raylib type.
    struct Skin;
    std::unordered_map<BRITE::ModelHandle, std::shared_ptr<Skin>> m_skins;
    // Puts the model's vertex buffers in the pose, or back at rest when it cannot take the pose (or none is set) and
    // they hold another. Returns whether they now hold the pose. Every posed draw and posed read-back goes through it.
    bool PoseModel(BRITE::ModelHandle model, const BRITE::ModelPose& pose);
    // PoseModel for the draw of pass.ModelCommands[command], unless its pose is in the buffers already: a draw whose
    // meshes are drawn apart -- its opaque ones, then each translucent one in its place in the sort -- is skinned once
    // for them all, and again only if another draw of the model came between.
    void PoseForDraw(BRITE::ModelHandle model, const BRITE::ModelPose& pose, std::size_t command);
    // Counts the passes submitted, so a pose a draw left is never taken for one of a later pass.
    std::uint64_t m_passSerial = 0;
    // Appends the clips of a glTF file to a skinned model's; -1, and nothing added, when it cannot.
    int AddClips(BRITE::ModelHandle model, const char* fileName);
};

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
