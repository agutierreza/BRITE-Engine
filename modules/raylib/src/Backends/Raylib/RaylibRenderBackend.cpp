#include "Backends/Raylib/RaylibRenderBackend.hpp"
#include "Backends/MotionBlur.hpp"
#include "Backends/Raylib/PbrShaderSource.hpp"
#include "Backends/Raylib/PostShaderSource.hpp"
#include "Backends/ShadowCascades.hpp"
#include "Backends/ToneCurve.hpp"
#include "RaylibPostChain.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

// Five OpenGL 3.0 calls rlgl does not wrap, the ones a multisampled render
// texture needs. They are reached through raylib's own GL loader, glad, whose
// function pointers raylib compiles in and leaves visible to the program it is
// linked into, already loaded once the window exists. Were raylib to rename or
// hide them, this would fail to link -- loudly -- rather than at run time.
extern "C" {
#if defined(_WIN32) && !defined(_WIN64)
#define BRITE_GL_CALL __stdcall
#else
#define BRITE_GL_CALL
#endif
extern void(BRITE_GL_CALL* glad_glGenRenderbuffers)(int count, unsigned int* renderbuffers);
extern void(BRITE_GL_CALL* glad_glBindRenderbuffer)(unsigned int target, unsigned int renderbuffer);
extern void(BRITE_GL_CALL* glad_glRenderbufferStorageMultisample)(unsigned int target, int samples,
                                                                  unsigned int internalFormat, int width, int height);
extern void(BRITE_GL_CALL* glad_glDeleteRenderbuffers)(int count, const unsigned int* renderbuffers);
extern void(BRITE_GL_CALL* glad_glGetIntegerv)(unsigned int name, int* values);
}

namespace BRITE {
namespace Backends {
namespace Raylib {

// A mesh's material slot, checked against the model's material count before anything indexes the materials with it:
// a loader bug or a hand-edited model file could otherwise send an out-of-range index straight into the array. Falls
// back to slot 0, raylib's own default material. Above its first use, ReadMesh.
int RaylibRenderBackend::MeshMaterialSlot(int slot, int materialCount) {
    if (slot < 0 || slot >= materialCount)
        return 0;
    return slot;
}

namespace {
// How many maps every raylib Material holds: rmodels.c's MAX_MATERIAL_MAPS, which
// raylib.h does not export. Each material's maps array is allocated this long and
// DrawMesh reads this many, so a copy of one must be exactly this long.
constexpr int MATERIAL_MAPS_PER_MATERIAL = 12;

// OpenGL names rlgl does not define, for the multisampled render texture.
constexpr unsigned int GL_RENDERBUFFER_ = 0x8D41;
constexpr unsigned int GL_RGBA8_ = 0x8058;
constexpr unsigned int GL_DEPTH_COMPONENT24_ = 0x81A6;
constexpr unsigned int GL_MAX_SAMPLES_ = 0x8D57;
constexpr int GL_COLOR_BUFFER_BIT_ = 0x4000;

// Tangents for a mesh that has texture coordinates and normals but no tangents
// of its own, so that a normal map can be read on it. raylib's GenMeshTangents
// takes the bitangent's direction from increasing v -- DOWN the texture, since v
// counts rows from the top, in glTF and in MeshData alike -- while a normal
// map's green means UP the texture. So the handedness it computes is flipped
// here, and a map reads the right way up. A mesh whose file gave tangents keeps
// them: their handedness is the file's to say.
void GenerateTangents(::Mesh& mesh) {
    if (mesh.tangents != nullptr || mesh.texcoords == nullptr || mesh.normals == nullptr || mesh.vertices == nullptr)
        return;
    ::GenMeshTangents(&mesh);
    if (mesh.tangents == nullptr)
        return;
    for (int i = 0; i < mesh.vertexCount; ++i)
        mesh.tangents[i * 4 + 3] = -mesh.tangents[i * 4 + 3];
    if (mesh.vboId != nullptr && mesh.vboId[SHADER_LOC_VERTEX_TANGENT] != 0)
        ::rlUpdateVertexBuffer(mesh.vboId[SHADER_LOC_VERTEX_TANGENT], mesh.tangents,
                               mesh.vertexCount * 4 * static_cast<int>(sizeof(float)), 0);
}

// Sets a texture's filters, anisotropy and wrap, generating its mipmaps first
// when the filter reads them. The filters go straight to rlgl rather than
// through raylib's SetTextureFilter, whose "point" and "bilinear" turn into
// mipmapped filters once a texture has mipmaps: here each filter means the same
// whatever was asked of the texture before.
void ApplySampling(::Texture2D& texture, const TextureSampling& sampling) {
    int minFilter = RL_TEXTURE_FILTER_NEAREST;
    int magFilter = RL_TEXTURE_FILTER_NEAREST;
    switch (sampling.Filter) {
    case SamplingFilter::Point:
        break;
    case SamplingFilter::Bilinear:
        minFilter = magFilter = RL_TEXTURE_FILTER_LINEAR;
        break;
    case SamplingFilter::Trilinear:
        if (texture.mipmaps <= 1)
            ::GenTextureMipmaps(&texture);
        minFilter = RL_TEXTURE_FILTER_MIP_LINEAR;
        magFilter = RL_TEXTURE_FILTER_LINEAR;
        break;
    }
    ::rlTextureParameters(texture.id, RL_TEXTURE_MIN_FILTER, minFilter);
    ::rlTextureParameters(texture.id, RL_TEXTURE_MAG_FILTER, magFilter);
    // rlgl resets the level to 1 before setting it, so 1 turns it off.
    ::rlTextureParameters(texture.id, RL_TEXTURE_FILTER_ANISOTROPIC, std::max(1, sampling.Anisotropy));
    const int wrap = sampling.Wrap == SamplingWrap::Clamp ? RL_TEXTURE_WRAP_CLAMP : RL_TEXTURE_WRAP_REPEAT;
    ::rlTextureParameters(texture.id, RL_TEXTURE_WRAP_S, wrap);
    ::rlTextureParameters(texture.id, RL_TEXTURE_WRAP_T, wrap);
}

// A bone placed by its parent, as raylib composes a clip's keyframes (BuildPoseFromParentJoints in rmodels.c): the
// child's scale times the parent's, the child's rotation after the parent's, and the child's translation scaled,
// rotated and moved by the parent.
::Transform Placed(const ::Transform& parent, const ::Transform& local) {
    ::Transform out;
    out.rotation = ::QuaternionMultiply(parent.rotation, local.rotation);
    out.scale = ::Vector3Multiply(local.scale, parent.scale);
    out.translation =
        ::Vector3Add(::Vector3RotateByQuaternion(::Vector3Multiply(local.translation, parent.scale), parent.rotation),
                     parent.translation);
    return out;
}

// Placed's inverse: the transform relative to its parent that puts a bone at `placed`.
::Transform RelativeTo(const ::Transform& parent, const ::Transform& placed) {
    const ::Quaternion unrotate = ::QuaternionInvert(parent.rotation);
    ::Transform out;
    out.rotation = ::QuaternionMultiply(unrotate, placed.rotation);
    out.scale = ::Vector3Divide(placed.scale, parent.scale);
    out.translation = ::Vector3Divide(
        ::Vector3RotateByQuaternion(::Vector3Subtract(placed.translation, parent.translation), unrotate), parent.scale);
    return out;
}

::Transform ToRaylib(const BRITE::BoneTransform& t) {
    return {{t.Translation.x, t.Translation.y, t.Translation.z},
            {t.Rotation.x, t.Rotation.y, t.Rotation.z, t.Rotation.w},
            {t.Scale.x, t.Scale.y, t.Scale.z}};
}

BRITE::BoneTransform FromRaylib(const ::Transform& t) {
    return {{t.translation.x, t.translation.y, t.translation.z},
            {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w},
            {t.scale.x, t.scale.y, t.scale.z}};
}

// Every bone placed in the model from transforms relative to each bone's parent, parents first whatever order the
// file listed them in. A parent index out of range, or a loop of parents, places the bone as if it had none.
std::vector<::Transform> PlacedInModel(const ::BoneInfo* bones, int boneCount, const std::vector<::Transform>& local) {
    std::vector<::Transform> placed(static_cast<std::size_t>(boneCount));
    std::vector<int> state(static_cast<std::size_t>(boneCount), 0); // 0 not placed, 1 being placed, 2 placed
    auto place = [&](auto&& self, int i) -> void {
        const std::size_t at = static_cast<std::size_t>(i);
        if (state[at] == 2)
            return;
        state[at] = 1;
        const int parent = bones[i].parent;
        const bool hasParent = parent >= 0 && parent < boneCount && state[static_cast<std::size_t>(parent)] != 1;
        if (hasParent) {
            self(self, parent);
            placed[at] = Placed(placed[static_cast<std::size_t>(parent)], local[at]);
        } else {
            placed[at] = local[at];
        }
        state[at] = 2;
    };
    for (int i = 0; i < boneCount; ++i)
        place(place, i);
    return placed;
}

// Copies a mesh's arrays into a MeshData -- the posed positions and normals when `posed`, the rest ones otherwise.
void ReadMesh(const ::Model& model, int m, bool posed, BRITE::MeshData& out) {
    const ::Mesh& mesh = model.meshes[m];
    if (mesh.vertices == nullptr || mesh.vertexCount <= 0)
        return; // a mesh whose file held no positions raylib could read
    const std::size_t vertexCount = static_cast<std::size_t>(mesh.vertexCount);
    const float* positions = posed && mesh.animVertices != nullptr ? mesh.animVertices : mesh.vertices;
    const float* normals = posed && mesh.animNormals != nullptr ? mesh.animNormals : mesh.normals;

    out.Positions.resize(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i)
        out.Positions[i] = {positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]};

    // A node's scale reaches the normals through the inverse-transpose, which raylib does not renormalise
    // afterwards, and neither does its skinning; the shader does, so the model draws right, but a caller is promised
    // unit normals.
    if (mesh.normals != nullptr) {
        out.Normals.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i) {
            const BRITE::Vector3 n = {normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]};
            const float length = BRITE::Math::Length(n);
            out.Normals[i] = length > 0.0f ? BRITE::Vector3{n.x / length, n.y / length, n.z / length} : n;
        }
    }

    // The draw multiplies the material's colour by the vertex colour; the read-back folds that product into the
    // vertex colour, so a plain white material draws it the same.
    const ::Color base =
        model.materials[RaylibRenderBackend::MeshMaterialSlot(model.meshMaterial[m], model.materialCount)]
            .maps[MATERIAL_MAP_DIFFUSE]
            .color;
    out.Colors.resize(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const unsigned char* vertex = mesh.colors != nullptr ? &mesh.colors[i * 4] : nullptr;
        auto channel = [&](int c, unsigned char own) {
            return RaylibRenderBackend::ModulateChannel(vertex != nullptr ? vertex[c] : 255, own);
        };
        out.Colors[i] = {channel(0, base.r), channel(1, base.g), channel(2, base.b), channel(3, base.a)};
    }

    // The first set of texture coordinates, when the mesh has one. A second set (glTF's TEXCOORD_1) has no place in
    // MeshData and is left behind.
    if (mesh.texcoords != nullptr) {
        out.TexCoords.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
            out.TexCoords[i] = {mesh.texcoords[i * 2 + 0], mesh.texcoords[i * 2 + 1]};
    }

    // Without an index list raylib draws the vertices in order, three to a triangle; a trailing one or two make no
    // triangle and are not drawn.
    const std::size_t indexCount =
        mesh.indices != nullptr ? static_cast<std::size_t>(mesh.triangleCount) * 3 : vertexCount / 3 * 3;
    out.Indices.resize(indexCount);
    for (std::size_t i = 0; i < indexCount; ++i)
        out.Indices[i] = mesh.indices != nullptr ? mesh.indices[i] : static_cast<std::uint32_t>(i);
}
} // namespace

struct RaylibRenderBackend::Skin {
    // Each array raylib loaded, freed with it when the model is unloaded, and every clip in them in order.
    std::vector<std::pair<::ModelAnimation*, int>> loaded;
    std::vector<::ModelAnimation> clips;
    // Each bone's rest transform relative to its parent, from raylib's bind pose (which it keeps placed in the model).
    std::vector<::Transform> rest;
    // Whether the vertex buffers hold a pose rather than the rest geometry.
    bool posed = false;
    // Which draw put them there: the pass (m_passSerial; 0 for none) and the command's index in it.
    std::uint64_t posedPass = 0;
    std::size_t posedCommand = 0;
};

// A render target as described (IRenderBackend.hpp). Its objects are the backend's own (RaylibPostChain), and a raylib
// RenderTexture2D that names them stands for it in m_textures, so a pass targets it and a sprite or a material reads it
// as it does any render texture. Its multisampled surface, when it has one, is the one passes draw into.
struct RaylibRenderBackend::FormattedTarget {
    TargetObjects objects;
    BRITE::TextureHandle depth = BRITE::NullTextureHandle; // the readable depth's own handle, or none
};

// What a pass's shadow pass leaves for its lit draws (RenderShadows): per cascade, the light's view and projection, the
// tile's rectangle in the atlas's uv, the far edge along the view and a texel's size in world units.
struct RaylibRenderBackend::ShadowFrame {
    int cascades = 0;
    float forward[3] = {0.0f, 0.0f, -1.0f};
    ::Matrix lightViewProjection[BRITE::ShadowCascades::MaxCascades];
    float rect[BRITE::ShadowCascades::MaxCascades][4];
    float farEdge[BRITE::ShadowCascades::MaxCascades];
    float texel[BRITE::ShadowCascades::MaxCascades];
};

// The planar reflection's targets and what the reflecting draws read the current one with. Kept by size and format,
// as the chain keeps its sets (RaylibPostChain.hpp): two reflecting passes a frame at different sizes each keep their
// own, and a target no pass has drawn into for PostChain::IdlePasses passes is freed, so a resize does not keep the old
// size's for the application's life.
struct RaylibRenderBackend::PlanarState {
    struct Slot {
        TargetObjects target;
        std::uint64_t used = 0; // the pass that last drew into it
    };
    std::vector<Slot> slots; // most recently used last
    static constexpr std::size_t MaxSlots = 3;
    // The slot the current pass drew into (an index into slots), and its mirrored camera's view and projection.
    std::size_t current = 0;
    ::Matrix viewProjection = {};
    // Sizes the device refused, not asked again; one log line for them all.
    std::vector<std::array<int, 3>> refused;
    bool failureLogged = false;

    PlanarState() = default;
    PlanarState(const PlanarState&) = delete;
    PlanarState& operator=(const PlanarState&) = delete;
    ~PlanarState() {
        // As the chain's and the atlas's: no GL call once the window, and its context, are gone.
        if (!::IsWindowReady())
            return;
        for (Slot& slot : slots)
            DestroyTarget(slot.target);
    }
    const TargetObjects& Current() const {
        return slots[current].target;
    }
    // The target of this size and format, kept or made, stamped with `pass`; null when the device cannot make it.
    TargetObjects* Acquire(int width, int height, RenderTargetFormat format, std::uint64_t pass) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const TargetObjects& t = slots[i].target;
            if (t.width == width && t.height == height && t.format == format) {
                std::rotate(slots.begin() + static_cast<std::ptrdiff_t>(i),
                            slots.begin() + static_cast<std::ptrdiff_t>(i) + 1, slots.end());
                current = slots.size() - 1;
                slots[current].used = pass;
                return &slots[current].target;
            }
        }
        const std::array<int, 3> key = {width, height, static_cast<int>(format)};
        if (std::find(refused.begin(), refused.end(), key) != refused.end())
            return nullptr;
        Slot slot;
        std::string why;
        if (!CreateTarget({width, height, format, 1, false}, slot.target, why)) {
            refused.push_back(key);
            if (!failureLogged)
                spdlog::error("BRITE: a {}x{} planar reflection could not be made ({}); the sky is reflected instead",
                              width, height, why);
            failureLogged = true;
            return nullptr;
        }
        if (slots.size() >= MaxSlots) { // the least recently used goes
            DestroyTarget(slots.front().target);
            slots.erase(slots.begin());
        }
        slot.used = pass;
        slots.push_back(slot);
        current = slots.size() - 1;
        return &slots[current].target;
    }
    // Frees every target no pass has drawn into for more than PostChain::IdlePasses passes.
    void ForgetIdle(std::uint64_t pass) {
        for (std::size_t i = 0; i < slots.size();) {
            if (pass > slots[i].used && pass - slots[i].used > PostChain::IdlePasses) {
                DestroyTarget(slots[i].target);
                slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        current = 0;
    }
};

namespace {
// The texture unit the planar reflection is bound to for a pass's lit draws, beside the shadow atlas's.
constexpr int PlanarTextureUnit = 13;

// Whether raylib refused a shader it was asked to load: a stage that fails to COMPILE leaves no program at all (id 0),
// and a program that fails to LINK is replaced by raylib's default one. Either is a refusal.
bool ShaderRefused(const ::Shader& shader) {
    return shader.id == 0 || shader.id == ::rlGetShaderIdDefault();
}
// The texture units the pass's environment map is bound to for its lit draws, as the 2D panorama pbr.fs samples:
// irradiance and prefilter. raylib's DrawMesh binds a material's irradiance and prefilter slots as CUBEMAPS, which a
// panorama is not -- the bind fails and the shader reads black -- so the backend binds them itself, beside the maps.
constexpr int IrradianceTextureUnit = 12;
constexpr int PrefilterTextureUnit = 15;

// LENGYEL'S OBLIQUE NEAR PLANE ("Oblique View Frustum Depth Projection and Clipping", 2005): the projection with its
// near plane moved onto `plane` (in view space, ax + by + cz + d, the kept side positive), its far plane tilted to
// match and nothing else changed -- x and y project exactly as before. The frustum's corner opposite the plane, q, is
// found in view space through the inverse projection; the plane, scaled so q lands on the far plane, replaces the
// projection's third row less its fourth.
::Matrix ObliqueNearPlane(::Matrix projection, ::Vector4 plane) {
    const ::Matrix inverse = ::MatrixInvert(projection);
    auto sign = [](float v) { return v > 0.0f ? 1.0f : v < 0.0f ? -1.0f : 0.0f; };
    const float cx = sign(plane.x), cy = sign(plane.y);
    const ::Vector4 q = {inverse.m0 * cx + inverse.m4 * cy + inverse.m8 + inverse.m12,
                         inverse.m1 * cx + inverse.m5 * cy + inverse.m9 + inverse.m13,
                         inverse.m2 * cx + inverse.m6 * cy + inverse.m10 + inverse.m14,
                         inverse.m3 * cx + inverse.m7 * cy + inverse.m11 + inverse.m15};
    const float scale = 2.0f / (plane.x * q.x + plane.y * q.y + plane.z * q.z + plane.w * q.w);
    projection.m2 = plane.x * scale - projection.m3;
    projection.m6 = plane.y * scale - projection.m7;
    projection.m10 = plane.z * scale - projection.m11;
    projection.m14 = plane.w * scale - projection.m15;
    return projection;
}

// The texture unit the shadow atlas is bound to for a pass's lit draws: one no material map reaches (raylib binds a
// material's twelve maps to units 0 to 11).
constexpr int ShadowTextureUnit = 14;

// A model draw's transform as DrawModelEx builds it: the model's own, then scale, rotation and translation.
::Matrix DrawTransform(const ::Model& model, const BRITE::ModelDrawCommand& cmd) {
    ::Vector3 axis;
    float angle;
    ::QuaternionToAxisAngle({cmd.Rotation.x, cmd.Rotation.y, cmd.Rotation.z, cmd.Rotation.w}, &axis, &angle);
    const ::Matrix placement = ::MatrixMultiply(
        ::MatrixMultiply(::MatrixScale(cmd.Scale.x, cmd.Scale.y, cmd.Scale.z), ::MatrixRotate(axis, angle)),
        ::MatrixTranslate(cmd.Position.x, cmd.Position.y, cmd.Position.z));
    return ::MatrixMultiply(model.transform, placement);
}
} // namespace

bool RaylibRenderBackend::MeshTranslucent(const BRITE::ModelDrawCommand& cmd, int slot, unsigned char ownAlpha) const {
    // Whether one mesh of a model draw is translucent: additive, or its colour's alpha -- the model's own times the
    // draw's tint, as the shader multiplies them -- is below 255, or its file says BLEND. A cut-out is not: a masked
    // fragment is drawn solid or not at all. A texture's alpha makes a BLEND mesh translucent only through the file's
    // say-so; an OPAQUE material ignores it, as glTF says, and so does a vertex colour's alpha here, which costs a scan
    // of the mesh to read.
    if (cmd.Material.Blend == BRITE::BlendMode::Additive)
        return true; // light added to what is behind: drawn after it, whatever its alpha
    const BRITE::GltfMaterialInfo* file = FileMaterial(cmd.Model, slot);
    if (cmd.Material.AlphaMask || (file != nullptr && file->AlphaMode == BRITE::GltfAlphaMode::Mask))
        return false;
    if (file != nullptr && file->AlphaMode == BRITE::GltfAlphaMode::Blend)
        return true;
    return (static_cast<int>(ownAlpha) * static_cast<int>(cmd.Material.AlbedoTint.a)) / 255 < 255;
}

float RaylibRenderBackend::ClipFrame(float seconds, int keyframeCount) {
    const float last = static_cast<float>(std::max(keyframeCount - 1, 0));
    float frame = seconds * ClipSamplesPerSecond;
    if (!(frame > 0.0f)) // before the start, and not a number
        frame = 0.0f;
    if (frame > last)
        frame = last;
    return frame;
}

float RaylibRenderBackend::HeldBlendWeight(float weight) {
    if (!(weight > 0.0f))
        return 0.0f;
    return weight > 1.0f ? 1.0f : weight;
}

bool RaylibRenderBackend::PoseModel(BRITE::ModelHandle handle, const BRITE::ModelPose& pose) {
    auto skinIt = m_skins.find(handle);
    auto modelIt = m_models.find(handle);
    if (skinIt == m_skins.end() || modelIt == m_models.end())
        return false;
    Skin& skin = *skinIt->second;
    skin.posedPass = 0; // whatever is in the buffers now, no draw has put it there
    const ::Model& model = *static_cast<const ::Model*>(modelIt->second);
    const int boneCount = static_cast<int>(model.skeleton.boneCount);

    // A clip the model has, whose every keyframe moves every bone.
    auto clip = [&](int index) -> const ::ModelAnimation* {
        if (index < 0 || index >= static_cast<int>(skin.clips.size()))
            return nullptr;
        const ::ModelAnimation& c = skin.clips[static_cast<std::size_t>(index)];
        if (c.keyframeCount <= 0 || c.keyframePoses == nullptr || static_cast<int>(c.boneCount) != boneCount)
            return nullptr;
        return &c;
    };

    bool posed = false;
    switch (pose.Kind) {
    case BRITE::PoseKind::Rest:
        break;
    case BRITE::PoseKind::Clip:
        if (const ::ModelAnimation* a = clip(pose.A.Clip)) {
            ::UpdateModelAnimation(model, *a, ClipFrame(pose.A.Seconds, a->keyframeCount));
            posed = true;
        }
        break;
    case BRITE::PoseKind::Blend: {
        const ::ModelAnimation* a = clip(pose.A.Clip);
        const ::ModelAnimation* b = clip(pose.B.Clip);
        if (a && b) {
            ::UpdateModelAnimationEx(model, *a, ClipFrame(pose.A.Seconds, a->keyframeCount), *b,
                                     ClipFrame(pose.B.Seconds, b->keyframeCount), HeldBlendWeight(pose.Weight));
            posed = true;
        }
        break;
    }
    case BRITE::PoseKind::Bones:
        if (static_cast<int>(pose.Bones.size()) == boneCount) {
            // The bones placed in the model, handed to raylib as a clip of one keyframe: frame 0 and the "next" one
            // are both that keyframe, so it is drawn exactly.
            std::vector<::Transform> local;
            local.reserve(pose.Bones.size());
            for (const BRITE::BoneTransform& bone : pose.Bones)
                local.push_back(ToRaylib(bone));
            std::vector<::Transform> placed = PlacedInModel(model.skeleton.bones, boneCount, local);
            ::Transform* keyframe = placed.data();
            ::ModelAnimation once = {};
            once.boneCount = model.skeleton.boneCount;
            once.keyframeCount = 1;
            once.keyframePoses = &keyframe;
            ::UpdateModelAnimation(model, once, 0.0f);
            posed = true;
        }
        break;
    }
    if (posed) {
        // raylib's CPU skinner uploads the posed positions to the position buffer, but the posed normals to the buffer
        // its shader-location enum names (SHADER_LOC_VERTEX_NORMAL, 3), which is the COLOUR buffer: the normal buffer
        // is attribute location 2 (RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL), where DrawMesh reads normals. So the
        // posed normals go there too, or a lit posed draw would be shaded with its rest normals. (LoadModel mends the
        // same mix-up for colours.)
        for (int m = 0; m < model.meshCount; ++m) {
            const ::Mesh& mesh = model.meshes[m];
            if (mesh.vboId == nullptr || mesh.animNormals == nullptr || mesh.normals == nullptr)
                continue;
            ::rlUpdateVertexBuffer(mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL], mesh.animNormals,
                                   mesh.vertexCount * 3 * static_cast<int>(sizeof(float)), 0);
        }
        skin.posed = true;
        return true;
    }

    // At rest: the bound geometry back in the buffers, if a pose is in them.
    if (skin.posed) {
        for (int m = 0; m < model.meshCount; ++m) {
            const ::Mesh& mesh = model.meshes[m];
            if (mesh.vboId == nullptr || mesh.vertices == nullptr)
                continue;
            const int bytes = mesh.vertexCount * 3 * static_cast<int>(sizeof(float));
            ::rlUpdateVertexBuffer(mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION], mesh.vertices, bytes, 0);
            if (mesh.normals != nullptr)
                ::rlUpdateVertexBuffer(mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL], mesh.normals, bytes, 0);
        }
        skin.posed = false;
    }
    return false;
}

void RaylibRenderBackend::PoseForDraw(BRITE::ModelHandle handle, const BRITE::ModelPose& pose, std::size_t command) {
    auto skinIt = m_skins.find(handle);
    if (skinIt == m_skins.end())
        return;
    // The buffers already hold this very command's pose: its earlier meshes in this pass were drawn in it, and no
    // other draw or read-back has posed the model since.
    Skin& skin = *skinIt->second;
    if (skin.posedPass == m_passSerial && skin.posedCommand == command)
        return;
    PoseModel(handle, pose);
    skin.posedPass = m_passSerial;
    skin.posedCommand = command;
}

int RaylibRenderBackend::AddClips(BRITE::ModelHandle handle, const char* fileName) {
    auto skinIt = m_skins.find(handle);
    auto modelIt = m_models.find(handle);
    if (skinIt == m_skins.end() || modelIt == m_models.end() || fileName == nullptr)
        return -1;
    if (!::IsFileExtension(fileName, ".gltf") && !::IsFileExtension(fileName, ".glb"))
        return -1;
    const ::Model& model = *static_cast<const ::Model*>(modelIt->second);
    int count = 0;
    ::ModelAnimation* clips = ::LoadModelAnimations(fileName, &count);
    if (clips == nullptr)
        return -1;
    // raylib's own check of a clip against a model is the bone count; nothing finer is kept in a clip.
    bool fits = count > 0;
    for (int i = 0; i < count && fits; ++i)
        fits = clips[i].boneCount == model.skeleton.boneCount;
    if (!fits) {
        ::UnloadModelAnimations(clips, count);
        return -1;
    }
    Skin& skin = *skinIt->second;
    skin.loaded.emplace_back(clips, count);
    for (int i = 0; i < count; ++i)
        skin.clips.push_back(clips[i]);
    return count;
}

int RaylibRenderBackend::AddModelClips(BRITE::ModelHandle model, const char* fileName) {
    return AddClips(model, fileName);
}

bool RaylibRenderBackend::ReadModelSkeleton(BRITE::ModelHandle handle, BRITE::ModelSkeleton& skeleton) {
    skeleton = {};
    auto modelIt = m_models.find(handle);
    if (modelIt == m_models.end())
        return false;
    auto skinIt = m_skins.find(handle);
    if (skinIt == m_skins.end())
        return true; // no skin: no bones and no clips
    const ::Model& model = *static_cast<const ::Model*>(modelIt->second);
    const Skin& skin = *skinIt->second;
    for (unsigned int i = 0; i < model.skeleton.boneCount; ++i) {
        BRITE::SkeletonBone bone;
        // A loader may fill all 32 bytes of a name and leave no NUL (raylib's IQM loader can): read no further.
        const char* name = model.skeleton.bones[i].name;
        bone.Name.assign(name, ::strnlen(name, sizeof(model.skeleton.bones[i].name)));
        bone.Parent = model.skeleton.bones[i].parent;
        bone.Rest = FromRaylib(skin.rest[i]);
        skeleton.Bones.push_back(std::move(bone));
    }
    for (const ::ModelAnimation& clip : skin.clips) {
        BRITE::AnimationClipInfo info;
        info.Name.assign(clip.name, ::strnlen(clip.name, sizeof(clip.name)));
        info.Duration = static_cast<float>(std::max(clip.keyframeCount - 1, 0)) / ClipSamplesPerSecond;
        skeleton.Clips.push_back(std::move(info));
    }
    return true;
}

bool RaylibRenderBackend::ReadPosedModelMeshes(BRITE::ModelHandle handle, const BRITE::ModelPose& pose,
                                               std::vector<BRITE::MeshData>& meshes) {
    meshes.clear();
    auto it = m_models.find(handle);
    if (it == m_models.end())
        return false;
    const bool posed = PoseModel(handle, pose);
    const ::Model& model = *static_cast<const ::Model*>(it->second);
    meshes.resize(static_cast<std::size_t>(model.meshCount));
    for (int m = 0; m < model.meshCount; ++m)
        ReadMesh(model, m, posed, meshes[static_cast<std::size_t>(m)]);
    return true;
}

void RaylibRenderBackend::UseBlend(bool additive) const {
    if (!m_premultipliedScene) {
        ::rlSetBlendMode(additive ? RL_BLEND_ADDITIVE : RL_BLEND_ALPHA);
        return;
    }
    // Into the premultiplied scene: the colour as ever (times its alpha, over or added), and the alpha as coverage --
    // over: a + dst (1 - a); added light covers nothing, so the alpha is left as it was.
    if (additive)
        ::rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE, RL_ZERO, RL_ONE, RL_FUNC_ADD, RL_FUNC_ADD);
    else
        ::rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD,
                                    RL_FUNC_ADD);
    ::rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
}

void RaylibRenderBackend::SubmitRenderPass(const BRITE::RenderPass& pass) {
    ++m_passSerial;
    m_modelDrawCalls = 0;
    // The pass's own target, and its size: a render texture's, or the window's drawing surface.
    RenderTexture2D* targetTexture = nullptr;
    int targetWidth = ::GetRenderWidth();
    int targetHeight = ::GetRenderHeight();
    if (pass.TargetFramebuffer != BRITE::NullTextureHandle) {
        auto it = m_textures.find(pass.TargetFramebuffer);
        if (it == m_textures.end() || !it->second.isRenderTexture)
            return; // Invalid target
        targetTexture = static_cast<RenderTexture2D*>(it->second.ptr);
        targetWidth = targetTexture->texture.width;
        targetHeight = targetTexture->texture.height;
    }

    // SUN SHADOWS (RenderPass.hpp, ShadowSettings) are drawn first, into their atlas, before any target is bound.
    m_shadowFrame.reset();
    m_shadowReport.clear();
    RenderShadows(pass, targetWidth, targetHeight);

    // AN HDR PASS (RenderPass.hpp, PostSettings) draws its background callbacks and its 3D world first, into the
    // chain's own float target, then blooms it; the tone map lands on the pass's target below, where the 2D draws
    // follow. A device that cannot run the chain draws the pass as if Hdr were off.
    // A DISPLAY-REFERRED BLOOM (PostSettings: Bloom with Hdr off) takes the same road through the chain's target, in
    // the colours the screen shows: the 3D world as an 8-bit pass draws it, bloomed, and the bloom screened over it.
    if (m_post)
        m_post->ForgetIdle(m_passSerial);
    const bool canChain = pass.Camera3DPtr != nullptr && targetWidth > 0 && targetHeight > 0;
    // MOTION BLUR (RenderPass.hpp, MotionBlurSettings) runs in the chain, under HDR or, with Hdr off, on the road the
    // display-referred bloom takes -- only on a frame that has something to blur and is not a cut, so every other frame
    // is drawn exactly as with it off.
    bool blur = canChain && WantsMotionBlur(pass, targetWidth, targetHeight);
    if (blur) {
        if (!m_post)
            m_post = std::make_shared<PostChain>();
        if (m_refuseMotionBlurForTest)
            m_post->RefuseMotionForTest();
        blur = m_post->Ready() && m_post->MotionReady();
    }
    bool hdr = pass.Post.Hdr && canChain;
    bool displayBloom = !pass.Post.Hdr && (pass.Post.Bloom.Enabled || blur) && canChain;
    if (hdr || displayBloom) {
        if (!m_post)
            m_post = std::make_shared<PostChain>();
        hdr = hdr && m_post->Ready() && EnsureAuthoredColourShader();
        displayBloom = displayBloom && m_post->Ready();
    }
    // A PLANAR REFLECTION (ReflectionSettings) is the world drawn a second time, into its own target, before the scene
    // binds the chain's: in linear light for an HDR pass, in the colours the screen shows for any other.
    m_planarThisPass = false;
    if (m_planar)
        m_planar->ForgetIdle(m_passSerial);
    const bool planarHdr = hdr;
    RenderPlanarReflection(pass, targetWidth, targetHeight, planarHdr);

    if (hdr || displayBloom) {
        // The clear colour is authored as what the screen shows: under HDR the light that shows it; in a display pass
        // the colour itself. Alpha as authored.
        const float exposure = pass.Post.Exposure;
        auto shown = [&](unsigned char c) {
            return hdr ? BRITE::ToneCurve::AuthoredToLinear(c / 255.0f, exposure) : c / 255.0f;
        };
        const float clear[4] = {shown(pass.ClearColor.r), shown(pass.ClearColor.g), shown(pass.ClearColor.b),
                                pass.ClearColor.a / 255.0f};
        const bool began =
            m_post->BeginScene(targetWidth, targetHeight, pass.Post.Samples, pass.ShouldClear, clear, m_passSerial);
        hdr = hdr && began;
        displayBloom = displayBloom && began;
    }
    // A pass that fell back from HDR after its reflection was drawn in linear light would mix light as colour: it
    // reflects the sky instead.
    if (planarHdr && !hdr)
        m_planarThisPass = false;
    const bool chained = hdr || displayBloom;
    if (chained) {
        // The chain's scene is premultiplied: the colour blends as ever, and the alpha builds as coverage -- a layer of
        // alpha a over nothing leaves alpha a, not a x a -- so a pass that does not clear lies over its target as an
        // 8-bit pass would (RaylibPostChain.cpp, Composite).
        m_premultipliedScene = true;
        UseBlend(false);
        for (auto& cb : pass.BackgroundDrawCallbacks) {
            if (cb)
                cb();
        }
        // Set again: a callback may leave any blend behind it, and the 3D world must draw under the chain's own.
        UseBlend(false);
        Draw3D(pass, hdr);
        ::rlDrawRenderBatchActive();
        m_premultipliedScene = false;
        UseBlend(false);
        m_post->EndScene();
        if (blur)
            RenderMotionBlur(pass, targetWidth, targetHeight);
        m_post->Bloom(pass.Post.Bloom);
    }

    if (targetTexture != nullptr) {
        // A multisampled target is drawn into its multisampled framebuffer,
        // at the same size; the pass is resolved into rt when it ends.
        auto multisampled = m_multisampled.find(pass.TargetFramebuffer);
        if (multisampled != m_multisampled.end()) {
            RenderTexture2D surface = *targetTexture;
            surface.id = multisampled->second.framebuffer;
            ::BeginTextureMode(surface);
        } else {
            ::BeginTextureMode(*targetTexture);
        }
    } else {
        ::BeginDrawing();
    }

    if (chained) {
        // The tone-mapped (or screened) picture replaces what the target held, or lies over it where the world drew.
        m_post->Composite(pass.Post.Exposure, pass.Post.Bloom, pass.ShouldClear, displayBloom);
    } else {
        if (pass.ShouldClear) {
            ::ClearBackground({pass.ClearColor.r, pass.ClearColor.g, pass.ClearColor.b, pass.ClearColor.a});
        }

        for (auto& cb : pass.BackgroundDrawCallbacks) {
            if (cb)
                cb();
        }
    }

    if (pass.Shader != BRITE::NullShaderHandle) {
        auto it = m_shaders.find(pass.Shader);
        if (it != m_shaders.end()) {
            ::Shader* shader = static_cast<::Shader*>(it->second);
            ::BeginShaderMode(*shader);
        }
    }

    if (!chained)
        Draw3D(pass, false);

    if (pass.Camera) {
        ::Camera2D rlCamera;
        rlCamera.offset = {pass.Camera->offset.x, pass.Camera->offset.y};
        rlCamera.target = {pass.Camera->target.x, pass.Camera->target.y};
        rlCamera.rotation = pass.Camera->rotation;
        rlCamera.zoom = pass.Camera->zoom;
        ::BeginMode2D(rlCamera);
    }

    for (auto& cb : pass.WorldDrawCallbacks) {
        if (cb)
            cb();
    }

    for (const auto& cmd : pass.SpriteCommands) {
        if (cmd.Material.AlbedoMap == BRITE::NullTextureHandle)
            continue;
        auto it = m_textures.find(cmd.Material.AlbedoMap);
        if (it == m_textures.end())
            continue;

        ::Texture2D rlTexture;
        if (it->second.isRenderTexture) {
            RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
            rlTexture = rt->texture;
        } else {
            ::Texture2D* tex = static_cast<::Texture2D*>(it->second.ptr);
            rlTexture = *tex;
        }

        ::Rectangle rlSource = {cmd.SourceRect.x, cmd.SourceRect.y, cmd.SourceRect.width, cmd.SourceRect.height};
        ::Rectangle rlDest = {cmd.DestRect.x, cmd.DestRect.y, cmd.DestRect.width, cmd.DestRect.height};
        ::Vector2 rlOrigin = {cmd.Origin.x, cmd.Origin.y};
        ::Color rlTint = {cmd.Material.AlbedoTint.r, cmd.Material.AlbedoTint.g, cmd.Material.AlbedoTint.b,
                          cmd.Material.AlbedoTint.a};

        ::DrawTexturePro(rlTexture, rlSource, rlDest, rlOrigin, cmd.RotationDeg, rlTint);
    }

    for (const auto& cmd : pass.LineCommands) {
        ::DrawLine(cmd.Start.x, cmd.Start.y, cmd.End.x, cmd.End.y, {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a});
    }

    for (const auto& cmd : pass.RectCommands) {
        ::Rectangle rlDest = {cmd.DestRect.x, cmd.DestRect.y, cmd.DestRect.width, cmd.DestRect.height};
        ::Vector2 rlOrigin = {cmd.Origin.x, cmd.Origin.y};
        ::Color rlTint = {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a};
        if (cmd.IsFilled) {
            ::DrawRectanglePro(rlDest, rlOrigin, cmd.RotationDeg, rlTint);
        } else {
            ::DrawRectangleLines(cmd.DestRect.x - cmd.Origin.x, cmd.DestRect.y - cmd.Origin.y, cmd.DestRect.width,
                                 cmd.DestRect.height, rlTint);
        }
    }

    if (pass.Camera) {
        ::EndMode2D();
    }

    for (auto& cb : pass.UIDrawCallbacks) {
        if (cb)
            cb();
    }

    if (pass.Shader != BRITE::NullShaderHandle) {
        auto it = m_shaders.find(pass.Shader);
        if (it != m_shaders.end()) {
            ::EndShaderMode();
        }
    }

    if (pass.TargetFramebuffer != BRITE::NullTextureHandle) {
        ::EndTextureMode();
        // Resolve a multisampled target: average each pixel's samples into the
        // ordinary render texture that every reader of the handle reads. A target
        // made by LoadRenderTarget resolves its own, depth included when readable.
        auto formatted = m_formattedTargets.find(pass.TargetFramebuffer);
        auto multisampled = m_multisampled.find(pass.TargetFramebuffer);
        if (formatted != m_formattedTargets.end()) {
            ResolveTarget(formatted->second->objects);
            // A pass drawn through the chain left its 3D world's depth in the chain's own target: copied here, so the
            // target's readable depth holds the depth of this pass, as RenderTargetDepth promises.
            if (chained && formatted->second->objects.depthTexture != 0)
                m_post->CopySceneDepth(formatted->second->objects.framebuffer);
        } else if (multisampled != m_multisampled.end()) {
            const RenderTexture2D* rt = static_cast<RenderTexture2D*>(m_textures[pass.TargetFramebuffer].ptr);
            const int width = rt->texture.width, height = rt->texture.height;
            ::rlBindFramebuffer(RL_READ_FRAMEBUFFER, multisampled->second.framebuffer);
            ::rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, rt->id);
            ::rlBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT_);
            ::rlBindFramebuffer(RL_READ_FRAMEBUFFER, 0);
            ::rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, 0);
        }
    } else {
        ::EndDrawing();
    }
}

// The 3D world of a pass -- its camera, primitives, models and 3D lines, opaque then translucent -- drawn into whatever
// is bound: the pass's own target, or under an HDR pass the chain's float target, where primitives and lines go
// through the authored-colour shader and the lit shader writes linear light.
void RaylibRenderBackend::Draw3D(const BRITE::RenderPass& pass, bool hdr) {
    if (pass.Camera3DPtr) {
        ::Camera3D rlCamera3D = {0};
        rlCamera3D.position = {pass.Camera3DPtr->position.x, pass.Camera3DPtr->position.y,
                               pass.Camera3DPtr->position.z};
        rlCamera3D.target = {pass.Camera3DPtr->target.x, pass.Camera3DPtr->target.y, pass.Camera3DPtr->target.z};
        rlCamera3D.up = {pass.Camera3DPtr->up.x, pass.Camera3DPtr->up.y, pass.Camera3DPtr->up.z};
        rlCamera3D.fovy = pass.Camera3DPtr->fovy;
        rlCamera3D.projection = (pass.Camera3DPtr->projection == BRITE::Math::CameraProjection::Perspective)
                                    ? CAMERA_PERSPECTIVE
                                    : CAMERA_ORTHOGRAPHIC;
        ::BeginMode3D(rlCamera3D);
        if (m_projectionOverride) {
            ::Matrix projection;
            std::memcpy(&projection, m_projection, sizeof(projection));
            ::rlSetMatrixProjection(projection);
        }

        // Draw Skybox if requested
        if (pass.DrawSkybox && pass.Environment.Cubemap != BRITE::NullTextureHandle) {
            auto texIt = m_textures.find(pass.Environment.Cubemap);
            if (texIt != m_textures.end() && !texIt->second.isRenderTexture) {
                ::rlDisableBackfaceCulling();
                ::rlDisableDepthMask();

                // Draw a simple cube as a skybox, but sample the HDR spherical map
                // We'll use rlgl directly for a quick spherical mapped cube or rely on standard DrawCube
                // A true skybox shader handles this better, but for now we draw a giant white cube inside out?
                // Actually, Raylib's models_skybox_rendering uses a specific shader for the skybox.
                // We could use pass.Environment.Cubemap and standard drawing.

                ::rlEnableDepthMask();
                ::rlEnableBackfaceCulling();
            }
        }
    }

    // Under an HDR pass every primitive and 3D line is drawn through the authored-colour shader, so its tint still
    // shows as authored after the tone map. rlgl draws its batch with the shader set when the batch is drawn, so the
    // shader stays set until the last of them is drawn, below.
    ::Shader* authoredColour = nullptr;
    if (hdr) {
        authoredColour = static_cast<::Shader*>(m_shaders[m_authoredColourShader]);
        ::SetShaderValue(*authoredColour, m_authoredColourExposure, &pass.Post.Exposure, SHADER_UNIFORM_FLOAT);
        ::BeginShaderMode(*authoredColour);
    }

    // THE ORDER A PASS IS DRAWN IN (RenderPass.hpp says it too): the opaque draws first, then the translucent draws.
    // raylib draws a model the moment it is asked but holds primitives and lines in a batch that is drawn later, so
    // left alone every model reaches the screen before every primitive whatever the pass lists: a translucent model
    // then blends with the clear colour, and the primitive behind it fails the depth test against the depth the model
    // wrote. So the opaque stage ends by drawing the batch, and every translucent draw -- primitive, model mesh or line
    // -- is drawn after it, writing no depth. The opaque stage itself is as it always was: the opaque models reach the
    // screen first, as they are asked for, then the batched primitives and lines when the batch is drawn; at equal
    // depth the later wins (GL_LEQUAL), so a primitive or a line beats a model there. That is kept, not changed: it is
    // what every pass drew before, and a line resolving in front of the model it lies on depends on it.
    auto drawPrimitive = [&](const BRITE::Primitive3DDrawCommand& cmd) {
        ::Color rlTint = {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a};

        ::rlPushMatrix();
        ::rlTranslatef(cmd.Position.x, cmd.Position.y, cmd.Position.z);

        ::Vector3 axis;
        float angle;
        ::Quaternion rlQuat = {cmd.Rotation.x, cmd.Rotation.y, cmd.Rotation.z, cmd.Rotation.w};
        ::QuaternionToAxisAngle(rlQuat, &axis, &angle);
        ::rlRotatef(angle * RAD2DEG, axis.x, axis.y, axis.z);

        ::rlScalef(cmd.Scale.x, cmd.Scale.y, cmd.Scale.z);

        switch (cmd.Type) {
        case BRITE::Primitive3DType::Cube:
            ::DrawCube({0, 0, 0}, cmd.Size.x, cmd.Size.y, cmd.Size.z, rlTint);
            break;
        case BRITE::Primitive3DType::CubeWires:
            ::DrawCubeWires({0, 0, 0}, cmd.Size.x, cmd.Size.y, cmd.Size.z, rlTint);
            break;
        case BRITE::Primitive3DType::Sphere:
            ::DrawSphere({0, 0, 0}, cmd.Size.x, rlTint); // Size.x is radius
            break;
        case BRITE::Primitive3DType::SphereWires:
            ::DrawSphereWires({0, 0, 0}, cmd.Size.x, 16, 16, rlTint); // Size.x is radius
            break;
        case BRITE::Primitive3DType::Grid:
            ::DrawGrid((int)cmd.Size.x, cmd.Size.y); // Size.x is slices, Size.y is spacing
            break;
        }

        ::rlPopMatrix();
    };

    // A primitive is translucent when its tint is: a grid takes no tint and is always opaque.
    auto primitiveTranslucent = [](const BRITE::Primitive3DDrawCommand& cmd) {
        return cmd.Type != BRITE::Primitive3DType::Grid && cmd.Tint.a < 255;
    };
    for (const auto& cmd : pass.Primitive3DCommands) {
        if (!primitiveTranslucent(cmd))
            drawPrimitive(cmd);
    }

    // THE LIT SHADER, the variant for what this pass uses and no more (pbr.fs, THE VARIANTS): a model draw's, and the
    // instanced one beside it when the pass has instanced draws. Each program the pass uses is given the pass's
    // lighting once, the first time it is used; a program keeps its uniforms, so changing between the two costs a
    // lookup.
    PbrVariant variant;
    variant.hdr = hdr;
    for (const BRITE::ModelDrawCommand& cmd : pass.ModelCommands)
        variant.reflections = variant.reflections || cmd.Material.Reflection != BRITE::ReflectionMode::None;
    for (const BRITE::InstancedModelDrawCommand& cmd : pass.InstancedModelCommands)
        variant.reflections = variant.reflections || cmd.Material.Reflection != BRITE::ReflectionMode::None;
    if (m_shadowFrame && m_shadowFrame->cascades > 0) {
        variant.shadowCascades = m_shadowFrame->cascades;
        variant.shadowFilter = static_cast<int>(pass.Shadows.Filter);
    }
    bool lit[2] = {false, false};
    auto useVariant = [&](bool instanced) {
        PbrVariant wanted = variant;
        wanted.instanced = instanced;
        EnsurePbrShader(wanted);
        if (!lit[instanced ? 1 : 0]) {
            ApplyPassLighting(pass);
            lit[instanced ? 1 : 0] = true;
        }
    };
    if (!pass.ModelCommands.empty())
        useVariant(false);

    // Whether one mesh of a model draw is translucent (MeshTranslucent).
    auto meshTranslucent = [&](const BRITE::ModelDrawCommand& cmd, const ::Model& rlModel, int m) {
        const int slot = MeshMaterialSlot(rlModel.meshMaterial[m], rlModel.materialCount);
        return MeshTranslucent(cmd, slot, rlModel.materials[slot].maps[MATERIAL_MAP_DIFFUSE].color.a);
    };

    // A texture the draw command names, or nothing when it names none or the
    // handle is not a plain texture.
    auto commandTexture = [&](TextureHandle handle) -> const ::Texture2D* {
        if (handle == BRITE::NullTextureHandle)
            return nullptr;
        auto texIt = m_textures.find(handle);
        if (texIt == m_textures.end() || texIt->second.isRenderTexture)
            return nullptr;
        return static_cast<const ::Texture2D*>(texIt->second.ptr);
    };

    // ONE MESH of a draw, through the lit shader in use, with ITS OWN material: a model loaded from a file keeps its
    // materials in slots 1..n (raylib reserves slot 0 for a default), and every one of them is drawn lit. The material
    // is copied, maps and all, so nothing below changes the model: the draw's textures replace the model's own for this
    // draw only, and a slot the draw leaves empty keeps what the model brought. `issue` draws the mesh with the
    // material made: once (a model draw) or once per copy (an instanced one).
    auto drawMesh = [&](BRITE::ModelHandle handle, const BRITE::PBRMaterial& drawMaterial, const ::Model& rlModel,
                        int m, bool writesDepth, const ::Color& tint, auto&& issue) {
        const ::Shader& pbrShader = *static_cast<::Shader*>(m_shaders[m_pbrShader]);
        const std::pair<TextureHandle, int> overrides[] = {
            {drawMaterial.AlbedoMap, MATERIAL_MAP_ALBEDO},       {drawMaterial.NormalMap, MATERIAL_MAP_NORMAL},
            {drawMaterial.RoughnessMap, MATERIAL_MAP_ROUGHNESS}, {drawMaterial.MetallicMap, MATERIAL_MAP_METALNESS},
            {drawMaterial.EmissionMap, MATERIAL_MAP_EMISSION},   {drawMaterial.AOMap, MATERIAL_MAP_OCCLUSION},
        };
        const int slot = MeshMaterialSlot(rlModel.meshMaterial[m], rlModel.materialCount);
        const ::Material& own = rlModel.materials[slot];
        ::MaterialMap maps[MATERIAL_MAPS_PER_MATERIAL];
        std::memcpy(maps, own.maps, sizeof(maps));
        ::Material material = own;
        material.maps = maps;
        material.shader = pbrShader;

        // The loader's placeholder is a 1x1 white texture, which is no texture.
        auto owns = [&](int map) {
            const unsigned int id = maps[map].texture.id;
            return id != 0 && id != ::rlGetTextureIdDefault();
        };
        OwnMaps ownMaps;
        ownMaps.albedo = owns(MATERIAL_MAP_ALBEDO);
        ownMaps.emission = owns(MATERIAL_MAP_EMISSION);
        ownMaps.metalness = owns(MATERIAL_MAP_METALNESS);
        ownMaps.roughness = owns(MATERIAL_MAP_ROUGHNESS);
        ownMaps.occlusion = owns(MATERIAL_MAP_OCCLUSION);
        ownMaps.normal = owns(MATERIAL_MAP_NORMAL);

        for (const auto& [mapHandle, mapIndex] : overrides) {
            if (const ::Texture2D* texture = commandTexture(mapHandle))
                maps[mapIndex].texture = *texture;
        }
        // The environment belongs to the pass, never to the model: bound by ApplyPassLighting as the 2D panorama it
        // is, so these slots, which DrawMesh would bind as cubemaps, are left empty.
        maps[MATERIAL_MAP_IRRADIANCE].texture = ::Texture2D{0};
        maps[MATERIAL_MAP_PREFILTER].texture = ::Texture2D{0};

        const bool bothSides = ApplyMaterial(drawMaterial, ownMaps, FileMaterial(handle, slot));

        // The draw tint times the material's own colour, as DrawModelEx does.
        const ::Color ownColour = maps[MATERIAL_MAP_DIFFUSE].color;
        maps[MATERIAL_MAP_DIFFUSE].color = {static_cast<unsigned char>((static_cast<int>(ownColour.r) * tint.r) / 255),
                                            static_cast<unsigned char>((static_cast<int>(ownColour.g) * tint.g) / 255),
                                            static_cast<unsigned char>((static_cast<int>(ownColour.b) * tint.b) / 255),
                                            static_cast<unsigned char>((static_cast<int>(ownColour.a) * tint.a) / 255)};

        // Culling off for this mesh alone, and back on before the next.
        if (bothSides)
            ::rlDisableBackfaceCulling();
        // Depth writes off for this mesh alone -- the draw's own DepthWrite, and
        // every translucent mesh -- and back on before anything else: the next
        // mesh, and the primitives, which rlgl batches and draws later.
        if (!writesDepth)
            ::rlDisableDepthMask();
        // An additive mesh adds its colour, times its alpha, to what is behind it; and back to the ordinary blend
        // before anything else is drawn.
        const bool additive = drawMaterial.Blend == BRITE::BlendMode::Additive;
        if (additive)
            UseBlend(true);
        issue(rlModel.meshes[m], material);
        ++m_modelDrawCalls;
        if (additive)
            UseBlend(false);
        if (!writesDepth)
            ::rlEnableDepthMask();
        if (bothSides)
            ::rlEnableBackfaceCulling();
    };

    // One model draw's meshes: the opaque ones in the opaque stage, or in the translucent stage the one mesh
    // `onlyMesh` names, which writes no depth.
    auto drawModel = [&](const BRITE::ModelDrawCommand& cmd, bool translucentStage, int onlyMesh) {
        if (cmd.Model == BRITE::NullModelHandle)
            return;
        auto it = m_models.find(cmd.Model);
        if (it == m_models.end())
            return;

        const ::Model* rlModel = static_cast<::Model*>(it->second);
        const bool writesDepth = !translucentStage && cmd.Material.DepthWrite;

        // The model's placement, built exactly as DrawModelEx builds it (scale,
        // then rotation, then translation, after the model's own transform), so
        // a model lands where it always has.
        ::Vector3 rlAxis;
        float rlAngle;
        ::Quaternion rlQuat = {cmd.Rotation.x, cmd.Rotation.y, cmd.Rotation.z, cmd.Rotation.w};
        ::QuaternionToAxisAngle(rlQuat, &rlAxis, &rlAngle);
        rlAngle *= RAD2DEG;
        const ::Matrix placement =
            ::MatrixMultiply(::MatrixMultiply(::MatrixScale(cmd.Scale.x, cmd.Scale.y, cmd.Scale.z),
                                              ::MatrixRotate(rlAxis, rlAngle * DEG2RAD)),
                             ::MatrixTranslate(cmd.Position.x, cmd.Position.y, cmd.Position.z));
        const ::Matrix transform = ::MatrixMultiply(rlModel->transform, placement);
        const ::Color tint = {cmd.Material.AlbedoTint.r, cmd.Material.AlbedoTint.g, cmd.Material.AlbedoTint.b,
                              cmd.Material.AlbedoTint.a};

        // This draw's pose goes into the model's vertex buffers before the first of its meshes this call draws -- the
        // opaque stage draws a command's opaque meshes, the translucent stage one mesh at a time, interleaved with
        // other draws -- so every mesh is drawn in its own command's pose, and two draws in one pass never share one.
        bool posed = false;
        for (int m = 0; m < rlModel->meshCount; ++m) {
            if ((onlyMesh >= 0 && m != onlyMesh) || meshTranslucent(cmd, *rlModel, m) != translucentStage)
                continue;
            if (!posed) {
                PoseForDraw(cmd.Model, cmd.Pose, static_cast<std::size_t>(&cmd - pass.ModelCommands.data()));
                posed = true;
            }
            drawMesh(cmd.Model, cmd.Material, *rlModel, m, writesDepth, tint,
                     [&](const ::Mesh& mesh, const ::Material& material) { ::DrawMesh(mesh, material, transform); });
        }
    };

    for (const auto& cmd : pass.ModelCommands)
        drawModel(cmd, false, -1);

    // INSTANCED DRAWS (InstancedModelDrawCommand). Each command's copies become the stream's attributes once, in the
    // order listed: a placement as a model draw's (the model's own transform, then scale, rotation, translation) and a
    // tint. A mesh is translucent by a model draw's rules applied to `proxies` -- the command's model and material, the
    // material's alpha times the faintest copy's -- so one faint copy makes the whole mesh translucent.
    std::vector<std::vector<InstanceData>> instanceData(pass.InstancedModelCommands.size());
    std::vector<BRITE::ModelDrawCommand> proxies(pass.InstancedModelCommands.size());
    for (std::size_t i = 0; i < pass.InstancedModelCommands.size(); ++i) {
        const BRITE::InstancedModelDrawCommand& cmd = pass.InstancedModelCommands[i];
        auto it = m_models.find(cmd.Model);
        if (cmd.Model == BRITE::NullModelHandle || it == m_models.end() || cmd.Instances.empty())
            continue;
        const ::Model& rlModel = *static_cast<::Model*>(it->second);
        int faintest = 255;
        std::vector<InstanceData>& data = instanceData[i];
        data.resize(cmd.Instances.size());
        for (std::size_t k = 0; k < cmd.Instances.size(); ++k) {
            const BRITE::ModelInstance& copy = cmd.Instances[k];
            BRITE::ModelDrawCommand placed;
            placed.Position = copy.Position;
            placed.Rotation = copy.Rotation;
            placed.Scale = copy.Scale;
            const ::float16 columns = ::MatrixToFloatV(DrawTransform(rlModel, placed));
            std::memcpy(data[k].transform, columns.v, sizeof(data[k].transform));
            data[k].tint[0] = copy.Tint.r / 255.0f;
            data[k].tint[1] = copy.Tint.g / 255.0f;
            data[k].tint[2] = copy.Tint.b / 255.0f;
            data[k].tint[3] = copy.Tint.a / 255.0f;
            faintest = std::min(faintest, static_cast<int>(copy.Tint.a));
        }
        proxies[i].Model = cmd.Model;
        proxies[i].Material = cmd.Material;
        proxies[i].Material.AlbedoTint.a =
            static_cast<unsigned char>((static_cast<int>(cmd.Material.AlbedoTint.a) * faintest) / 255);
    }

    // One instanced draw of a mesh: what raylib's DrawMesh does for one placement, done once for every copy in `data`
    // -- the material's colour and maps bound, the view and projection as the matrix, the model matrix per copy.
    auto issueInstanced = [&](const ::Mesh& mesh, const ::Material& material, const InstanceData* data, int count) {
        if (!m_instances)
            m_instances = std::make_shared<InstanceStream>();
        const ::Shader& shader = material.shader;
        ::rlEnableShader(shader.id);
        if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] != -1) {
            const ::Color c = material.maps[MATERIAL_MAP_DIFFUSE].color;
            const float values[4] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
            ::rlSetUniform(shader.locs[SHADER_LOC_COLOR_DIFFUSE], values, SHADER_UNIFORM_VEC4, 1);
        }
        const ::Matrix view = ::rlGetMatrixModelview();
        const ::Matrix projection = ::rlGetMatrixProjection();
        if (shader.locs[SHADER_LOC_MATRIX_VIEW] != -1)
            ::rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_VIEW], view);
        if (shader.locs[SHADER_LOC_MATRIX_PROJECTION] != -1)
            ::rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_PROJECTION], projection);
        for (int map = 0; map < MATERIAL_MAPS_PER_MATERIAL; ++map) {
            if (material.maps[map].texture.id == 0)
                continue;
            ::rlActiveTextureSlot(map);
            if (map == MATERIAL_MAP_IRRADIANCE || map == MATERIAL_MAP_PREFILTER || map == MATERIAL_MAP_CUBEMAP)
                ::rlEnableTextureCubemap(material.maps[map].texture.id);
            else
                ::rlEnableTexture(material.maps[map].texture.id);
            ::rlSetUniform(shader.locs[SHADER_LOC_MAP_DIFFUSE + map], &map, SHADER_UNIFORM_INT, 1);
        }
        // The copies' placements carry the model matrix; rlgl's own transform stack is empty inside a pass.
        ::rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_MVP], ::MatrixMultiply(view, projection));
        m_instances->Draw(mesh.vaoId, mesh.indices != nullptr, mesh.triangleCount * 3, mesh.vertexCount, data, count);
        for (int map = 0; map < MATERIAL_MAPS_PER_MATERIAL; ++map) {
            if (material.maps[map].texture.id == 0)
                continue;
            ::rlActiveTextureSlot(map);
            if (map == MATERIAL_MAP_IRRADIANCE || map == MATERIAL_MAP_PREFILTER || map == MATERIAL_MAP_CUBEMAP)
                ::rlDisableTextureCubemap();
            else
                ::rlDisableTexture();
        }
        ::rlActiveTextureSlot(0);
        ::rlDisableShader();
    };

    // One instanced command's meshes, as drawModel draws a model draw's: the opaque ones, or the one translucent mesh
    // `onlyMesh` names with the copies in `data` (sorted, for a sorted draw). A skinned model is drawn at rest.
    auto drawInstanced = [&](std::size_t index, bool translucentStage, int onlyMesh,
                             const std::vector<InstanceData>& data) {
        const BRITE::InstancedModelDrawCommand& cmd = pass.InstancedModelCommands[index];
        // A device that refused the instanced variant left no usable program for it -- no program, or raylib's default
        // (ShaderRefused): the copies are skipped (EnsurePbrShader has said so in the log).
        if (data.empty() || ShaderRefused(*static_cast<::Shader*>(m_shaders[m_pbrShader])))
            return;
        const ::Model& rlModel = *static_cast<::Model*>(m_models[cmd.Model]);
        const bool writesDepth = !translucentStage && cmd.Material.DepthWrite;
        const ::Color tint = {cmd.Material.AlbedoTint.r, cmd.Material.AlbedoTint.g, cmd.Material.AlbedoTint.b,
                              cmd.Material.AlbedoTint.a};
        bool posed = false;
        for (int m = 0; m < rlModel.meshCount; ++m) {
            if ((onlyMesh >= 0 && m != onlyMesh) || meshTranslucent(proxies[index], rlModel, m) != translucentStage)
                continue;
            if (!posed) {
                PoseForDraw(cmd.Model, BRITE::ModelPose{}, InstancedPoseIndex);
                posed = true;
            }
            drawMesh(cmd.Model, cmd.Material, rlModel, m, writesDepth, tint,
                     [&](const ::Mesh& mesh, const ::Material& material) {
                         issueInstanced(mesh, material, data.data(), static_cast<int>(data.size()));
                     });
        }
    };

    if (!pass.InstancedModelCommands.empty()) {
        useVariant(true);
        for (std::size_t i = 0; i < pass.InstancedModelCommands.size(); ++i)
            drawInstanced(i, false, -1, instanceData[i]);
    }

    auto drawLine = [](const BRITE::Line3DDrawCommand& cmd) {
        ::DrawLine3D({cmd.Start.x, cmd.Start.y, cmd.Start.z}, {cmd.End.x, cmd.End.y, cmd.End.z},
                     {cmd.Tint.r, cmd.Tint.g, cmd.Tint.b, cmd.Tint.a});
    };
    if (pass.Camera3DPtr) {
        // After the opaque models, so a segment sharing their depth resolves in front of
        // them rather than behind.
        for (const auto& cmd : pass.Line3DCommands) {
            if (cmd.Tint.a == 255)
                drawLine(cmd);
        }
    }

    // THE TRANSLUCENT STAGE. Everything above is drawn (the batch is flushed below), so a translucent draw blends
    // with what is really behind it. Each one is depth-TESTED against that and writes no depth, and they are drawn
    // farthest first by the depth of the centre of the box around the draw -- a primitive's position, a line's
    // midpoint, a model mesh's bounding box -- along the camera's view direction, the same measure the depth buffer
    // uses, which is right for an orthographic camera too. One draw is not split: two meshes that interpenetrate, or
    // one mesh with translucent triangles in front of each other, come out in the order the mesh lists them.
    // Draws of equal depth, and every draw when the pass has no 3D camera, keep the order the pass lists them in:
    // primitives, then models, then lines.
    //
    // A model draw that sets DepthWrite off is a LAYER, which asks for the order it was submitted in (see
    // PBRMaterial::DepthWrite): its translucent meshes are drawn BEFORE every sorted draw, in submission order. Before,
    // because a layer was always drawn ahead of the batch of translucent primitives and lines, and a layer under them
    // keeps what it looked like.
    struct TranslucentDraw {
        enum class Kind { Primitive, ModelMesh, Line, InstancedMesh } kind;
        float depth;
        std::size_t index;
        int mesh;
        // An instanced mesh's copies, sorted farthest first: an index into sortedCopies, or none (a layer's, drawn as
        // listed).
        std::size_t copies = static_cast<std::size_t>(-1);
    };
    std::vector<std::vector<InstanceData>> sortedCopies;
    std::vector<TranslucentDraw> sorted;
    std::vector<TranslucentDraw> layers;
    ::Vector3 eye = {0.0f, 0.0f, 0.0f};
    ::Vector3 forward = {0.0f, 0.0f, 0.0f};
    if (pass.Camera3DPtr) {
        eye = {pass.Camera3DPtr->position.x, pass.Camera3DPtr->position.y, pass.Camera3DPtr->position.z};
        forward = ::Vector3Normalize(::Vector3Subtract(
            {pass.Camera3DPtr->target.x, pass.Camera3DPtr->target.y, pass.Camera3DPtr->target.z}, eye));
    }
    auto viewDepth = [&](const ::Vector3& point) {
        return pass.Camera3DPtr ? ::Vector3DotProduct(::Vector3Subtract(point, eye), forward) : 0.0f;
    };

    // The centre of each of a model's meshes' own bounding boxes, in model space, found once: a scan of the mesh. A
    // skinned model's is its rest geometry's: a pose moves the mesh within reach of its bones, and is not applied to
    // find the key.
    auto meshCentres = [&](BRITE::ModelHandle handle, const ::Model& rlModel) -> const std::vector<BRITE::Vector3>& {
        std::vector<BRITE::Vector3>& centres = m_meshCentres[handle];
        if (centres.size() != static_cast<std::size_t>(rlModel.meshCount)) {
            centres.clear();
            for (int k = 0; k < rlModel.meshCount; ++k) {
                const ::BoundingBox box = ::GetMeshBoundingBox(rlModel.meshes[k]);
                centres.push_back(
                    {(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f});
            }
        }
        return centres;
    };

    for (std::size_t i = 0; i < pass.Primitive3DCommands.size(); ++i) {
        const auto& cmd = pass.Primitive3DCommands[i];
        if (primitiveTranslucent(cmd))
            sorted.push_back(
                {TranslucentDraw::Kind::Primitive, viewDepth({cmd.Position.x, cmd.Position.y, cmd.Position.z}), i, 0});
    }
    for (std::size_t i = 0; i < pass.ModelCommands.size(); ++i) {
        const auto& cmd = pass.ModelCommands[i];
        auto it = m_models.find(cmd.Model);
        if (cmd.Model == BRITE::NullModelHandle || it == m_models.end())
            continue;
        const ::Model* rlModel = static_cast<::Model*>(it->second);
        for (int m = 0; m < rlModel->meshCount; ++m) {
            if (!meshTranslucent(cmd, *rlModel, m))
                continue;
            const std::vector<BRITE::Vector3>& centres = meshCentres(cmd.Model, *rlModel);
            // Placed as the draw below places the model: its own transform, then scale, rotation, translation.
            ::Vector3 axis;
            float angle;
            const ::Quaternion rotation = {cmd.Rotation.x, cmd.Rotation.y, cmd.Rotation.z, cmd.Rotation.w};
            ::QuaternionToAxisAngle(rotation, &axis, &angle);
            const ::Matrix placement = ::MatrixMultiply(
                ::MatrixMultiply(::MatrixScale(cmd.Scale.x, cmd.Scale.y, cmd.Scale.z), ::MatrixRotate(axis, angle)),
                ::MatrixTranslate(cmd.Position.x, cmd.Position.y, cmd.Position.z));
            const BRITE::Vector3 centre = centres[static_cast<std::size_t>(m)];
            const ::Vector3 world =
                ::Vector3Transform(::Vector3Transform({centre.x, centre.y, centre.z}, rlModel->transform), placement);
            (cmd.Material.DepthWrite ? sorted : layers)
                .push_back({TranslucentDraw::Kind::ModelMesh, viewDepth(world), i, m});
        }
    }
    // An instanced command's translucent mesh is ONE draw, keyed by the centre of its copies' positions -- the mean of
    // the mesh's centre as each copy places it -- with the copies inside it drawn farthest first. A layer's copies keep
    // the order listed.
    for (std::size_t i = 0; i < pass.InstancedModelCommands.size(); ++i) {
        const std::vector<InstanceData>& data = instanceData[i];
        if (data.empty())
            continue;
        const BRITE::InstancedModelDrawCommand& cmd = pass.InstancedModelCommands[i];
        const ::Model& rlModel = *static_cast<::Model*>(m_models[cmd.Model]);
        for (int m = 0; m < rlModel.meshCount; ++m) {
            if (!meshTranslucent(proxies[i], rlModel, m))
                continue;
            const BRITE::Vector3 centre = meshCentres(cmd.Model, rlModel)[static_cast<std::size_t>(m)];
            std::vector<std::pair<float, std::size_t>> depths(data.size());
            ::Vector3 sum = {0.0f, 0.0f, 0.0f};
            for (std::size_t k = 0; k < data.size(); ++k) {
                const float* t = data[k].transform; // column-major: t[0..3] the first column
                const ::Vector3 world = {t[0] * centre.x + t[4] * centre.y + t[8] * centre.z + t[12],
                                         t[1] * centre.x + t[5] * centre.y + t[9] * centre.z + t[13],
                                         t[2] * centre.x + t[6] * centre.y + t[10] * centre.z + t[14]};
                sum = ::Vector3Add(sum, world);
                depths[k] = {viewDepth(world), k};
            }
            const ::Vector3 mean = ::Vector3Scale(sum, 1.0f / static_cast<float>(data.size()));
            TranslucentDraw draw{TranslucentDraw::Kind::InstancedMesh, viewDepth(mean), i, m};
            if (cmd.Material.DepthWrite) {
                std::stable_sort(depths.begin(), depths.end(),
                                 [](const auto& a, const auto& b) { return a.first > b.first; });
                std::vector<InstanceData> copies(data.size());
                for (std::size_t k = 0; k < depths.size(); ++k)
                    copies[k] = data[depths[k].second];
                draw.copies = sortedCopies.size();
                sortedCopies.push_back(std::move(copies));
                sorted.push_back(draw);
            } else {
                layers.push_back(draw);
            }
        }
    }
    if (pass.Camera3DPtr) {
        for (std::size_t i = 0; i < pass.Line3DCommands.size(); ++i) {
            const auto& cmd = pass.Line3DCommands[i];
            if (cmd.Tint.a < 255)
                sorted.push_back({TranslucentDraw::Kind::Line,
                                  viewDepth({(cmd.Start.x + cmd.End.x) * 0.5f, (cmd.Start.y + cmd.End.y) * 0.5f,
                                             (cmd.Start.z + cmd.End.z) * 0.5f}),
                                  i, 0});
        }
    }

    if (!sorted.empty() || !layers.empty()) {
        // The opaque primitives and lines are still in rlgl's batch.
        ::rlDrawRenderBatchActive();
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const TranslucentDraw& a, const TranslucentDraw& b) { return a.depth > b.depth; });
        std::vector<TranslucentDraw> order = layers;
        order.insert(order.end(), sorted.begin(), sorted.end());
        for (std::size_t i = 0; i < order.size();) {
            if (order[i].kind == TranslucentDraw::Kind::ModelMesh) {
                if (!pass.InstancedModelCommands.empty())
                    useVariant(false);
                drawModel(pass.ModelCommands[order[i].index], true, order[i].mesh);
                ++i;
                continue;
            }
            if (order[i].kind == TranslucentDraw::Kind::InstancedMesh) {
                useVariant(true);
                const TranslucentDraw& draw = order[i];
                drawInstanced(draw.index, true, draw.mesh,
                              draw.copies < sortedCopies.size() ? sortedCopies[draw.copies] : instanceData[draw.index]);
                ++i;
                continue;
            }
            // A run of primitives and lines shares one change of the depth writes and one flush: the batch is drawn at
            // the flush, so the writes are off across it, in the order the run lists them.
            ::rlDisableDepthMask();
            for (; i < order.size() && order[i].kind != TranslucentDraw::Kind::ModelMesh &&
                   order[i].kind != TranslucentDraw::Kind::InstancedMesh;
                 ++i) {
                if (order[i].kind == TranslucentDraw::Kind::Primitive)
                    drawPrimitive(pass.Primitive3DCommands[order[i].index]);
                else
                    drawLine(pass.Line3DCommands[order[i].index]);
            }
            ::rlDrawRenderBatchActive();
            ::rlEnableDepthMask();
        }
    }

    if (authoredColour != nullptr)
        ::EndShaderMode(); // draws what is left of the batch through it first
    if (pass.Camera3DPtr)
        ::EndMode3D();
    if (m_planarThisPass) {
        ::rlActiveTextureSlot(PlanarTextureUnit);
        ::rlDisableTexture();
        ::rlActiveTextureSlot(0);
    }
    if (m_environmentThisPass) {
        for (const int unit : {IrradianceTextureUnit, PrefilterTextureUnit}) {
            ::rlActiveTextureSlot(unit);
            ::rlDisableTexture();
        }
        ::rlActiveTextureSlot(0);
        m_environmentThisPass = false;
    }
    if (m_shadowFrame && m_shadowFrame->cascades > 0) {
        ::rlActiveTextureSlot(ShadowTextureUnit);
        ::rlDisableTexture();
        ::rlActiveTextureSlot(0);
    }
}

// ---------------------------------------------------------------------------
// The planar reflection
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Motion blur (RenderPass.hpp, MotionBlurSettings)
// ---------------------------------------------------------------------------

namespace {

// A camera's view and projection as BeginMode3D makes them on a target `aspect` wide (raylib's rcore.c): a frustum of
// the vertical angle fovy, or an orthographic box fovy high, between the cull distances.
void CameraMatrices(const BRITE::Camera3D& camera, float aspect, ::Matrix& view, ::Matrix& projection) {
    view = ::MatrixLookAt({camera.position.x, camera.position.y, camera.position.z},
                          {camera.target.x, camera.target.y, camera.target.z}, {camera.up.x, camera.up.y, camera.up.z});
    const double nearPlane = ::rlGetCullDistanceNear(), farPlane = ::rlGetCullDistanceFar();
    if (camera.projection == BRITE::Math::CameraProjection::Perspective) {
        const double top = nearPlane * std::tan(camera.fovy * 0.5 * DEG2RAD), right = top * aspect;
        projection = ::MatrixFrustum(-right, right, -top, top, nearPlane, farPlane);
    } else {
        const double top = camera.fovy / 2.0, right = top * aspect;
        projection = ::MatrixOrtho(-right, right, -top, top, nearPlane, farPlane);
    }
}

std::array<float, 16> Columns(const ::Matrix& m) {
    const ::float16 v = ::MatrixToFloatV(m);
    std::array<float, 16> out{};
    std::memcpy(out.data(), v.v, sizeof(v.v));
    return out;
}

} // namespace

bool RaylibRenderBackend::WantsMotionBlur(const BRITE::RenderPass& pass, int targetWidth, int targetHeight) const {
    const BRITE::MotionBlurSettings& settings = pass.Post.MotionBlur;
    if (!settings.Enabled || pass.Camera3DPtr == nullptr ||
        BRITE::MotionBlur::IsCut(settings, *pass.Camera3DPtr, targetWidth, targetHeight))
        return false;
    // Something must move -- the camera, or a draw that stood elsewhere last frame -- or the pass is drawn exactly as
    // with motion blur off, even where blurring it would take it through the chain.
    namespace MB = BRITE::MotionBlur;
    if (settings.PreviousCamera && MB::Moved(*settings.PreviousCamera, *pass.Camera3DPtr))
        return true;
    for (const BRITE::ModelDrawCommand& cmd : pass.ModelCommands)
        if (cmd.MotionBlurred && cmd.Previous && MB::Moved(*cmd.Previous, {cmd.Position, cmd.Rotation, cmd.Scale}))
            return true;
    for (const BRITE::InstancedModelDrawCommand& cmd : pass.InstancedModelCommands)
        if (cmd.MotionBlurred)
            for (const BRITE::ModelInstance& copy : cmd.Instances)
                if (copy.Previous && MB::Moved(*copy.Previous, {copy.Position, copy.Rotation, copy.Scale}))
                    return true;
    return false;
}

void RaylibRenderBackend::RenderMotionBlur(const BRITE::RenderPass& pass, int targetWidth, int targetHeight) {
    const BRITE::MotionBlurSettings& settings = pass.Post.MotionBlur;
    const BRITE::Camera3D& camera = *pass.Camera3DPtr;
    const BRITE::Camera3D& before = settings.PreviousCamera ? *settings.PreviousCamera : camera;
    const float aspect = static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
    ::Matrix viewNow, projectionNow, viewBefore, projectionBefore;
    CameraMatrices(camera, aspect, viewNow, projectionNow);
    CameraMatrices(before, aspect, viewBefore, projectionBefore);
    const ::Matrix viewProjectionNow = ::MatrixMultiply(viewNow, projectionNow);
    const ::Matrix viewProjectionBefore = ::MatrixMultiply(viewBefore, projectionBefore);
    const float depthRange[3] = {static_cast<float>(::rlGetCullDistanceNear()),
                                 static_cast<float>(::rlGetCullDistanceFar()),
                                 camera.projection == BRITE::Math::CameraProjection::Perspective ? 1.0f : 0.0f};
    const int tile =
        std::clamp(static_cast<int>(std::ceil(settings.MaxLength)), static_cast<int>(BRITE::MotionBlur::LeastMaxLength),
                   static_cast<int>(BRITE::MotionBlur::MostMaxLength));
    if (!m_post->BeginMotion(tile, m_passSerial))
        return;

    // Whether one mesh of a draw is translucent, as the lit draw judged it: a translucent mesh writes no motion.
    auto translucent = [&](const BRITE::ModelDrawCommand& cmd, const ::Model& model, int m) {
        const int slot = MeshMaterialSlot(model.meshMaterial[m], model.materialCount);
        return MeshTranslucent(cmd, slot, model.materials[slot].maps[MATERIAL_MAP_DIFFUSE].color.a);
    };
    auto modelOf = [&](BRITE::ModelHandle handle) -> const ::Model* {
        auto it = m_models.find(handle);
        return it == m_models.end() ? nullptr : static_cast<const ::Model*>(it->second);
    };
    auto drawMesh = [&](const ::Mesh& mesh, const std::array<float, 16>& now, const std::array<float, 16>& was) {
        if (mesh.vboId == nullptr)
            return;
        const unsigned int indices =
            mesh.indices != nullptr ? mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_INDICES] : 0;
        m_post->DrawVelocity(mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION], indices, mesh.triangleCount * 3,
                             mesh.vertexCount, now.data(), was.data());
    };
    auto drawInstanced = [&](const ::Mesh& mesh, const std::vector<float>& placements, std::size_t copies) {
        if (mesh.vboId == nullptr)
            return;
        const unsigned int indices =
            mesh.indices != nullptr ? mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_INDICES] : 0;
        m_post->DrawVelocityInstanced(mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION], indices,
                                      mesh.triangleCount * 3, mesh.vertexCount, placements.data(),
                                      static_cast<int>(copies), Columns(viewProjectionNow).data(),
                                      Columns(viewProjectionBefore).data());
    };
    // An instanced draw's copies, now and then -- a copy with no Previous then where it is now -- 32 floats a copy; and
    // the proxy its translucency is judged by, as its lit draw judged it (the faintest copy's tint).
    auto placementsOf = [&](const BRITE::InstancedModelDrawCommand& cmd, const ::Model& model, bool still) {
        std::vector<float> placements(cmd.Instances.size() * 32);
        for (std::size_t k = 0; k < cmd.Instances.size(); ++k) {
            const BRITE::ModelInstance& copy = cmd.Instances[k];
            BRITE::ModelDrawCommand placed;
            placed.Position = copy.Position;
            placed.Rotation = copy.Rotation;
            placed.Scale = copy.Scale;
            const std::array<float, 16> now = Columns(DrawTransform(model, placed));
            if (copy.Previous && !still) {
                placed.Position = copy.Previous->Position;
                placed.Rotation = copy.Previous->Rotation;
                placed.Scale = copy.Previous->Scale;
            }
            const std::array<float, 16> was = Columns(DrawTransform(model, placed));
            std::memcpy(&placements[k * 32], now.data(), sizeof(float) * 16);
            std::memcpy(&placements[k * 32 + 16], was.data(), sizeof(float) * 16);
        }
        return placements;
    };
    auto instancedProxy = [](const BRITE::InstancedModelDrawCommand& cmd) {
        BRITE::ModelDrawCommand proxy;
        proxy.Model = cmd.Model;
        proxy.Material = cmd.Material;
        int faintest = 255;
        for (const BRITE::ModelInstance& copy : cmd.Instances)
            faintest = std::min(faintest, static_cast<int>(copy.Tint.a));
        proxy.Material.AlbedoTint.a =
            static_cast<unsigned char>((static_cast<int>(cmd.Material.AlbedoTint.a) * faintest) / 255);
        return proxy;
    };

    // The moving draws: each opaque mesh's motion, from where the draw stood to where it stands, under last frame's
    // camera and this one's.
    m_post->UseVelocity(false, false, depthRange);
    for (std::size_t i = 0; i < pass.ModelCommands.size(); ++i) {
        const BRITE::ModelDrawCommand& cmd = pass.ModelCommands[i];
        const ::Model* model = modelOf(cmd.Model);
        if (!cmd.MotionBlurred || !cmd.Previous || model == nullptr)
            continue;
        BRITE::ModelDrawCommand was = cmd;
        was.Position = cmd.Previous->Position;
        was.Rotation = cmd.Previous->Rotation;
        was.Scale = cmd.Previous->Scale;
        const std::array<float, 16> mvpNow = Columns(::MatrixMultiply(DrawTransform(*model, cmd), viewProjectionNow));
        const std::array<float, 16> mvpBefore =
            Columns(::MatrixMultiply(DrawTransform(*model, was), viewProjectionBefore));
        PoseForDraw(cmd.Model, cmd.Pose, i);
        for (int m = 0; m < model->meshCount; ++m)
            if (!translucent(cmd, *model, m))
                drawMesh(model->meshes[m], mvpNow, mvpBefore);
    }
    m_post->UseVelocity(true, false, depthRange);
    for (const BRITE::InstancedModelDrawCommand& cmd : pass.InstancedModelCommands) {
        const ::Model* model = modelOf(cmd.Model);
        const bool moves = std::any_of(cmd.Instances.begin(), cmd.Instances.end(),
                                       [](const BRITE::ModelInstance& copy) { return copy.Previous.has_value(); });
        if (!cmd.MotionBlurred || !moves || model == nullptr)
            continue;
        const std::vector<float> placements = placementsOf(cmd, *model, false);
        const BRITE::ModelDrawCommand proxy = instancedProxy(cmd);
        PoseForDraw(cmd.Model, BRITE::ModelPose{}, InstancedPoseIndex);
        for (int m = 0; m < model->meshCount; ++m)
            if (!translucent(proxy, *model, m))
                drawInstanced(model->meshes[m], placements, cmd.Instances.size());
    }

    // The exempt draws, over them: every mesh, opaque or translucent, marked wherever it shows.
    m_post->UseVelocity(false, true, depthRange);
    for (std::size_t i = 0; i < pass.ModelCommands.size(); ++i) {
        const BRITE::ModelDrawCommand& cmd = pass.ModelCommands[i];
        const ::Model* model = modelOf(cmd.Model);
        if (cmd.MotionBlurred || model == nullptr)
            continue;
        const std::array<float, 16> mvp = Columns(::MatrixMultiply(DrawTransform(*model, cmd), viewProjectionNow));
        PoseForDraw(cmd.Model, cmd.Pose, i);
        for (int m = 0; m < model->meshCount; ++m)
            drawMesh(model->meshes[m], mvp, mvp);
    }
    m_post->UseVelocity(true, true, depthRange);
    for (const BRITE::InstancedModelDrawCommand& cmd : pass.InstancedModelCommands) {
        const ::Model* model = modelOf(cmd.Model);
        if (cmd.MotionBlurred || model == nullptr || cmd.Instances.empty())
            continue;
        const std::vector<float> placements = placementsOf(cmd, *model, true);
        PoseForDraw(cmd.Model, BRITE::ModelPose{}, InstancedPoseIndex);
        for (int m = 0; m < model->meshCount; ++m)
            drawInstanced(model->meshes[m], placements, cmd.Instances.size());
    }

    // A point's clip position now, back through this frame's view-projection and forward through last frame's.
    const std::array<float, 16> reprojection =
        Columns(::MatrixMultiply(::MatrixInvert(viewProjectionNow), viewProjectionBefore));
    m_post->EndMotion(settings, reprojection.data(), settings.PreviousCamera.has_value(), depthRange);
}

bool RaylibRenderBackend::ReadLastMotionVectors(int& width, int& height, std::vector<float>& rgba) const {
    return m_post && m_post->ReadMotionFloats(rgba, width, height);
}

bool RaylibRenderBackend::ReadLastMotionBlurred(int& width, int& height, std::vector<float>& rgba) const {
    return m_post && m_post->ReadBlurredFloats(rgba, width, height);
}

int RaylibRenderBackend::MotionBlurTargetSetsHeld() const {
    return m_post ? m_post->MotionSetsHeld() : 0;
}

bool RaylibRenderBackend::RenderPlanarReflection(const BRITE::RenderPass& pass, int targetWidth, int targetHeight,
                                                 bool hdr) {
    const BRITE::ReflectionSettings& settings = pass.Reflections;
    // A camera at or below the plane sees its underside, where the oblique near plane would face the wrong way: the
    // surfaces reflect the sky instead.
    if (!settings.Planar || pass.Camera3DPtr == nullptr || targetWidth <= 0 || targetHeight <= 0 ||
        pass.Camera3DPtr->position.y <= settings.PlaneHeight)
        return false;
    // Drawn only for a pass that has a draw to read it; that draw is not drawn into it.
    BRITE::RenderPass mirrored;
    mirrored.ModelCommands.reserve(pass.ModelCommands.size());
    bool anyReader = false;
    for (const BRITE::ModelDrawCommand& cmd : pass.ModelCommands) {
        if (cmd.Material.Reflection == BRITE::ReflectionMode::Planar)
            anyReader = true;
        else
            mirrored.ModelCommands.push_back(cmd);
    }
    for (const BRITE::InstancedModelDrawCommand& cmd : pass.InstancedModelCommands) {
        if (cmd.Material.Reflection == BRITE::ReflectionMode::Planar)
            anyReader = true;
        else
            mirrored.InstancedModelCommands.push_back(cmd);
    }
    if (!anyReader)
        return false;

    const float scale = std::clamp(settings.Resolution, 0.05f, 1.0f);
    const int width = std::max(1, static_cast<int>(static_cast<float>(targetWidth) * scale));
    const int height = std::max(1, static_cast<int>(static_cast<float>(targetHeight) * scale));
    const RenderTargetFormat format = hdr ? RenderTargetFormat::RGBA16F : RenderTargetFormat::RGBA8;
    if (!m_planar)
        m_planar = std::make_shared<PlanarState>();
    TargetObjects* acquired = m_planar->Acquire(width, height, format, m_passSerial);
    if (acquired == nullptr)
        return false;
    const TargetObjects& target = *acquired;

    // The camera mirrored in the plane y = h: below it, looking up at the mirrored target, its up mirrored too. Through
    // a point of the plane it sees what the real camera sees reflected there, so a reflecting fragment reads the point
    // it projects to; and its view is the real one's mirror image, so every point of the plane the real camera sees
    // lies inside it -- with the up left as it was, a rolled camera's mirror would be turned by twice its roll and
    // miss the corners.
    const float h = settings.PlaneHeight;
    BRITE::Camera3D camera = *pass.Camera3DPtr;
    camera.position.y = 2.0f * h - camera.position.y;
    camera.target.y = 2.0f * h - camera.target.y;
    camera.up.y = -camera.up.y;
    const ::Matrix view =
        ::MatrixLookAt({camera.position.x, camera.position.y, camera.position.z},
                       {camera.target.x, camera.target.y, camera.target.z}, {camera.up.x, camera.up.y, camera.up.z});
    const double nearPlane = ::rlGetCullDistanceNear(), farPlane = ::rlGetCullDistanceFar();
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    ::Matrix projection;
    if (camera.projection == BRITE::Math::CameraProjection::Perspective) {
        projection = ::MatrixPerspective(camera.fovy * DEG2RAD, aspect, nearPlane, farPlane);
    } else {
        const double top = camera.fovy / 2.0, right = top * aspect;
        projection = ::MatrixOrtho(-right, right, -top, top, nearPlane, farPlane);
    }
    // The plane y - h = 0, kept side up, in the mirrored camera's view: a plane transforms by the inverse of the
    // view, transposed.
    const ::Matrix fromView = ::MatrixInvert(view);
    const ::Vector4 world = {0.0f, 1.0f, 0.0f, -h};
    const ::Vector4 plane = {
        world.x * fromView.m0 + world.y * fromView.m1 + world.z * fromView.m2 + world.w * fromView.m3,
        world.x * fromView.m4 + world.y * fromView.m5 + world.z * fromView.m6 + world.w * fromView.m7,
        world.x * fromView.m8 + world.y * fromView.m9 + world.z * fromView.m10 + world.w * fromView.m11,
        world.x * fromView.m12 + world.y * fromView.m13 + world.z * fromView.m14 + world.w * fromView.m15};
    projection = ObliqueNearPlane(projection, plane);
    m_planar->viewProjection = ::MatrixMultiply(view, projection);

    // The pass's 3D world, but the reflecting draws, under the mirrored camera, without shadows.
    mirrored.Camera3DPtr = &camera;
    mirrored.Lights = pass.Lights;
    mirrored.AmbientColor = pass.AmbientColor;
    mirrored.AmbientIntensity = pass.AmbientIntensity;
    mirrored.Fog = pass.Fog;
    mirrored.Environment = pass.Environment;
    mirrored.Post = pass.Post;
    mirrored.Reflections = pass.Reflections;
    mirrored.Primitive3DCommands = pass.Primitive3DCommands;
    mirrored.Line3DCommands = pass.Line3DCommands;

    ::RenderTexture2D surface{};
    surface.id = target.framebuffer;
    surface.texture.id = target.colour;
    surface.texture.width = width;
    surface.texture.height = height;
    surface.texture.mipmaps = 1;
    surface.texture.format = hdr ? PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 : PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    ::BeginTextureMode(surface);
    const auto clear = [&](unsigned char c) {
        return hdr ? BRITE::ToneCurve::AuthoredToLinear(c / 255.0f, pass.Post.Exposure) : c / 255.0f;
    };
    ClearBound(clear(pass.ClearColor.r), clear(pass.ClearColor.g), clear(pass.ClearColor.b), 1.0f);
    std::shared_ptr<ShadowFrame> shadows = std::move(m_shadowFrame);
    std::memcpy(m_projection, &projection, sizeof(m_projection));
    m_projectionOverride = true;
    Draw3D(mirrored, hdr);
    m_projectionOverride = false;
    m_shadowFrame = std::move(shadows);
    ::EndTextureMode();
    // A skinned draw of the mirrored list may have left its pose in a model's buffers under another command's index:
    // the pass's own draws are told to pose again.
    ++m_passSerial;
    m_planarThisPass = true;
    return true;
}

bool RaylibRenderBackend::ReadLastPlanarReflection(int& width, int& height, std::vector<float>& rgba) const {
    if (!m_planarThisPass || !m_planar)
        return false;
    width = m_planar->Current().width;
    height = m_planar->Current().height;
    return ReadTargetFloats(m_planar->Current(), rgba);
}

int RaylibRenderBackend::PlanarReflectionTargetsHeld() const {
    return m_planar ? static_cast<int>(m_planar->slots.size()) : 0;
}

// ---------------------------------------------------------------------------
// Sun shadows
// ---------------------------------------------------------------------------

bool RaylibRenderBackend::RenderShadows(const BRITE::RenderPass& pass, int targetWidth, int targetHeight) {
    namespace Cascades = BRITE::ShadowCascades;
    const BRITE::ShadowSettings& settings = pass.Shadows;
    if (!settings.Enabled || pass.Camera3DPtr == nullptr ||
        (pass.ModelCommands.empty() && pass.InstancedModelCommands.empty()) || targetWidth <= 0 || targetHeight <= 0)
        return false;
    const int lights = static_cast<int>(std::min(pass.Lights.size(), BRITE::MaxLightsPerPass));
    if (settings.LightIndex < 0 || settings.LightIndex >= lights ||
        pass.Lights[static_cast<std::size_t>(settings.LightIndex)].Type != BRITE::LightType::Directional)
        return false;

    // The depth pass's shader, compiled on first use.
    if (m_shadowDepthShader == BRITE::NullShaderHandle) {
        m_shadowDepthShader =
            LoadShaderFromMemory(EmbeddedShaders::SHADOW_DEPTH_VERTEX, EmbeddedShaders::SHADOW_DEPTH_FRAGMENT);
        ::Shader* depth = static_cast<::Shader*>(m_shaders[m_shadowDepthShader]);
        m_shadowDepthWorks = !ShaderRefused(*depth);
        if (!m_shadowDepthWorks)
            spdlog::error("BRITE: the shadow depth shader failed to compile; every pass is drawn without shadows");
        m_shadowDepthAlphaMask = ::GetShaderLocation(*depth, "alphaMask");
        m_shadowDepthAlphaCutoff = ::GetShaderLocation(*depth, "alphaCutoff");
    }
    // Its instanced variant, for InstancedModelDrawCommand casters, compiled on the first pass that has any. Refused,
    // they cast nothing; the rest still do.
    if (m_shadowDepthInstancedShader == BRITE::NullShaderHandle && !pass.InstancedModelCommands.empty()) {
        std::string vertex = EmbeddedShaders::SHADOW_DEPTH_VERTEX;
        vertex.insert(vertex.find('\n') + 1, "#define BRITE_INSTANCED 1\n");
        m_shadowDepthInstancedShader = LoadShaderFromMemory(vertex.c_str(), EmbeddedShaders::SHADOW_DEPTH_FRAGMENT);
        ::Shader* instanced = static_cast<::Shader*>(m_shaders[m_shadowDepthInstancedShader]);
        m_shadowDepthInstancedWorks = !ShaderRefused(*instanced);
        if (m_shadowDepthWorks && !m_shadowDepthInstancedWorks)
            spdlog::error("BRITE: the instanced shadow depth shader failed to compile; instanced draws cast no shadow");
        m_shadowDepthInstancedAlphaMask = ::GetShaderLocation(*instanced, "alphaMask");
        m_shadowDepthInstancedAlphaCutoff = ::GetShaderLocation(*instanced, "alphaCutoff");
    }
    // The atlas: one tile per cascade, two across once there are two, two down once there are three.
    const int cascades = std::clamp(settings.Cascades, 1, Cascades::MaxCascades);
    const int refusedKey = ShadowVariantKey(cascades, static_cast<int>(settings.Filter));
    if (!m_shadowDepthWorks || std::find(m_shadowVariantsRefused.begin(), m_shadowVariantsRefused.end(), refusedKey) !=
                                   m_shadowVariantsRefused.end())
        return false; // a pass could not draw these shadows: none is drawn

    const int tilesAcross = cascades == 1 ? 1 : 2;
    const int tilesDown = cascades <= 2 ? 1 : 2;
    const int mapSize = std::max(settings.MapSize, 16);
    if (!m_shadowAtlas)
        m_shadowAtlas = std::make_shared<ShadowAtlas>();
    if (!m_shadowAtlas->Prepare(mapSize * tilesAcross, mapSize * tilesDown))
        return false;

    // The camera's frame and the view's size at a distance d: tan(half the angle) x d for a perspective view, half its
    // size for an orthographic one.
    const BRITE::Camera3D& camera = *pass.Camera3DPtr;
    const BRITE::Vector3 eye = camera.position;
    const BRITE::Vector3 forward =
        BRITE::Math::Normalize({camera.target.x - eye.x, camera.target.y - eye.y, camera.target.z - eye.z});
    const BRITE::Vector3 right = BRITE::Math::Normalize(BRITE::Math::CrossProduct(forward, camera.up));
    const BRITE::Vector3 up = BRITE::Math::CrossProduct(right, forward);
    const float aspect = static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
    const bool perspective = camera.projection == BRITE::Math::CameraProjection::Perspective;
    const float halfHeight = perspective ? std::tan(camera.fovy * BRITE::Deg2Rad * 0.5f) : camera.fovy * 0.5f;
    const float heightSlope = perspective ? halfHeight : 0.0f, heightConstant = perspective ? 0.0f : halfHeight;
    const float widthSlope = heightSlope * aspect, widthConstant = heightConstant * aspect;
    const float nearPlane = static_cast<float>(::rlGetCullDistanceNear());
    std::array<float, Cascades::MaxCascades - 1> splits = {settings.Splits[0], settings.Splits[1], settings.Splits[2]};
    const std::array<float, Cascades::MaxCascades> farEdges = Cascades::FarEdges(
        cascades, nearPlane, std::max(settings.Distance, 2.0f * nearPlane), settings.SplitLambda, splits);

    // The light's view: from the origin, looking the way the light travels.
    BRITE::Vector3 travel =
        BRITE::Math::Normalize(pass.Lights[static_cast<std::size_t>(settings.LightIndex)].Direction);
    if (BRITE::Math::Length(travel) < 0.5f)
        travel = {0.0f, -1.0f, 0.0f};
    const ::Vector3 lightUp = std::fabs(travel.y) > 0.99f ? ::Vector3{0.0f, 0.0f, 1.0f} : ::Vector3{0.0f, 1.0f, 0.0f};
    const ::Matrix lightView = ::MatrixLookAt({0.0f, 0.0f, 0.0f}, {travel.x, travel.y, travel.z}, lightUp);

    // The casters: every opaque mesh of a draw that casts, writes its depth and is lit -- an unlit draw (a sky dome, a
    // painted backdrop) is not part of the lit world and casts nothing -- with its bounding sphere in the light's view.
    struct Caster {
        std::size_t command;
        int mesh;
        int slot;
        ::Vector3 centre; // in the light's view
        float radius;
        ::Matrix transform; // the draw's
        bool cutOut;        // a cut-out, drawn with its albedo for the alpha test
        float cutoff;
        bool skinned; // a skinned mesh's pose can reach past its rest sphere: it is drawn into every cascade
    };
    std::vector<Caster> casters;
    for (std::size_t i = 0; i < pass.ModelCommands.size(); ++i) {
        const BRITE::ModelDrawCommand& cmd = pass.ModelCommands[i];
        if (!cmd.CastsShadow || !cmd.Material.DepthWrite || cmd.Material.Unlit || cmd.Model == BRITE::NullModelHandle)
            continue;
        auto it = m_models.find(cmd.Model);
        if (it == m_models.end())
            continue;
        const ::Model& model = *static_cast<::Model*>(it->second);
        std::vector<MeshSphere>& spheres = m_meshSpheres[cmd.Model];
        if (spheres.size() != static_cast<std::size_t>(model.meshCount)) {
            spheres.clear();
            for (int m = 0; m < model.meshCount; ++m) {
                const ::BoundingBox box = ::GetMeshBoundingBox(model.meshes[m]);
                const ::Vector3 centre = ::Vector3Scale(::Vector3Add(box.min, box.max), 0.5f);
                spheres.push_back({{centre.x, centre.y, centre.z}, ::Vector3Distance(box.max, centre)});
            }
        }
        const ::Matrix transform = DrawTransform(model, cmd);
        // The sphere grows by at most the transform's Frobenius norm over its 3x3: a bound on how far it can stretch
        // any direction, rotated and non-uniformly scaled alike.
        const float scale =
            std::sqrt(transform.m0 * transform.m0 + transform.m1 * transform.m1 + transform.m2 * transform.m2 +
                      transform.m4 * transform.m4 + transform.m5 * transform.m5 + transform.m6 * transform.m6 +
                      transform.m8 * transform.m8 + transform.m9 * transform.m9 + transform.m10 * transform.m10);
        const bool skinned = m_skins.find(cmd.Model) != m_skins.end();
        for (int m = 0; m < model.meshCount; ++m) {
            const int slot = MeshMaterialSlot(model.meshMaterial[m], model.materialCount);
            const BRITE::GltfMaterialInfo* file = FileMaterial(cmd.Model, slot);
            if (MeshTranslucent(cmd, slot, model.materials[slot].maps[MATERIAL_MAP_DIFFUSE].color.a) ||
                (file != nullptr && file->Unlit))
                continue;
            const MeshSphere& sphere = spheres[static_cast<std::size_t>(m)];
            const ::Vector3 world = ::Vector3Transform({sphere.centre.x, sphere.centre.y, sphere.centre.z}, transform);
            const bool fileMasks = file != nullptr && file->AlphaMode == BRITE::GltfAlphaMode::Mask;
            const bool cutOut = cmd.Material.AlphaMask || fileMasks;
            const float cutoff = cmd.Material.AlphaMask ? cmd.Material.AlphaCutoff
                                 : fileMasks            ? file->AlphaCutoff
                                                        : 0.0f;
            casters.push_back({i, m, slot, ::Vector3Transform(world, lightView), sphere.radius * scale, transform,
                               cutOut, cutoff, skinned});
        }
    }

    // The instanced casters (InstancedModelDrawCommand): the same rule for each opaque mesh, and a bounding sphere for
    // every copy, so each cascade draws only the copies that reach it -- in one draw per mesh. A skinned model's copies
    // are drawn at rest, so their rest spheres hold and they are culled like any other.
    struct InstancedCaster {
        std::size_t command;
        int mesh;
        int slot;
        bool cutOut;
        float cutoff;
        std::vector<::Vector3> centres; // each copy's, in the light's view
        std::vector<float> radii;
    };
    std::vector<InstancedCaster> instancedCasters;
    std::vector<std::vector<InstanceData>> instancedData(pass.InstancedModelCommands.size());
    for (std::size_t i = 0; i < pass.InstancedModelCommands.size() && m_shadowDepthInstancedWorks; ++i) {
        const BRITE::InstancedModelDrawCommand& cmd = pass.InstancedModelCommands[i];
        if (!cmd.CastsShadow || !cmd.Material.DepthWrite || cmd.Material.Unlit || cmd.Model == BRITE::NullModelHandle ||
            cmd.Instances.empty())
            continue;
        auto it = m_models.find(cmd.Model);
        if (it == m_models.end())
            continue;
        const ::Model& model = *static_cast<::Model*>(it->second);
        // A copy whose tint is faint makes the mesh translucent (RenderPass.hpp), and a translucent mesh never casts.
        BRITE::ModelDrawCommand proxy;
        proxy.Model = cmd.Model;
        proxy.Material = cmd.Material;
        int faintest = 255;
        for (const BRITE::ModelInstance& copy : cmd.Instances)
            faintest = std::min(faintest, static_cast<int>(copy.Tint.a));
        proxy.Material.AlbedoTint.a =
            static_cast<unsigned char>((static_cast<int>(cmd.Material.AlbedoTint.a) * faintest) / 255);
        std::vector<MeshSphere>& spheres = m_meshSpheres[cmd.Model];
        if (spheres.size() != static_cast<std::size_t>(model.meshCount)) {
            spheres.clear();
            for (int m = 0; m < model.meshCount; ++m) {
                const ::BoundingBox box = ::GetMeshBoundingBox(model.meshes[m]);
                const ::Vector3 centre = ::Vector3Scale(::Vector3Add(box.min, box.max), 0.5f);
                spheres.push_back({{centre.x, centre.y, centre.z}, ::Vector3Distance(box.max, centre)});
            }
        }
        std::vector<::Matrix> transforms(cmd.Instances.size());
        std::vector<float> scales(cmd.Instances.size());
        std::vector<InstanceData>& data = instancedData[i];
        data.resize(cmd.Instances.size());
        for (std::size_t k = 0; k < cmd.Instances.size(); ++k) {
            const BRITE::ModelInstance& copy = cmd.Instances[k];
            BRITE::ModelDrawCommand placed;
            placed.Position = copy.Position;
            placed.Rotation = copy.Rotation;
            placed.Scale = copy.Scale;
            const ::Matrix& t = transforms[k] = DrawTransform(model, placed);
            // As a model draw's: the sphere grows by at most the Frobenius norm of the placement's 3x3.
            scales[k] = std::sqrt(t.m0 * t.m0 + t.m1 * t.m1 + t.m2 * t.m2 + t.m4 * t.m4 + t.m5 * t.m5 + t.m6 * t.m6 +
                                  t.m8 * t.m8 + t.m9 * t.m9 + t.m10 * t.m10);
            const ::float16 columns = ::MatrixToFloatV(t);
            std::memcpy(data[k].transform, columns.v, sizeof(data[k].transform));
            data[k].tint[0] = copy.Tint.r / 255.0f;
            data[k].tint[1] = copy.Tint.g / 255.0f;
            data[k].tint[2] = copy.Tint.b / 255.0f;
            data[k].tint[3] = copy.Tint.a / 255.0f;
        }
        for (int m = 0; m < model.meshCount; ++m) {
            const int slot = MeshMaterialSlot(model.meshMaterial[m], model.materialCount);
            const BRITE::GltfMaterialInfo* file = FileMaterial(cmd.Model, slot);
            if (MeshTranslucent(proxy, slot, model.materials[slot].maps[MATERIAL_MAP_DIFFUSE].color.a) ||
                (file != nullptr && file->Unlit))
                continue;
            const bool fileMasks = file != nullptr && file->AlphaMode == BRITE::GltfAlphaMode::Mask;
            InstancedCaster caster{i,
                                   m,
                                   slot,
                                   cmd.Material.AlphaMask || fileMasks,
                                   cmd.Material.AlphaMask ? cmd.Material.AlphaCutoff
                                   : fileMasks            ? file->AlphaCutoff
                                                          : 0.0f,
                                   {},
                                   {}};
            const MeshSphere& sphere = spheres[static_cast<std::size_t>(m)];
            caster.centres.resize(cmd.Instances.size());
            caster.radii.resize(cmd.Instances.size());
            for (std::size_t k = 0; k < cmd.Instances.size(); ++k) {
                const ::Vector3 world =
                    ::Vector3Transform({sphere.centre.x, sphere.centre.y, sphere.centre.z}, transforms[k]);
                caster.centres[k] = ::Vector3Transform(world, lightView);
                caster.radii[k] = sphere.radius * scales[k];
            }
            instancedCasters.push_back(std::move(caster));
        }
    }
    // Which copies of each instanced caster reach each cascade.
    std::vector<std::array<std::vector<std::uint32_t>, Cascades::MaxCascades>> copiesReaching(instancedCasters.size());

    auto frame = std::make_shared<ShadowFrame>();
    frame->cascades = cascades;
    frame->forward[0] = forward.x;
    frame->forward[1] = forward.y;
    frame->forward[2] = forward.z;

    // Each cascade's box, and which casters reach it.
    std::array<::Matrix, Cascades::MaxCascades> projections{};
    std::vector<std::array<bool, Cascades::MaxCascades>> reaches(casters.size());
    float edgeNear = nearPlane;
    for (int c = 0; c < cascades; ++c) {
        const float edgeFar = farEdges[static_cast<std::size_t>(c)];
        const Cascades::Sphere slice = Cascades::SliceSphere(eye, forward, right, up, edgeNear, edgeFar, widthSlope,
                                                             widthConstant, heightSlope, heightConstant);
        edgeNear = edgeFar;
        const float radius = slice.Radius;
        const float texel = Cascades::TexelSize(radius, mapSize);
        ::Vector3 centre = ::Vector3Transform({slice.Centre.x, slice.Centre.y, slice.Centre.z}, lightView);
        centre.x = Cascades::SnapToTexel(centre.x, texel);
        centre.y = Cascades::SnapToTexel(centre.y, texel);

        // The casters that can reach this cascade -- every skinned one -- and the nearest of them to the sun, which
        // sets the box's near side.
        int draws = 0;
        float sunward = centre.z + radius;
        for (std::size_t k = 0; k < casters.size(); ++k) {
            const Caster& caster = casters[k];
            const bool reaching =
                caster.skinned || Cascades::CasterReachesCascade({caster.centre.x, caster.centre.y, caster.centre.z},
                                                                 caster.radius, {centre.x, centre.y, centre.z}, radius);
            reaches[k][static_cast<std::size_t>(c)] = reaching;
            if (!reaching)
                continue;
            ++draws;
            sunward = std::max(sunward, caster.centre.z + caster.radius);
        }
        for (std::size_t k = 0; k < instancedCasters.size(); ++k) {
            const InstancedCaster& caster = instancedCasters[k];
            std::vector<std::uint32_t>& reached = copiesReaching[k][static_cast<std::size_t>(c)];
            for (std::size_t copy = 0; copy < caster.centres.size(); ++copy) {
                const ::Vector3& at = caster.centres[copy];
                if (!Cascades::CasterReachesCascade({at.x, at.y, at.z}, caster.radii[copy],
                                                    {centre.x, centre.y, centre.z}, radius))
                    continue;
                reached.push_back(static_cast<std::uint32_t>(copy));
                sunward = std::max(sunward, at.z + caster.radii[copy]);
            }
            if (!reached.empty())
                ++draws; // one draw for all the copies of this mesh that reach the cascade
        }
        // The light's view looks down -z: the box spans depths from the sunward caster to the slice's far side.
        projections[static_cast<std::size_t>(c)] = ::MatrixOrtho(
            centre.x - radius, centre.x + radius, centre.y - radius, centre.y + radius, -sunward, -(centre.z - radius));
        frame->lightViewProjection[c] = ::MatrixMultiply(lightView, projections[static_cast<std::size_t>(c)]);
        const int tileX = c % tilesAcross, tileY = c / tilesAcross;
        frame->rect[c][0] = static_cast<float>(tileX) / static_cast<float>(tilesAcross);
        frame->rect[c][1] = static_cast<float>(tileY) / static_cast<float>(tilesDown);
        frame->rect[c][2] = 1.0f / static_cast<float>(tilesAcross);
        frame->rect[c][3] = 1.0f / static_cast<float>(tilesDown);
        frame->farEdge[c] = edgeFar;
        frame->texel[c] = texel;

        ShadowCascadeReport report;
        report.CentreX = centre.x;
        report.CentreY = centre.y;
        report.Radius = radius;
        report.Texel = texel;
        report.FarEdge = edgeFar;
        report.CasterDraws = draws;
        m_shadowReport.push_back(report);
    }

    // The depth pass, caster by caster with the cascades inside, so each draw is posed once for all its cascades -- and
    // once more for its own lit draw after, unless that draw is the one posed last (PoseForDraw keeps only the newest
    // pose): about two poses a skinned draw a pass, not one.
    ::rlDrawRenderBatchActive();
    const ::Matrix savedProjection = ::rlGetMatrixProjection();
    const ::Matrix savedModelview = ::rlGetMatrixModelview();
    m_shadowAtlas->Begin();
    const ::Shader& depthShader = *static_cast<::Shader*>(m_shaders[m_shadowDepthShader]);
    // Most casters are drawn the lean way: the mesh's vertex array and one matrix, no material, since only depth is
    // written. A cut-out is drawn through DrawMesh with its albedo bound, for the alpha test.
    bool leanReady = false;
    const int noCutOut = 0;
    for (std::size_t k = 0; k < casters.size(); ++k) {
        const Caster& caster = casters[k];
        const BRITE::ModelDrawCommand& cmd = pass.ModelCommands[caster.command];
        const ::Model& model = *static_cast<::Model*>(m_models[cmd.Model]);
        bool posed = false;
        for (int c = 0; c < cascades; ++c) {
            if (!reaches[k][static_cast<std::size_t>(c)])
                continue;
            if (!posed) {
                PoseForDraw(cmd.Model, cmd.Pose, caster.command); // the pose the draw itself takes, kept for it
                posed = true;
            }
            const ::Matrix& projection = projections[static_cast<std::size_t>(c)];
            m_shadowAtlas->Tile(c % tilesAcross, c / tilesAcross, mapSize);
            const ::Mesh& mesh = model.meshes[caster.mesh];
            if (!caster.cutOut && mesh.vaoId != 0) {
                if (!leanReady) {
                    ::rlEnableShader(depthShader.id);
                    ::rlSetUniform(m_shadowDepthAlphaMask, &noCutOut, RL_SHADER_UNIFORM_INT, 1);
                    leanReady = true;
                }
                // As DrawMesh composes it: the model's transform, then the view, then the projection.
                ::rlSetUniformMatrix(depthShader.locs[SHADER_LOC_MATRIX_MVP],
                                     ::MatrixMultiply(::MatrixMultiply(caster.transform, lightView), projection));
                ::rlEnableVertexArray(mesh.vaoId);
                if (mesh.indices != nullptr)
                    ::rlDrawVertexArrayElements(0, mesh.triangleCount * 3, nullptr);
                else
                    ::rlDrawVertexArray(0, mesh.vertexCount);
                ::rlDisableVertexArray();
                continue;
            }
            leanReady = false; // DrawMesh leaves no program bound
            ::rlSetMatrixProjection(projection);
            ::rlSetMatrixModelview(lightView);
            ::MaterialMap maps[MATERIAL_MAPS_PER_MATERIAL];
            std::memcpy(maps, model.materials[caster.slot].maps, sizeof(maps));
            ::Material material = model.materials[caster.slot];
            material.maps = maps;
            material.shader = depthShader;
            // A cut-out casts what it cuts: the draw's albedo map or the model's own, and its colour's alpha.
            if (cmd.Material.AlbedoMap != BRITE::NullTextureHandle) {
                auto texture = m_textures.find(cmd.Material.AlbedoMap);
                if (texture != m_textures.end() && !texture->second.isRenderTexture)
                    maps[MATERIAL_MAP_ALBEDO].texture = *static_cast<::Texture2D*>(texture->second.ptr);
            }
            const ::Color own = maps[MATERIAL_MAP_DIFFUSE].color;
            maps[MATERIAL_MAP_DIFFUSE].color.a =
                static_cast<unsigned char>((static_cast<int>(own.a) * cmd.Material.AlbedoTint.a) / 255);
            const int cutOut = caster.cutOut ? 1 : 0;
            ::SetShaderValue(depthShader, m_shadowDepthAlphaMask, &cutOut, SHADER_UNIFORM_INT);
            ::SetShaderValue(depthShader, m_shadowDepthAlphaCutoff, &caster.cutoff, SHADER_UNIFORM_FLOAT);
            ::DrawMesh(mesh, material, caster.transform);
        }
    }
    if (leanReady)
        ::rlDisableShader();

    // The instanced casters: for each mesh and cascade, the copies that reach it, in one draw. A cut-out's albedo map
    // and colour go to the depth shader for its alpha test, as DrawMesh gives them to a model draw's.
    if (!instancedCasters.empty()) {
        if (!m_instances)
            m_instances = std::make_shared<InstanceStream>();
        const ::Shader& shader = *static_cast<::Shader*>(m_shaders[m_shadowDepthInstancedShader]);
        std::vector<InstanceData> reaching;
        for (std::size_t k = 0; k < instancedCasters.size(); ++k) {
            const InstancedCaster& caster = instancedCasters[k];
            const BRITE::InstancedModelDrawCommand& cmd = pass.InstancedModelCommands[caster.command];
            const ::Model& model = *static_cast<::Model*>(m_models[cmd.Model]);
            const ::Mesh& mesh = model.meshes[caster.mesh];
            PoseForDraw(cmd.Model, BRITE::ModelPose{}, InstancedPoseIndex);
            ::Texture2D albedo = model.materials[caster.slot].maps[MATERIAL_MAP_ALBEDO].texture;
            if (cmd.Material.AlbedoMap != BRITE::NullTextureHandle) {
                auto texture = m_textures.find(cmd.Material.AlbedoMap);
                if (texture != m_textures.end() && !texture->second.isRenderTexture)
                    albedo = *static_cast<::Texture2D*>(texture->second.ptr);
            }
            const ::Color own = model.materials[caster.slot].maps[MATERIAL_MAP_DIFFUSE].color;
            const float colour[4] = {1.0f, 1.0f, 1.0f,
                                     static_cast<float>((static_cast<int>(own.a) * cmd.Material.AlbedoTint.a) / 255) /
                                         255.0f};
            const int cutOut = caster.cutOut ? 1 : 0;
            for (int c = 0; c < cascades; ++c) {
                const std::vector<std::uint32_t>& reached = copiesReaching[k][static_cast<std::size_t>(c)];
                if (reached.empty())
                    continue;
                reaching.clear();
                for (const std::uint32_t copy : reached)
                    reaching.push_back(instancedData[caster.command][copy]);
                m_shadowAtlas->Tile(c % tilesAcross, c / tilesAcross, mapSize);
                ::rlEnableShader(shader.id);
                ::rlSetUniform(m_shadowDepthInstancedAlphaMask, &cutOut, RL_SHADER_UNIFORM_INT, 1);
                ::rlSetUniform(m_shadowDepthInstancedAlphaCutoff, &caster.cutoff, RL_SHADER_UNIFORM_FLOAT, 1);
                if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] != -1)
                    ::rlSetUniform(shader.locs[SHADER_LOC_COLOR_DIFFUSE], colour, SHADER_UNIFORM_VEC4, 1);
                ::rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_MVP],
                                     ::MatrixMultiply(lightView, projections[static_cast<std::size_t>(c)]));
                if (caster.cutOut && albedo.id != 0) {
                    const int unit = 0;
                    ::rlActiveTextureSlot(unit);
                    ::rlEnableTexture(albedo.id);
                    if (shader.locs[SHADER_LOC_MAP_DIFFUSE] != -1)
                        ::rlSetUniform(shader.locs[SHADER_LOC_MAP_DIFFUSE], &unit, SHADER_UNIFORM_INT, 1);
                }
                m_instances->Draw(mesh.vaoId, mesh.indices != nullptr, mesh.triangleCount * 3, mesh.vertexCount,
                                  reaching.data(), static_cast<int>(reaching.size()));
                if (caster.cutOut && albedo.id != 0)
                    ::rlDisableTexture();
                ::rlDisableShader();
            }
        }
    }
    m_shadowAtlas->End();
    ::rlSetMatrixProjection(savedProjection);
    ::rlSetMatrixModelview(savedModelview);
    m_shadowFrame = std::move(frame);
    return true;
}

// ---------------------------------------------------------------------------
// The PBR shader: compiled from the embedded source on first use
// ---------------------------------------------------------------------------

void RaylibRenderBackend::EnsurePbrShader(const PbrVariant& variant) {
    const int key = variant.Key();
    auto found = m_pbrVariants.find(key);
    if (found != m_pbrVariants.end()) {
        m_pbrShader = found->second.first;
        m_pbrLocs = found->second.second;
        return;
    }

    // The variant's features, defined just after the source's #version line (pbr.fs, THE VARIANTS).
    std::string fragment = EmbeddedShaders::PBR_FRAGMENT;
    std::string defines = "#define BRITE_HDR " + std::to_string(variant.hdr ? 1 : 0) +
                          "\n#define BRITE_SHADOW_CASCADES " + std::to_string(variant.shadowCascades) +
                          "\n#define BRITE_SHADOW_FILTER " + std::to_string(variant.shadowFilter) +
                          "\n#define BRITE_REFLECTIONS " + std::to_string(variant.reflections ? 1 : 0) + "\n";
    fragment.insert(fragment.find('\n') + 1, defines);
    // The vertex stage's one feature: an instanced draw (pbr.vs). A variant without it is the source as it stands.
    std::string vertex = EmbeddedShaders::PBR_VERTEX;
    if (variant.instanced)
        vertex.insert(vertex.find('\n') + 1, "#define BRITE_INSTANCED 1\n");
    if (m_refuseInstancedShadowsForTest && variant.instanced && variant.shadowCascades > 0)
        vertex.insert(vertex.find('\n') + 1, "#error refused for a test\n");
    m_pbrShader = LoadShaderFromMemory(vertex.c_str(), fragment.c_str());
    m_pbrLocs = {};
    ::Shader* shader = static_cast<::Shader*>(m_shaders[m_pbrShader]);
    if (ShaderRefused(*shader) && variant.shadowCascades > 0) {
        // The shadows are what the device refused: the same features without them, rather than no program. Only
        // a model draw's variant refuses these shadows for the passes after it; an instanced one's refusal drops them
        // for the instanced copies alone, since every other draw, and the depth pass, still have them.
        if (variant.instanced) {
            spdlog::error("BRITE: the instanced lit shader with {} shadow cascades failed to compile; instanced copies "
                          "are drawn without shadows",
                          variant.shadowCascades);
        } else {
            spdlog::error("BRITE: the lit shader with {} shadow cascades failed to compile; the pass is drawn without "
                          "shadows",
                          variant.shadowCascades);
            m_shadowVariantsRefused.push_back(ShadowVariantKey(variant.shadowCascades, variant.shadowFilter));
        }
        PbrVariant without = variant;
        without.shadowCascades = 0;
        EnsurePbrShader(without);
        m_pbrVariants[key] = {m_pbrShader, m_pbrLocs};
        return;
    }
    if (ShaderRefused(*shader)) {
        // The device refused the lit shader: it left no program, or raylib's default (ShaderRefused). The compiler's
        // own messages precede this line in the raylib log.
        if (variant.instanced)
            spdlog::error(
                "BRITE: the embedded PBR shader's instanced variant was refused; instanced copies are skipped");
        else
            spdlog::error("BRITE: the embedded PBR shader was refused (no program, or raylib's default one); the "
                          "compiler's messages are above");
    }

    shader->locs[SHADER_LOC_MAP_ALBEDO] = ::GetShaderLocation(*shader, "albedoMap");
    shader->locs[SHADER_LOC_MAP_METALNESS] = ::GetShaderLocation(*shader, "metallicMap");
    shader->locs[SHADER_LOC_MAP_ROUGHNESS] = ::GetShaderLocation(*shader, "roughnessMap");
    shader->locs[SHADER_LOC_MAP_OCCLUSION] = ::GetShaderLocation(*shader, "occlusionMap");
    shader->locs[SHADER_LOC_MAP_NORMAL] = ::GetShaderLocation(*shader, "normalMap");
    shader->locs[SHADER_LOC_MAP_EMISSION] = ::GetShaderLocation(*shader, "emissiveMap");
    shader->locs[SHADER_LOC_MAP_IRRADIANCE] = ::GetShaderLocation(*shader, "irradianceMap");
    shader->locs[SHADER_LOC_MAP_PREFILTER] = ::GetShaderLocation(*shader, "prefilterMap");

    auto loc = [&](const char* name) { return ::GetShaderLocation(*shader, name); };
    m_pbrLocs.numOfLights = loc("numOfLights");
    m_pbrLocs.useIBL = loc("useIBL");
    m_pbrLocs.viewPos = loc("viewPos");
    m_pbrLocs.ambientColor = loc("ambientColor");
    m_pbrLocs.ambient = loc("ambient");
    m_pbrLocs.useTexAlbedo = loc("useTexAlbedo");
    m_pbrLocs.useTexNormal = loc("useTexNormal");
    m_pbrLocs.useTexMetallic = loc("useTexMetallic");
    m_pbrLocs.useTexRoughness = loc("useTexRoughness");
    m_pbrLocs.useTexOcclusion = loc("useTexOcclusion");
    m_pbrLocs.useTexEmissive = loc("useTexEmissive");
    m_pbrLocs.metallicValue = loc("metallicValue");
    m_pbrLocs.roughnessValue = loc("roughnessValue");
    m_pbrLocs.occlusionStrength = loc("occlusionStrength");
    m_pbrLocs.normalScale = loc("normalScale");
    m_pbrLocs.unlit = loc("unlit");
    m_pbrLocs.alphaMask = loc("alphaMask");
    m_pbrLocs.alphaCutoff = loc("alphaCutoff");
    m_pbrLocs.alphaBlend = loc("alphaBlend");
    m_pbrLocs.doubleSided = loc("doubleSided");
    m_pbrLocs.emissiveLight = loc("emissiveLight");
    m_pbrLocs.fogEnabled = loc("fogEnabled");
    m_pbrLocs.fogColor = loc("fogColor");
    m_pbrLocs.fogStart = loc("fogStart");
    m_pbrLocs.fogEnd = loc("fogEnd");
    m_pbrLocs.exposure = loc("exposure");
    m_pbrLocs.shadowLight = loc("shadowLight");
    m_pbrLocs.shadowMap = loc("shadowMap");
    m_pbrLocs.shadowTexelUV = loc("shadowTexelUV");
    m_pbrLocs.shadowDepthBias = loc("shadowDepthBias");
    m_pbrLocs.shadowNormalBias = loc("shadowNormalBias");
    m_pbrLocs.viewForward = loc("viewForward");
    m_pbrLocs.reflection = loc("reflection");
    m_pbrLocs.reflectionF0 = loc("reflectionF0");
    m_pbrLocs.reflectionDistortion = loc("reflectionDistortion");
    m_pbrLocs.skyColor = loc("skyColor");
    m_pbrLocs.planarMap = loc("planarMap");
    m_pbrLocs.planarMatrix = loc("planarMatrix");
    for (int c = 0; c < BRITE::ShadowCascades::MaxCascades; ++c) {
        const std::string at = "[" + std::to_string(c) + "]";
        m_pbrLocs.shadowMatrix[c] = loc(("shadowMatrix" + at).c_str());
        m_pbrLocs.shadowRect[c] = loc(("shadowRect" + at).c_str());
        m_pbrLocs.cascadeFar[c] = loc(("cascadeFar" + at).c_str());
        m_pbrLocs.cascadeTexel[c] = loc(("cascadeTexel" + at).c_str());
    }
    for (std::size_t i = 0; i < BRITE::MaxLightsPerPass; ++i) {
        const std::string prefix = "lights[" + std::to_string(i) + "].";
        auto& l = m_pbrLocs.lights[i];
        l.enabled = loc((prefix + "enabled").c_str());
        l.type = loc((prefix + "type").c_str());
        l.position = loc((prefix + "position").c_str());
        l.direction = loc((prefix + "direction").c_str());
        l.color = loc((prefix + "color").c_str());
        l.intensity = loc((prefix + "intensity").c_str());
    }

    // The texture-tiling uniforms have no per-material data yet; identity so a
    // textured model reads its map once across its UV range.
    const float tiling[2] = {1.0f, 1.0f};
    const float offset[2] = {0.0f, 0.0f};
    ::SetShaderValue(*shader, loc("tiling"), tiling, SHADER_UNIFORM_VEC2);
    ::SetShaderValue(*shader, loc("offset"), offset, SHADER_UNIFORM_VEC2);
    m_pbrVariants[key] = {m_pbrShader, m_pbrLocs};
}

bool RaylibRenderBackend::PbrVariantHasUniform(bool hdr, int shadowCascades, const char* uniformName) {
    PbrVariant variant;
    variant.hdr = hdr;
    variant.shadowCascades = shadowCascades;
    EnsurePbrShader(variant);
    return ::GetShaderLocation(*static_cast<::Shader*>(m_shaders[m_pbrShader]), uniformName) >= 0;
}

void RaylibRenderBackend::ApplyPassLighting(const BRITE::RenderPass& pass) {
    ::Shader* shader = static_cast<::Shader*>(m_shaders[m_pbrShader]);

    // The HDR variant's exposure (the variant itself says whether the pass is HDR).
    ::SetShaderValue(*shader, m_pbrLocs.exposure, &pass.Post.Exposure, SHADER_UNIFORM_FLOAT);

    // The shadow variant's atlas and cascades, from this pass's shadow pass. The atlas is bound to a texture unit no
    // material map reaches (raylib binds maps to units 0 to 11), for every lit draw of the pass.
    if (m_shadowFrame && m_shadowFrame->cascades > 0) {
        const ShadowFrame& f = *m_shadowFrame;
        const int unit = ShadowTextureUnit;
        ::SetShaderValue(*shader, m_pbrLocs.shadowMap, &unit, SHADER_UNIFORM_INT);
        ::rlActiveTextureSlot(unit);
        ::rlEnableTexture(m_shadowAtlas->Texture());
        ::rlActiveTextureSlot(0);
        ::SetShaderValue(*shader, m_pbrLocs.shadowLight, &pass.Shadows.LightIndex, SHADER_UNIFORM_INT);
        ::SetShaderValue(*shader, m_pbrLocs.shadowDepthBias, &pass.Shadows.DepthBias, SHADER_UNIFORM_FLOAT);
        ::SetShaderValue(*shader, m_pbrLocs.shadowNormalBias, &pass.Shadows.NormalBias, SHADER_UNIFORM_FLOAT);
        const float texelUV[2] = {1.0f / static_cast<float>(m_shadowAtlas->Width()),
                                  1.0f / static_cast<float>(m_shadowAtlas->Height())};
        ::SetShaderValue(*shader, m_pbrLocs.shadowTexelUV, texelUV, SHADER_UNIFORM_VEC2);
        ::SetShaderValue(*shader, m_pbrLocs.viewForward, f.forward, SHADER_UNIFORM_VEC3);
        for (int c = 0; c < f.cascades; ++c) {
            ::SetShaderValueMatrix(*shader, m_pbrLocs.shadowMatrix[c], f.lightViewProjection[c]);
            ::SetShaderValue(*shader, m_pbrLocs.shadowRect[c], f.rect[c], SHADER_UNIFORM_VEC4);
            ::SetShaderValue(*shader, m_pbrLocs.cascadeFar[c], &f.farEdge[c], SHADER_UNIFORM_FLOAT);
            ::SetShaderValue(*shader, m_pbrLocs.cascadeTexel[c], &f.texel[c], SHADER_UNIFORM_FLOAT);
        }
    }

    const int count = static_cast<int>(std::min(pass.Lights.size(), BRITE::MaxLightsPerPass));
    for (std::size_t i = 0; i < BRITE::MaxLightsPerPass; ++i) {
        const auto& locs = m_pbrLocs.lights[i];
        const int enabled = static_cast<int>(i) < count ? 1 : 0;
        ::SetShaderValue(*shader, locs.enabled, &enabled, SHADER_UNIFORM_INT);
        if (!enabled)
            continue;

        const BRITE::Light& light = pass.Lights[i];
        const int type = light.Type == BRITE::LightType::Directional ? 0 : 1;
        ::SetShaderValue(*shader, locs.type, &type, SHADER_UNIFORM_INT);

        const float position[3] = {light.Position.x, light.Position.y, light.Position.z};
        ::SetShaderValue(*shader, locs.position, position, SHADER_UNIFORM_VEC3);

        // Normalised here so the shader can use it as-is. A zero direction is
        // meaningless for a sun; it becomes straight down rather than NaN.
        BRITE::Vector3 dir = BRITE::Math::Normalize(light.Direction);
        if (BRITE::Math::Length(dir) < 0.5f)
            dir = {0.0f, -1.0f, 0.0f};
        const float direction[3] = {dir.x, dir.y, dir.z};
        ::SetShaderValue(*shader, locs.direction, direction, SHADER_UNIFORM_VEC3);

        const float color[4] = {light.Tint.r / 255.0f, light.Tint.g / 255.0f, light.Tint.b / 255.0f,
                                light.Tint.a / 255.0f};
        ::SetShaderValue(*shader, locs.color, color, SHADER_UNIFORM_VEC4);
        ::SetShaderValue(*shader, locs.intensity, &light.Intensity, SHADER_UNIFORM_FLOAT);
    }
    ::SetShaderValue(*shader, m_pbrLocs.numOfLights, &count, SHADER_UNIFORM_INT);

    const float ambientColor[3] = {pass.AmbientColor.r / 255.0f, pass.AmbientColor.g / 255.0f,
                                   pass.AmbientColor.b / 255.0f};
    ::SetShaderValue(*shader, m_pbrLocs.ambientColor, ambientColor, SHADER_UNIFORM_VEC3);
    ::SetShaderValue(*shader, m_pbrLocs.ambient, &pass.AmbientIntensity, SHADER_UNIFORM_FLOAT);

    // Every pass sets the fog, off included: a uniform keeps its value, so a
    // fogged pass would otherwise fog every pass after it.
    const int fogEnabled = pass.Fog.Enabled ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.fogEnabled, &fogEnabled, SHADER_UNIFORM_INT);
    const float fogColor[3] = {pass.Fog.Tint.r / 255.0f, pass.Fog.Tint.g / 255.0f, pass.Fog.Tint.b / 255.0f};
    ::SetShaderValue(*shader, m_pbrLocs.fogColor, fogColor, SHADER_UNIFORM_VEC3);
    ::SetShaderValue(*shader, m_pbrLocs.fogStart, &pass.Fog.Start, SHADER_UNIFORM_FLOAT);
    ::SetShaderValue(*shader, m_pbrLocs.fogEnd, &pass.Fog.End, SHADER_UNIFORM_FLOAT);

    // The reflection variant's sky, and the planar reflection when this pass drew one.
    const float skyColor[3] = {pass.Reflections.SkyColor.r / 255.0f, pass.Reflections.SkyColor.g / 255.0f,
                               pass.Reflections.SkyColor.b / 255.0f};
    ::SetShaderValue(*shader, m_pbrLocs.skyColor, skyColor, SHADER_UNIFORM_VEC3);
    if (m_planarThisPass) {
        const int unit = PlanarTextureUnit;
        ::SetShaderValue(*shader, m_pbrLocs.planarMap, &unit, SHADER_UNIFORM_INT);
        ::rlActiveTextureSlot(unit);
        ::rlEnableTexture(m_planar->Current().colour);
        ::rlActiveTextureSlot(0);
        ::SetShaderValueMatrix(*shader, m_pbrLocs.planarMatrix, m_planar->viewProjection);
    }

    const int useIBL = pass.Environment.IrradianceMap != BRITE::NullTextureHandle ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.useIBL, &useIBL, SHADER_UNIFORM_INT);
    // The environment map, irradiance and prefilter, as 2D textures on units of their own (IrradianceTextureUnit).
    m_environmentThisPass = false;
    if (useIBL == 1) {
        auto texture = [&](BRITE::TextureHandle handle) -> unsigned int {
            auto it = m_textures.find(handle);
            if (handle == BRITE::NullTextureHandle || it == m_textures.end() || it->second.isRenderTexture)
                return 0;
            return static_cast<const ::Texture2D*>(it->second.ptr)->id;
        };
        const unsigned int irradiance = texture(pass.Environment.IrradianceMap);
        const unsigned int prefilter = texture(pass.Environment.PrefilterMap);
        const std::pair<unsigned int, int> bindings[2] = {{irradiance, IrradianceTextureUnit},
                                                          {prefilter, PrefilterTextureUnit}};
        const int locations[2] = {shader->locs[SHADER_LOC_MAP_IRRADIANCE], shader->locs[SHADER_LOC_MAP_PREFILTER]};
        for (int k = 0; k < 2; ++k) {
            const int unit = bindings[k].second;
            ::SetShaderValue(*shader, locations[k], &unit, SHADER_UNIFORM_INT);
            ::rlActiveTextureSlot(unit);
            if (bindings[k].first != 0)
                ::rlEnableTexture(bindings[k].first);
            else
                ::rlDisableTexture();
        }
        ::rlActiveTextureSlot(0);
        m_environmentThisPass = true;
    }

    if (pass.Camera3DPtr) {
        const float cameraPos[3] = {pass.Camera3DPtr->position.x, pass.Camera3DPtr->position.y,
                                    pass.Camera3DPtr->position.z};
        ::SetShaderValue(*shader, m_pbrLocs.viewPos, cameraPos, SHADER_UNIFORM_VEC3);
    }
}

bool RaylibRenderBackend::ApplyMaterial(const BRITE::PBRMaterial& material, const OwnMaps& ownMaps,
                                        const BRITE::GltfMaterialInfo* fileMaterial) {
    ::Shader* shader = static_cast<::Shader*>(m_shaders[m_pbrShader]);

    // Which maps are bound. The shader used to assume all of them, so a model
    // with no albedo texture sampled whatever was on texture unit 0 -- black on
    // the first draw -- instead of its colour. An albedo texture is the draw
    // command's or, failing that, the one the model's own material brought. So
    // is each of the metalness, roughness, occlusion and normal maps.
    const bool metalMap = material.MetallicMap != BRITE::NullTextureHandle || ownMaps.metalness;
    const bool roughMap = material.RoughnessMap != BRITE::NullTextureHandle || ownMaps.roughness;
    const bool occlusionMap = material.AOMap != BRITE::NullTextureHandle || ownMaps.occlusion;
    const int useAlbedo = (material.AlbedoMap != BRITE::NullTextureHandle || ownMaps.albedo) ? 1 : 0;
    const int useNormal = (material.NormalMap != BRITE::NullTextureHandle || ownMaps.normal) ? 1 : 0;
    const int useMetallic = metalMap ? 1 : 0;
    const int useRoughness = roughMap ? 1 : 0;
    const int useOcclusion = occlusionMap ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.useTexAlbedo, &useAlbedo, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexNormal, &useNormal, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexMetallic, &useMetallic, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexRoughness, &useRoughness, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexOcclusion, &useOcclusion, SHADER_UNIFORM_INT);

    // The factor each map multiplies, as glTF defines them. A file's own where
    // it wrote one. Where it did not: 1 under a map -- glTF's own default,
    // which means "as the map says" -- and the draw's scalar where there is no
    // map, never glTF's unwritten 1 alone, which is fully metallic and draws
    // dark under anything but an environment map. The draw's scalars are the
    // no-map case and nothing else, as PBRMaterial says.
    auto factor = [](bool written, float fileValue, bool mapped, float drawValue) {
        return written ? fileValue : mapped ? 1.0f : drawValue;
    };
    const bool hasFile = fileMaterial != nullptr;
    const float metallic = factor(hasFile && fileMaterial->HasMetallicFactor,
                                  hasFile ? fileMaterial->MetallicFactor : 0.0f, metalMap, material.Metallic);
    const float roughness = factor(hasFile && fileMaterial->HasRoughnessFactor,
                                   hasFile ? fileMaterial->RoughnessFactor : 0.0f, roughMap, material.Roughness);
    ::SetShaderValue(*shader, m_pbrLocs.metallicValue, &metallic, SHADER_UNIFORM_FLOAT);
    ::SetShaderValue(*shader, m_pbrLocs.roughnessValue, &roughness, SHADER_UNIFORM_FLOAT);
    // The file's occlusion strength for its own map; the whole map for a draw's.
    const float occlusionStrength =
        (material.AOMap == BRITE::NullTextureHandle && hasFile) ? fileMaterial->OcclusionStrength : 1.0f;
    ::SetShaderValue(*shader, m_pbrLocs.occlusionStrength, &occlusionStrength, SHADER_UNIFORM_FLOAT);
    // The file's normal scale for its own map; the map as authored for a draw's.
    const float normalScale =
        (material.NormalMap == BRITE::NullTextureHandle && hasFile) ? fileMaterial->NormalScale : 1.0f;
    ::SetShaderValue(*shader, m_pbrLocs.normalScale, &normalScale, SHADER_UNIFORM_FLOAT);

    // Set on every mesh, lit or not: a uniform keeps its value between draws,
    // so an unlit mesh would otherwise leave every mesh after it unlit.
    // The draw's Unlit, or the file's KHR_materials_unlit.
    const int unlit = (material.Unlit || (fileMaterial != nullptr && fileMaterial->Unlit)) ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.unlit, &unlit, SHADER_UNIFORM_INT);

    // The cut-out: the draw's own if it asks for one, else the file's MASK.
    // BLEND is the file's own: the texture's alpha counts, and the mesh is drawn
    // in the translucent stage (SubmitRenderPass), sorted, blended and writing no
    // depth. A cut-out, the draw's or the file's, wins over BLEND.
    const bool fileMasks = fileMaterial != nullptr && fileMaterial->AlphaMode == BRITE::GltfAlphaMode::Mask;
    const int alphaBlend =
        (!material.AlphaMask && fileMaterial != nullptr && fileMaterial->AlphaMode == BRITE::GltfAlphaMode::Blend) ? 1
                                                                                                                   : 0;
    ::SetShaderValue(*shader, m_pbrLocs.alphaBlend, &alphaBlend, SHADER_UNIFORM_INT);
    const int alphaMask = (material.AlphaMask || fileMasks) ? 1 : 0;
    const float alphaCutoff = material.AlphaMask ? material.AlphaCutoff : fileMasks ? fileMaterial->AlphaCutoff : 0.0f;
    ::SetShaderValue(*shader, m_pbrLocs.alphaMask, &alphaMask, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.alphaCutoff, &alphaCutoff, SHADER_UNIFORM_FLOAT);

    const bool bothSides = material.DoubleSided || (fileMaterial != nullptr && fileMaterial->DoubleSided);
    const int doubleSided = bothSides ? 1 : 0;
    ::SetShaderValue(*shader, m_pbrLocs.doubleSided, &doubleSided, SHADER_UNIFORM_INT);

    // Emission: the draw's if it names any, else the file's, else none; set on
    // every mesh, so one mesh's glow is never left on the next. The draw's
    // colour is sRGB, as the tint is, and is linearised; a file's factor is
    // linear already, as glTF defines it. The strength multiplies either.
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    int useEmissive = 0;
    const bool drawEmits = material.Emission.r != 0 || material.Emission.g != 0 || material.Emission.b != 0 ||
                           material.EmissionMap != BRITE::NullTextureHandle;
    if (drawEmits) {
        const unsigned char channels[3] = {material.Emission.r, material.Emission.g, material.Emission.b};
        for (int c = 0; c < 3; ++c)
            emissive[c] = std::pow(channels[c] / 255.0f, 2.2f) * material.EmissionStrength;
        useEmissive = material.EmissionMap != BRITE::NullTextureHandle ? 1 : 0;
    } else if (fileMaterial != nullptr) {
        for (int c = 0; c < 3; ++c)
            emissive[c] = fileMaterial->EmissiveFactor[c] * fileMaterial->EmissiveStrength;
        useEmissive = ownMaps.emission ? 1 : 0;
    }
    ::SetShaderValue(*shader, m_pbrLocs.emissiveLight, emissive, SHADER_UNIFORM_VEC3);

    // What the surface reflects: a planar reflection only where the pass drew one, else the sky.
    int reflection = 0;
    if (material.Reflection == BRITE::ReflectionMode::Sky)
        reflection = 1;
    else if (material.Reflection == BRITE::ReflectionMode::Planar)
        reflection = m_planarThisPass ? 2 : 1;
    ::SetShaderValue(*shader, m_pbrLocs.reflection, &reflection, SHADER_UNIFORM_INT);
    ::SetShaderValue(*shader, m_pbrLocs.reflectionF0, &material.ReflectionF0, SHADER_UNIFORM_FLOAT);
    ::SetShaderValue(*shader, m_pbrLocs.reflectionDistortion, &material.ReflectionDistortion, SHADER_UNIFORM_FLOAT);
    ::SetShaderValue(*shader, m_pbrLocs.useTexEmissive, &useEmissive, SHADER_UNIFORM_INT);
    return bothSides;
}

const BRITE::GltfMaterialInfo* RaylibRenderBackend::FileMaterial(BRITE::ModelHandle model, int slot) const {
    auto it = m_fileMaterials.find(model);
    if (it == m_fileMaterials.end() || slot <= 0 || static_cast<std::size_t>(slot) >= it->second.size())
        return nullptr;
    return &it->second[static_cast<std::size_t>(slot)];
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

BRITE::TextureHandle RaylibRenderBackend::LoadRenderTexture(int width, int height) {
    RenderTexture2D* rt = new RenderTexture2D(::LoadRenderTexture(width, height));
    BRITE::TextureHandle handle = m_nextId++;
    m_textures[handle] = {true, rt};
    return handle;
}

BRITE::TextureHandle RaylibRenderBackend::LoadMultisampledRenderTexture(int width, int height, int samples) {
    // The ordinary render texture every reader reads, and the pass resolves into.
    const BRITE::TextureHandle handle = LoadRenderTexture(width, height);
    if (samples <= 1)
        return handle;

    int most = 0;
    glad_glGetIntegerv(GL_MAX_SAMPLES_, &most);
    const int count = std::min(samples, most);
    if (count <= 1) {
        spdlog::warn("BRITE: this device cannot multisample a render texture; {} samples asked, an ordinary one "
                     "given",
                     samples);
        return handle;
    }

    MultisampledTarget target;
    target.samples = count;
    glad_glGenRenderbuffers(1, &target.colour);
    glad_glBindRenderbuffer(GL_RENDERBUFFER_, target.colour);
    glad_glRenderbufferStorageMultisample(GL_RENDERBUFFER_, count, GL_RGBA8_, width, height);
    glad_glGenRenderbuffers(1, &target.depth);
    glad_glBindRenderbuffer(GL_RENDERBUFFER_, target.depth);
    glad_glRenderbufferStorageMultisample(GL_RENDERBUFFER_, count, GL_DEPTH_COMPONENT24_, width, height);
    glad_glBindRenderbuffer(GL_RENDERBUFFER_, 0);

    target.framebuffer = ::rlLoadFramebuffer();
    ::rlFramebufferAttach(target.framebuffer, target.colour, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_RENDERBUFFER,
                          0);
    ::rlFramebufferAttach(target.framebuffer, target.depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    if (!::rlFramebufferComplete(target.framebuffer)) {
        spdlog::warn("BRITE: a {}-sample render texture could not be made; an ordinary one given", count);
        glad_glDeleteRenderbuffers(1, &target.colour);
        ::rlUnloadFramebuffer(target.framebuffer); // and the depth renderbuffer attached to it
        return handle;
    }
    m_multisampled[handle] = target;
    return handle;
}

BRITE::TextureHandle RaylibRenderBackend::LoadRenderTarget(const RenderTargetDescription& description) {
    auto target = std::make_shared<FormattedTarget>();
    std::string why;
    if (!CreateTarget(description, target->objects, why)) {
        spdlog::error("BRITE: a {}x{} render target could not be made: {}", description.Width, description.Height, why);
        return BRITE::NullTextureHandle;
    }
    const TargetObjects& o = target->objects;
    RenderTexture2D* rt = new RenderTexture2D{};
    rt->id = o.framebuffer;
    rt->texture.id = o.colour;
    rt->texture.width = o.width;
    rt->texture.height = o.height;
    rt->texture.mipmaps = 1;
    // raylib has no format for R11G11B10F; nothing of raylib's reads this target's pixels (ReadRenderTexture reads
    // a float target itself), so the nearest is named.
    rt->texture.format = o.format == RenderTargetFormat::RGBA8 ? PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                                                               : PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    rt->depth.id = o.depthTexture != 0 ? o.depthTexture : o.depthRenderbuffer;
    rt->depth.width = o.width;
    rt->depth.height = o.height;
    const BRITE::TextureHandle handle = m_nextId++;
    m_textures[handle] = {true, rt};
    if (o.msFramebuffer != 0) {
        MultisampledTarget surface;
        surface.framebuffer = o.msFramebuffer;
        surface.colour = o.msColour;
        surface.depth = o.msDepth;
        surface.samples = o.samples;
        m_multisampled[handle] = surface;
    }
    if (o.depthTexture != 0) {
        ::Texture2D* depth = new ::Texture2D{};
        depth->id = o.depthTexture;
        depth->width = o.width;
        depth->height = o.height;
        depth->mipmaps = 1;
        depth->format = PIXELFORMAT_UNCOMPRESSED_R32; // one channel, as a sampler reads a depth texture
        target->depth = m_nextId++;
        m_textures[target->depth] = {false, depth};
    }
    m_formattedTargets[handle] = std::move(target);
    return handle;
}

BRITE::TextureHandle RaylibRenderBackend::RenderTargetDepth(BRITE::TextureHandle target) {
    auto it = m_formattedTargets.find(target);
    return it != m_formattedTargets.end() ? it->second->depth : BRITE::NullTextureHandle;
}

bool RaylibRenderBackend::ReadRenderTargetLinear(BRITE::TextureHandle target, int& width, int& height,
                                                 std::vector<float>& rgba) {
    auto it = m_formattedTargets.find(target);
    if (it == m_formattedTargets.end())
        return false;
    const TargetObjects& o = it->second->objects;
    if (!ReadTargetFloats(o, rgba))
        return false;
    width = o.width;
    height = o.height;
    return true;
}

bool RaylibRenderBackend::ReadLastHdrScene(int& width, int& height, std::vector<float>& rgba) const {
    if (!m_post || m_post->Scene().colour == 0)
        return false;
    width = m_post->Scene().width;
    height = m_post->Scene().height;
    return ReadTargetFloats(m_post->Scene(), rgba);
}

int RaylibRenderBackend::HdrSceneTargetsMade() const {
    return m_post ? m_post->SceneTargetsMade() : 0;
}

int RaylibRenderBackend::HdrTargetSetsHeld() const {
    return m_post ? m_post->SetsHeld() : 0;
}

std::uint64_t RaylibRenderBackend::HdrIdlePasses() {
    return PostChain::IdlePasses;
}

bool RaylibRenderBackend::ReadLastBloom(int& width, int& height, std::vector<float>& rgba) const {
    return m_post && m_post->ReadBloomFloats(rgba, width, height);
}

bool RaylibRenderBackend::EnsureAuthoredColourShader() {
    if (m_authoredColourShader != BRITE::NullShaderHandle)
        return m_authoredColourWorks;
    m_authoredColourShader =
        LoadShaderFromMemory(EmbeddedShaders::AUTHORED_COLOUR_VERTEX, EmbeddedShaders::AUTHORED_COLOUR_FRAGMENT);
    ::Shader* shader = static_cast<::Shader*>(m_shaders[m_authoredColourShader]);
    // A refused shader is no program, or raylib's default one (ShaderRefused); raylib's log says why.
    m_authoredColourWorks = !ShaderRefused(*shader);
    if (!m_authoredColourWorks)
        spdlog::error(
            "BRITE: the authored-colour shader failed to compile; every HDR pass is drawn as if Hdr were off");
    m_authoredColourExposure = ::GetShaderLocation(*shader, "exposure");
    return m_authoredColourWorks;
}

int RaylibRenderBackend::RenderTextureSamples(BRITE::TextureHandle target) const {
    auto it = m_textures.find(target);
    if (it == m_textures.end() || !it->second.isRenderTexture)
        return 0;
    auto multisampled = m_multisampled.find(target);
    return multisampled != m_multisampled.end() ? multisampled->second.samples : 1;
}

void RaylibRenderBackend::UnloadRenderTexture(BRITE::TextureHandle target) {
    auto formatted = m_formattedTargets.find(target);
    if (formatted != m_formattedTargets.end()) {
        // Its objects are its own: freed here, and the raylib structs that named them dropped without raylib's
        // unload, which would free them a second time.
        DestroyTarget(formatted->second->objects);
        if (formatted->second->depth != BRITE::NullTextureHandle) {
            delete static_cast<::Texture2D*>(m_textures[formatted->second->depth].ptr);
            m_textures.erase(formatted->second->depth);
        }
        delete static_cast<RenderTexture2D*>(m_textures[target].ptr);
        m_textures.erase(target);
        m_multisampled.erase(target);
        m_formattedTargets.erase(formatted);
        return;
    }
    auto multisampled = m_multisampled.find(target);
    if (multisampled != m_multisampled.end()) {
        glad_glDeleteRenderbuffers(1, &multisampled->second.colour);
        ::rlUnloadFramebuffer(multisampled->second.framebuffer); // and the depth renderbuffer attached to it
        m_multisampled.erase(multisampled);
    }
    auto it = m_textures.find(target);
    if (it != m_textures.end() && it->second.isRenderTexture) {
        RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
        ::UnloadRenderTexture(*rt);
        delete rt;
        m_textures.erase(it);
    }
}

bool RaylibRenderBackend::ReadRenderTexture(BRITE::TextureHandle target, int& width, int& height,
                                            std::vector<BRITE::Color>& pixels) {
    auto it = m_textures.find(target);
    if (it == m_textures.end() || !it->second.isRenderTexture)
        return false;

    // A float target is read as floats and held to 0..1, as an 8-bit target would hold it.
    auto formatted = m_formattedTargets.find(target);
    if (formatted != m_formattedTargets.end() && formatted->second->objects.format != RenderTargetFormat::RGBA8) {
        std::vector<float> rgba;
        if (!ReadTargetFloats(formatted->second->objects, rgba))
            return false;
        width = formatted->second->objects.width;
        height = formatted->second->objects.height;
        pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        auto byte = [](float v) { return static_cast<unsigned char>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        const bool hasAlpha = formatted->second->objects.format == RenderTargetFormat::RGBA16F;
        for (int row = 0; row < height; ++row) {
            // OpenGL holds the bottom row first; the caller is promised the top row first.
            const float* source =
                &rgba[static_cast<std::size_t>(height - 1 - row) * static_cast<std::size_t>(width) * 4];
            for (int x = 0; x < width; ++x) {
                BRITE::Color& c = pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(x)];
                c = {byte(source[x * 4 + 0]), byte(source[x * 4 + 1]), byte(source[x * 4 + 2]),
                     hasAlpha ? byte(source[x * 4 + 3]) : static_cast<unsigned char>(255)};
            }
        }
        return true;
    }

    RenderTexture2D* rt = static_cast<RenderTexture2D*>(it->second.ptr);
    ::Image image = ::LoadImageFromTexture(rt->texture);
    if (image.data == nullptr)
        return false;

    ::ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    // A render texture is stored bottom row first (OpenGL's origin); the caller
    // is promised top row first.
    ::ImageFlipVertical(&image);

    width = image.width;
    height = image.height;
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    static_assert(sizeof(BRITE::Color) == 4, "BRITE::Color must be four bytes to copy an RGBA8 image into it");
    std::memcpy(pixels.data(), image.data, pixels.size() * sizeof(BRITE::Color));

    ::UnloadImage(image);
    return true;
}

BRITE::TextureHandle RaylibRenderBackend::LoadTexture(const char* fileName) {
    ::Texture2D* tex = new ::Texture2D(::LoadTexture(fileName));
    BRITE::TextureHandle handle = m_nextId++;
    m_textures[handle] = {false, tex};
    return handle;
}

void RaylibRenderBackend::UnloadTexture(BRITE::TextureHandle texture) {
    // A render target's readable depth goes with its target (RenderTargetDepth), never alone.
    for (const auto& [handle, target] : m_formattedTargets)
        if (target->depth == texture)
            return;
    auto it = m_textures.find(texture);
    if (it != m_textures.end() && !it->second.isRenderTexture) {
        ::Texture2D* tex = static_cast<::Texture2D*>(it->second.ptr);
        ::UnloadTexture(*tex);
        delete tex;
        m_textures.erase(it);
    }
}

bool RaylibRenderBackend::SetTextureSampling(BRITE::TextureHandle texture, const TextureSampling& sampling) {
    auto it = m_textures.find(texture);
    if (it == m_textures.end() || it->second.isRenderTexture)
        return false;
    ApplySampling(*static_cast<::Texture2D*>(it->second.ptr), sampling);
    return true;
}

bool RaylibRenderBackend::SetModelTextureSampling(BRITE::ModelHandle model, const TextureSampling& sampling) {
    auto it = m_models.find(model);
    if (it == m_models.end())
        return false;
    ::Model* rlModel = static_cast<::Model*>(it->second);

    // The textures the model owns, each once -- the shared placeholder is not
    // the model's to change. A texture bound in several maps is sampled once,
    // and every map holding it learns the mipmap count that produced.
    std::vector<unsigned int> bound;
    for (int m = 0; m < rlModel->materialCount; ++m)
        for (int map = 0; map < MATERIAL_MAPS_PER_MATERIAL; ++map)
            bound.push_back(rlModel->materials[m].maps[map].texture.id);
    for (const unsigned int id : TexturesOwnedByModel(bound, ::rlGetTextureIdDefault())) {
        ::Texture2D* first = nullptr;
        for (int m = 0; m < rlModel->materialCount; ++m) {
            for (int map = 0; map < MATERIAL_MAPS_PER_MATERIAL; ++map) {
                ::Texture2D& held = rlModel->materials[m].maps[map].texture;
                if (held.id != id)
                    continue;
                if (first == nullptr) {
                    first = &held;
                    ApplySampling(held, sampling);
                } else {
                    held.mipmaps = first->mipmaps;
                }
            }
        }
    }
    return true;
}

unsigned int RaylibRenderBackend::NativeTextureId(BRITE::TextureHandle texture) const {
    auto it = m_textures.find(texture);
    if (it == m_textures.end() || it->second.isRenderTexture)
        return 0;
    return static_cast<const ::Texture2D*>(it->second.ptr)->id;
}

BRITE::EnvironmentMap RaylibRenderBackend::LoadEnvironmentMap(const char* hdrFileName) {
    BRITE::EnvironmentMap env;

    // Load HDR Panorama as standard Texture2D
    ::Texture2D panorama = ::LoadTexture(hdrFileName);

    // Generate Mipmaps so that the shader can sample varying roughness
    ::GenTextureMipmaps(&panorama);

    // Set texture filter to trilinear for smooth mipmap interpolation
    ::SetTextureFilter(panorama, TEXTURE_FILTER_TRILINEAR);

    ::Texture2D* envTex = new ::Texture2D(panorama);
    BRITE::TextureHandle handle = m_nextId++;
    m_textures[handle] = {false, envTex};

    // Since we updated pbr.fs to sample spherically from a sampler2D with mipmaps,
    // we use the same texture handle for everything!
    env.Cubemap = handle;
    env.IrradianceMap = handle;
    env.PrefilterMap = handle;

    return env;
}

void RaylibRenderBackend::UnloadEnvironmentMap(BRITE::EnvironmentMap envMap) {
    UnloadTexture(envMap.Cubemap);
}

BRITE::ModelHandle RaylibRenderBackend::LoadModel(const char* fileName) {
    ::Model* model = new ::Model(::LoadModel(fileName));
    BRITE::ModelHandle handle = m_nextModelId++;
    m_models[handle] = model;
    for (int m = 0; m < model->meshCount; ++m)
        GenerateTangents(model->meshes[m]);

    // A skin: its bones' rest transforms relative to their parents, from the bind pose raylib keeps placed in the
    // model, and the clips of the same file. A model with no skin is loaded exactly as before.
    if (model->skeleton.boneCount > 0 && model->skeleton.bones != nullptr && model->skeleton.bindPose != nullptr &&
        model->boneMatrices != nullptr) {
        auto skin = std::make_shared<Skin>();
        const int boneCount = static_cast<int>(model->skeleton.boneCount);
        skin->rest.resize(static_cast<std::size_t>(boneCount));
        for (int i = 0; i < boneCount; ++i) {
            const int parent = model->skeleton.bones[i].parent;
            const ::Transform& placed = model->skeleton.bindPose[i];
            skin->rest[static_cast<std::size_t>(i)] =
                parent >= 0 && parent < boneCount ? RelativeTo(model->skeleton.bindPose[parent], placed) : placed;
        }
        m_skins[handle] = std::move(skin);
        AddClips(handle, fileName); // none is not a failure: a body posed from code needs no clip
    }

    // raylib's glTF loader reads a material's base colour and textures and
    // nothing else, so the rest is read from the file here: the file's material
    // i is raylib's slot i + 1, and slot 0 is raylib's own default. Read the
    // same way raylib reads the file, through LoadFileData below.
    if (::IsFileExtension(fileName, ".gltf") || ::IsFileExtension(fileName, ".glb")) {
        // glTF's vertex colours are linear, as its base colours are, and raylib
        // stored each as value x 255 -- as if it were an sRGB byte, which the
        // shader would linearise a second time. Re-encode each as the draw colour
        // it means, as the base colours are below, and re-upload the buffer.
        // (The buffer is indexed by raylib's attribute location for colour, 3,
        // not by the shader-location enum's 5.)
        for (int m = 0; m < model->meshCount; ++m) {
            ::Mesh& mesh = model->meshes[m];
            if (mesh.colors == nullptr)
                continue;
            for (int v = 0; v < mesh.vertexCount; ++v) {
                unsigned char* c = &mesh.colors[v * 4];
                const float linear[4] = {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
                const BRITE::Color drawn = BRITE::DrawColorFromLinear(linear);
                c[0] = drawn.r;
                c[1] = drawn.g;
                c[2] = drawn.b;
                c[3] = drawn.a;
            }
            if (mesh.vboId != nullptr && mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR] != 0)
                ::rlUpdateVertexBuffer(mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR], mesh.colors,
                                       mesh.vertexCount * 4, 0);
        }

        // Read through raylib's own file-loading hook rather than a second,
        // independent OS file handle: a caller who sandboxes raylib's reads
        // (SetLoadFileDataCallback) then sandboxes this re-read too, instead
        // of it quietly reaching the real filesystem on its own.
        std::vector<unsigned char> bytes;
        int fileSize = 0;
        if (unsigned char* fileData = ::LoadFileData(fileName, &fileSize)) {
            bytes.assign(fileData, fileData + fileSize);
            ::UnloadFileData(fileData);
        }
        std::vector<BRITE::GltfMaterialInfo> materials;
        if (!BRITE::ReadGltfMaterials(bytes, materials)) {
            spdlog::warn("BRITE: {}: its materials could not be read; drawn with their base colours alone", fileName);
        } else if (static_cast<int>(materials.size()) + 1 != model->materialCount) {
            spdlog::warn("BRITE: {}: {} materials in the file against {} loaded; drawn with their base colours alone",
                         fileName, materials.size(), model->materialCount - 1);
        } else {
            materials.insert(materials.begin(), BRITE::GltfMaterialInfo{});
            // raylib stored each base colour factor as factor x 255, as if the
            // linear factor were an sRGB byte, and the shader would linearise it
            // a second time. Replace it with the draw colour the factor means,
            // once, here -- so a drawn model and its read-back meshes agree.
            for (int slot = 1; slot < model->materialCount; ++slot) {
                const BRITE::Color colour =
                    BRITE::DrawColorFromLinear(materials[static_cast<std::size_t>(slot)].BaseColorFactor);
                model->materials[slot].maps[MATERIAL_MAP_ALBEDO].color = {colour.r, colour.g, colour.b, colour.a};
            }
            m_fileMaterials[handle] = std::move(materials);
        }
    }
    return handle;
}

BRITE::ModelHandle RaylibRenderBackend::LoadModelFromMesh(const BRITE::MeshData& data) {
    // The refusals live in CheckMeshData, a pure function with its own tests;
    // this only reports them. Everything below it may assume a well-formed mesh.
    const BRITE::MeshDataProblem problem = BRITE::CheckMeshData(data, MaxVerticesPerMesh);
    if (problem != BRITE::MeshDataProblem::None) {
        spdlog::error("BRITE: LoadModelFromMesh refused a mesh because {} ({} positions, {} normals, {} colours, "
                      "{} texture coordinates, {} indices; at most {} vertices per mesh)",
                      BRITE::Describe(problem), data.Positions.size(), data.Normals.size(), data.Colors.size(),
                      data.TexCoords.size(), data.Indices.size(), MaxVerticesPerMesh);
        return BRITE::NullModelHandle;
    }
    const std::size_t vertexCount = data.Positions.size();

    // raylib owns these arrays from here: UnloadModel frees them with RL_FREE, so
    // they are allocated with its allocator rather than a std::vector's.
    ::Mesh mesh = {0};
    mesh.vertexCount = static_cast<int>(vertexCount);
    mesh.triangleCount = static_cast<int>(data.Indices.size() / 3);
    mesh.vertices = static_cast<float*>(RL_MALLOC(vertexCount * 3 * sizeof(float)));
    mesh.normals = static_cast<float*>(RL_MALLOC(vertexCount * 3 * sizeof(float)));
    mesh.indices = static_cast<unsigned short*>(RL_MALLOC(data.Indices.size() * sizeof(unsigned short)));
    for (std::size_t i = 0; i < vertexCount; ++i) {
        mesh.vertices[i * 3 + 0] = data.Positions[i].x;
        mesh.vertices[i * 3 + 1] = data.Positions[i].y;
        mesh.vertices[i * 3 + 2] = data.Positions[i].z;
        mesh.normals[i * 3 + 0] = data.Normals[i].x;
        mesh.normals[i * 3 + 1] = data.Normals[i].y;
        mesh.normals[i * 3 + 2] = data.Normals[i].z;
    }
    if (!data.Colors.empty()) {
        mesh.colors = static_cast<unsigned char*>(RL_MALLOC(vertexCount * 4));
        for (std::size_t i = 0; i < vertexCount; ++i) {
            mesh.colors[i * 4 + 0] = data.Colors[i].r;
            mesh.colors[i * 4 + 1] = data.Colors[i].g;
            mesh.colors[i * 4 + 2] = data.Colors[i].b;
            mesh.colors[i * 4 + 3] = data.Colors[i].a;
        }
    }
    if (!data.TexCoords.empty()) {
        mesh.texcoords = static_cast<float*>(RL_MALLOC(vertexCount * 2 * sizeof(float)));
        for (std::size_t i = 0; i < vertexCount; ++i) {
            mesh.texcoords[i * 2 + 0] = data.TexCoords[i].x;
            mesh.texcoords[i * 2 + 1] = data.TexCoords[i].y;
        }
    }
    for (std::size_t i = 0; i < data.Indices.size(); ++i) {
        mesh.indices[i] = static_cast<unsigned short>(data.Indices[i]);
    }

    ::UploadMesh(&mesh, false);
    ::Model* model = new ::Model(::LoadModelFromMesh(mesh));
    GenerateTangents(model->meshes[0]);
    BRITE::ModelHandle handle = m_nextModelId++;
    m_models[handle] = model;
    return handle;
}

std::vector<unsigned int> RaylibRenderBackend::TexturesOwnedByModel(const std::vector<unsigned int>& boundTextureIds,
                                                                    unsigned int placeholderTextureId) {
    std::vector<unsigned int> owned;
    for (const unsigned int id : boundTextureIds) {
        if (id == 0 || id == placeholderTextureId)
            continue;
        if (std::find(owned.begin(), owned.end(), id) == owned.end())
            owned.push_back(id);
    }
    return owned;
}

void RaylibRenderBackend::UnloadModel(BRITE::ModelHandle model) {
    m_meshSpheres.erase(model);
    auto it = m_models.find(model);
    if (it != m_models.end()) {
        ::Model* rlModel = static_cast<::Model*>(it->second);
        // raylib's UnloadModel frees a model's meshes but leaves its textures to
        // the caller. The textures a loader created for the model's materials are
        // the model's alone -- a draw command's textures are never written into
        // it -- so they go with it.
        std::vector<unsigned int> bound;
        for (int m = 0; m < rlModel->materialCount; ++m) {
            for (int map = 0; map < MATERIAL_MAPS_PER_MATERIAL; ++map)
                bound.push_back(rlModel->materials[m].maps[map].texture.id);
        }
        for (const unsigned int id : TexturesOwnedByModel(bound, ::rlGetTextureIdDefault()))
            ::rlUnloadTexture(id);
        ::UnloadModel(*rlModel);
        delete rlModel;
        m_models.erase(it);
        m_fileMaterials.erase(model);
        m_meshCentres.erase(model);
        // Its clips go with it.
        if (auto skinIt = m_skins.find(model); skinIt != m_skins.end()) {
            for (const auto& [clips, count] : skinIt->second->loaded)
                ::UnloadModelAnimations(clips, count);
            m_skins.erase(skinIt);
        }
    }
}

unsigned char RaylibRenderBackend::ModulateChannel(unsigned char a, unsigned char b) {
    return static_cast<unsigned char>((static_cast<int>(a) * static_cast<int>(b) + 127) / 255);
}

bool RaylibRenderBackend::ReadModelMeshes(BRITE::ModelHandle model, std::vector<BRITE::MeshData>& meshes) {
    meshes.clear();
    auto it = m_models.find(model);
    if (it == m_models.end())
        return false;
    const ::Model* rlModel = static_cast<const ::Model*>(it->second);

    // raylib keeps each mesh's arrays on the CPU after uploading them, and its
    // file loaders have already applied every node's transform to them. Both
    // LoadModel and LoadModelFromMesh set the model's own transform to the
    // identity, and nothing in this backend changes it, so the arrays are in
    // the model's space as they stand. A skinned model's are its rest geometry,
    // whatever pose its last draw left in its vertex buffers.
    meshes.resize(static_cast<std::size_t>(rlModel->meshCount));
    for (int m = 0; m < rlModel->meshCount; ++m)
        ReadMesh(*rlModel, m, false, meshes[static_cast<std::size_t>(m)]);
    return true;
}

BRITE::ShaderHandle RaylibRenderBackend::LoadShader(const char* vsFileName, const char* fsFileName) {
    ::Shader* shader = new ::Shader(::LoadShader(vsFileName, fsFileName));
    BRITE::ShaderHandle handle = m_nextShaderId++;
    m_shaders[handle] = shader;
    return handle;
}

BRITE::ShaderHandle RaylibRenderBackend::LoadShaderFromMemory(const char* vsCode, const char* fsCode) {
    ::Shader* shader = new ::Shader(::LoadShaderFromMemory(vsCode, fsCode));
    BRITE::ShaderHandle handle = m_nextShaderId++;
    m_shaders[handle] = shader;
    return handle;
}

void RaylibRenderBackend::UnloadShader(BRITE::ShaderHandle shader) {
    auto it = m_shaders.find(shader);
    if (it != m_shaders.end()) {
        ::Shader* rlShader = static_cast<::Shader*>(it->second);
        ::UnloadShader(*rlShader);
        delete rlShader;
        m_shaders.erase(it);
    }
}

int RaylibRenderBackend::GetShaderLocation(BRITE::ShaderHandle shader, const char* uniformName) {
    auto it = m_shaders.find(shader);
    if (it != m_shaders.end()) {
        ::Shader* rlShader = static_cast<::Shader*>(it->second);
        return ::GetShaderLocation(*rlShader, uniformName);
    }
    return -1;
}

void RaylibRenderBackend::SetShaderValue(BRITE::ShaderHandle shader, int locIndex, const void* value,
                                         ShaderUniformDataType uniformType) {
    auto it = m_shaders.find(shader);
    if (it != m_shaders.end()) {
        ::Shader* rlShader = static_cast<::Shader*>(it->second);

        int rlUniformType = 0;
        switch (uniformType) {
        case ShaderUniformDataType::Float:
            rlUniformType = SHADER_UNIFORM_FLOAT;
            break;
        case ShaderUniformDataType::Vec2:
            rlUniformType = SHADER_UNIFORM_VEC2;
            break;
        case ShaderUniformDataType::Vec3:
            rlUniformType = SHADER_UNIFORM_VEC3;
            break;
        case ShaderUniformDataType::Vec4:
            rlUniformType = SHADER_UNIFORM_VEC4;
            break;
        case ShaderUniformDataType::Int:
            rlUniformType = SHADER_UNIFORM_INT;
            break;
        case ShaderUniformDataType::IVec2:
            rlUniformType = SHADER_UNIFORM_IVEC2;
            break;
        case ShaderUniformDataType::IVec3:
            rlUniformType = SHADER_UNIFORM_IVEC3;
            break;
        case ShaderUniformDataType::IVec4:
            rlUniformType = SHADER_UNIFORM_IVEC4;
            break;
        case ShaderUniformDataType::Sampler2D:
            rlUniformType = SHADER_UNIFORM_SAMPLER2D;
            break;
        }

        ::SetShaderValue(*rlShader, locIndex, value, rlUniformType);
    }
}

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
