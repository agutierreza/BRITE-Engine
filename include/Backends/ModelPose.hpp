#pragma once

#include "Math/BriteMath.hpp"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BRITE {

// SKELETAL ANIMATION -- a skinned model's bones, its clips, and the pose a draw puts it in.
//
// A model loaded from a file that carries a skin keeps its skeleton: its bones, each with a parent and a rest
// transform, and every vertex bound to up to four bones by weight. The clips in the same file load with it (glTF and
// GLB), and IRenderBackend::AddModelClips adds those of another file made for the same skeleton. Unloading the model
// releases its clips.
//
// A draw poses the model with ModelDrawCommand::Pose -- data on the command, applied when the pass is submitted, and
// never kept: the next draw of the same model, in the same pass or the next, is posed by its own command alone. A
// command whose Pose is left at Rest draws the model as it was bound to its bones, exactly as a draw did before poses
// existed; a model with no skin ignores any pose.
//
// TIME IS THE CALLER'S. A clip is sampled at the seconds a command names, never at the wall clock, so a clip can be
// driven by anything: a phase, a cadence, a replay, a slider. A time before the clip's start draws its first pose,
// and one past its end its last: a clip that should loop is looped by its caller, which knows whether it loops.
//
// BONES ARE ADDRESSED BY INDEX, in the order ReadModelSkeleton lists them. An index is what the skinned vertices
// themselves store, so a pose is a dense array the backend reads without a lookup per bone per draw; FindBone turns
// a name into an index once, when the model loads. A name is the file's, cut to 31 bytes by the loader.

// A bone's transform relative to its parent bone -- or to the model, for a bone with no parent: scale, then
// rotation, then translation.
struct BoneTransform {
    Vector3 Translation = {0.0f, 0.0f, 0.0f};
    Quaternion Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    Vector3 Scale = {1.0f, 1.0f, 1.0f};
};

struct SkeletonBone {
    std::string Name;
    int Parent = -1; // the parent bone's index, or -1 for a bone with no parent
    // Where the bone sits when the model is drawn at rest -- the pose its vertices were bound in -- relative to its
    // parent. A pose from code starts from these and changes the bones it moves.
    BoneTransform Rest;
};

struct AnimationClipInfo {
    std::string Name; // the file's, cut to 31 bytes by the loader; may be empty
    // How long the clip runs, in seconds. A loader may sample a clip at a fixed rate (raylib samples glTF at 60 a
    // second), so this is the file's length to the sample below it.
    float Duration = 0.0f;
};

struct ModelSkeleton {
    std::vector<SkeletonBone> Bones;      // empty for a model with no skin
    std::vector<AnimationClipInfo> Clips; // in the order AddModelClips and the model's own file added them
};

// The index of the bone or clip of that name, or -1 when there is none. The first of two of the same name.
inline int FindBone(const ModelSkeleton& skeleton, std::string_view name) {
    for (std::size_t i = 0; i < skeleton.Bones.size(); ++i)
        if (skeleton.Bones[i].Name == name)
            return static_cast<int>(i);
    return -1;
}
inline int FindClip(const ModelSkeleton& skeleton, std::string_view name) {
    for (std::size_t i = 0; i < skeleton.Clips.size(); ++i)
        if (skeleton.Clips[i].Name == name)
            return static_cast<int>(i);
    return -1;
}

enum class PoseKind {
    Rest,  // as the model was bound to its bones: the default, and every draw written before poses existed
    Clip,  // clip A at time A
    Blend, // clip A at time A and clip B at time B, blended by Weight
    Bones, // a pose from code: every bone's transform relative to its parent, in Bones
};

// A clip of the drawn model, by its index in ModelSkeleton::Clips, at a time in seconds from the clip's start.
struct ClipTime {
    int Clip = -1;
    float Seconds = 0.0f;
};

// The pose a draw puts a skinned model in.
//
// Blend: each bone's transform is clip A's moved toward clip B's by Weight -- 0 is clip A alone, 1 is clip B alone,
// and a weight outside 0..1 is held to the nearer end. The two are blended bone by bone where each bone sits in the
// model, translation and scale linearly and rotation along the shorter arc; a blend of two clips of one rhythm keeps
// that rhythm only if their times are kept in step by the caller.
//
// Bones: exactly one transform per bone, in ModelSkeleton's order, each relative to its parent as SkeletonBone::Rest
// is. A pose the model cannot take -- a clip it does not have, or a Bones array of any other length -- draws it at
// rest.
struct ModelPose {
    PoseKind Kind = PoseKind::Rest;
    ClipTime A;
    ClipTime B;
    float Weight = 0.0f;
    std::vector<BoneTransform> Bones;

    static ModelPose FromClip(int clip, float seconds) {
        ModelPose pose;
        pose.Kind = PoseKind::Clip;
        pose.A = {clip, seconds};
        return pose;
    }
    static ModelPose FromBlend(ClipTime a, ClipTime b, float weight) {
        ModelPose pose;
        pose.Kind = PoseKind::Blend;
        pose.A = a;
        pose.B = b;
        pose.Weight = weight;
        return pose;
    }
    static ModelPose FromBones(std::vector<BoneTransform> bones) {
        ModelPose pose;
        pose.Kind = PoseKind::Bones;
        pose.Bones = std::move(bones);
        return pose;
    }
};

} // namespace BRITE
