// Skeletal animation (ModelPose.hpp): a skinned model's bones and clips, and the pose a draw puts it in.
//
// Every case loads a tiny rigged glTF the test writes itself -- two bones and six vertices, every number chosen so the
// posed geometry can be worked by hand -- under a hidden window, and reads the posed geometry back
// (ReadPosedModelMeshes) or draws it and reads the pixels.
//
// THE ARM
//
//   bone 0 "root"  no parent;   rest translation (0, 0.5, 0) -- placed at (0, 0.5, 0)
//   bone 1 "tip"   parent root; rest translation (0, 0.5, 0) -- placed at (0, 0.5, 0) + (0, 0.5, 0) = (0, 1, 0)
//
//   The file's inverse bind matrices are the inverses of those placements, translations by (0, -0.5, 0) and
//   (0, -1, 0), so the six vertices below are drawn where they are written when the bones are at rest. Root's rest
//   is NOT where it is placed only because its parent is the model; tip's rest (0, 0.5, 0) differs from its
//   placement (0, 1, 0), which tells a rest relative to the parent from a placement.
//
//   v0 (-0.5, 0)  v1 (0.5, 0)   root alone, weight 1
//   v2 ( 0.5, 1)  v3 (-0.5, 1)  root 0.5 and tip 0.5
//   v4 ( 0.5, 2)  v5 (-0.5, 2)  tip alone, weight 1
//
//   all at z = 0, normals +z, triangles 0 1 2, 0 2 3 (the lower square) and 3 2 4, 3 4 5 (the upper one).
//
//   A vertex goes wherever each of its bones takes it, weighted: a bone that moves from its rest placement P to a
//   placement Q, with no rotation, takes a vertex v to v - P + Q, a move of Q - P. A vertex of weight 0.5 on each of
//   two bones moves by the average of their two moves.
//
// THE CLIPS, each one channel of two keyframes, at 0 s and at 1 s, linear. raylib samples a glTF clip at 60
// keyframes a second (61 keyframes over 1 s), so each lasts exactly 1 s and t seconds is keyframe 60 t.
//
//   clip 0 "slide"  tip's translation  (0, 0.5, 0) at 0 s  ->  (1, 0.5, 0) at 1 s: the tip moves right 1 a second
//   clip 1 "lift"   root's translation (0, 0.5, 0) at 0 s  ->  (0, 1.5, 0) at 1 s: the whole arm rises 1 a second
//
// A bone a clip does not move keeps its rest. (Two keyframes a channel also keep the clip's last keyframe exact on a
// raylib whose sampler, at exactly the last keyframe's time, falls back to the channel's first interval: with one
// interval, that interval is the last.)
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/MeshData.hpp>
#include <Backends/ModelPose.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <Backends/RenderPass.hpp>
#include <gtest/gtest.h>

#include "ScratchFolder.hpp"

#include <raylib.h>

#if __has_include(<Content/ContentRoot.hpp>)
#include <Backends/Raylib/RaylibContentLoads.hpp>
#include <Content/ContentRoot.hpp>
#define BRITE_TEST_HAS_CONTENT 1
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using BRITE::Backends::Raylib::RaylibRenderBackend;

namespace {

constexpr int SIZE = 64;
constexpr float EPSILON = 1e-5f;
constexpr float SIN_45 = 0.70710678f; // a quarter turn about +z is the quaternion (0, 0, sin 45, cos 45)

// ---------------------------------------------------------------------------
// A glTF writer for a skinned model: one mesh with its skin, a chain of joints, and clips of one translation channel.
// ---------------------------------------------------------------------------

struct Joint {
    std::string Name;
    int Parent = -1;
    float Translation[3] = {0.0f, 0.0f, 0.0f};
};

struct Channel {
    int Joint = 0;
    float From[3];
    float To[3];
};

struct Clip {
    std::string Name;
    std::vector<Channel> Channels; // each from 0 s to 1 s
};

template <typename T> void Append(std::vector<unsigned char>& bin, const T& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    bin.insert(bin.end(), bytes, bytes + sizeof(T));
}

std::string Floats(const float* values, int count) {
    std::string out = "[";
    for (int i = 0; i < count; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s%.9g", i ? "," : "", values[i]);
        out += buf;
    }
    return out + "]";
}

// The arm's mesh, bound to joints 0 and 1 as the header says, on any chain of joints.
std::string WriteSkinnedGltf(const BRITE::Test::ScratchFolder& folder, const std::string& name,
                             const std::vector<Joint>& joints, const std::vector<Clip>& clips) {
    std::vector<unsigned char> bin;
    std::string views, accessors;
    int index = 0;
    auto addView = [&](std::size_t offset, int count, int componentType, const char* type) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}",
                      views.empty() ? "" : ",", offset, bin.size() - offset);
        views += buf;
        std::snprintf(buf, sizeof(buf), "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%d,\"type\":\"%s\"}",
                      accessors.empty() ? "" : ",", index, componentType, count, type);
        accessors += buf;
        while (bin.size() % 4)
            bin.push_back(0);
        return index++;
    };

    const float positions[6][3] = {{-0.5f, 0, 0}, {0.5f, 0, 0}, {0.5f, 1, 0},
                                   {-0.5f, 1, 0}, {0.5f, 2, 0}, {-0.5f, 2, 0}};
    const unsigned char jointsOf[6][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 1, 0, 0},
                                          {0, 1, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0}};
    const float weightsOf[6][4] = {{1, 0, 0, 0},       {1, 0, 0, 0}, {0.5f, 0.5f, 0, 0},
                                   {0.5f, 0.5f, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0}};
    const unsigned short triangles[12] = {0, 1, 2, 0, 2, 3, 3, 2, 4, 3, 4, 5};

    std::size_t start = bin.size();
    for (const auto& p : positions)
        for (float f : p)
            Append(bin, f);
    const int position = addView(start, 6, 5126, "VEC3");
    start = bin.size();
    for (int v = 0; v < 6; ++v) {
        Append(bin, 0.0f);
        Append(bin, 0.0f);
        Append(bin, 1.0f);
    }
    const int normal = addView(start, 6, 5126, "VEC3");
    start = bin.size();
    for (const auto& j : jointsOf)
        for (unsigned char b : j)
            Append(bin, b);
    const int jointAttribute = addView(start, 6, 5121, "VEC4");
    start = bin.size();
    for (const auto& w : weightsOf)
        for (float f : w)
            Append(bin, f);
    const int weight = addView(start, 6, 5126, "VEC4");
    start = bin.size();
    for (unsigned short i : triangles)
        Append(bin, i);
    const int indices = addView(start, 12, 5123, "SCALAR");

    // Each joint's inverse bind matrix: the inverse of its placement, a translation by minus the sum of its chain's
    // translations (no joint here rotates or scales at rest). Column-major, as glTF stores a matrix.
    start = bin.size();
    for (std::size_t j = 0; j < joints.size(); ++j) {
        float placed[3] = {0, 0, 0};
        for (int at = static_cast<int>(j); at >= 0; at = joints[static_cast<std::size_t>(at)].Parent)
            for (int c = 0; c < 3; ++c)
                placed[c] += joints[static_cast<std::size_t>(at)].Translation[c];
        const float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -placed[0], -placed[1], -placed[2], 1};
        for (float f : matrix)
            Append(bin, f);
    }
    const int inverseBind = addView(start, static_cast<int>(joints.size()), 5126, "MAT4");

    std::string animations;
    for (const Clip& clip : clips) {
        std::string samplers, channels;
        for (const Channel& channel : clip.Channels) {
            start = bin.size();
            Append(bin, 0.0f);
            Append(bin, 1.0f);
            const int input = addView(start, 2, 5126, "SCALAR");
            start = bin.size();
            for (float f : channel.From)
                Append(bin, f);
            for (float f : channel.To)
                Append(bin, f);
            const int output = addView(start, 2, 5126, "VEC3");
            const int sampler = static_cast<int>(std::count(samplers.begin(), samplers.end(), '{'));
            samplers += (samplers.empty() ? "" : ",") + std::string("{\"input\":") + std::to_string(input) +
                        ",\"output\":" + std::to_string(output) + ",\"interpolation\":\"LINEAR\"}";
            channels += (channels.empty() ? "" : ",") + std::string("{\"sampler\":") + std::to_string(sampler) +
                        ",\"target\":{\"node\":" + std::to_string(channel.Joint) + ",\"path\":\"translation\"}}";
        }
        animations += (animations.empty() ? "" : ",") + std::string("{\"name\":\"") + clip.Name + "\",\"samplers\":[" +
                      samplers + "],\"channels\":[" + channels + "]}";
    }

    // Nodes: the joints first, by index, then the mesh's own node at the identity.
    std::string nodes, roots, jointList;
    for (std::size_t j = 0; j < joints.size(); ++j) {
        std::string children;
        for (std::size_t k = 0; k < joints.size(); ++k)
            if (joints[k].Parent == static_cast<int>(j))
                children += (children.empty() ? "" : ",") + std::to_string(k);
        nodes += (nodes.empty() ? "" : ",") + std::string("{\"name\":\"") + joints[j].Name +
                 "\",\"translation\":" + Floats(joints[j].Translation, 3) +
                 (children.empty() ? "" : ",\"children\":[" + children + "]") + "}";
        if (joints[j].Parent < 0)
            roots += (roots.empty() ? "" : ",") + std::to_string(j);
        jointList += (jointList.empty() ? "" : ",") + std::to_string(j);
    }
    const std::string meshNode = std::to_string(joints.size());
    nodes += ",{\"name\":\"body\",\"mesh\":0,\"skin\":0}";
    roots += "," + meshNode;

    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[" + roots + "]}],\"nodes\":[" + nodes +
        "],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(position) +
        ",\"NORMAL\":" + std::to_string(normal) + ",\"JOINTS_0\":" + std::to_string(jointAttribute) +
        ",\"WEIGHTS_0\":" + std::to_string(weight) + "},\"indices\":" + std::to_string(indices) +
        "}]}],\"skins\":[{\"joints\":[" + jointList + "],\"inverseBindMatrices\":" + std::to_string(inverseBind) +
        "}],\"accessors\":[" + accessors + "],\"bufferViews\":[" + views + "],\"buffers\":[{\"uri\":\"" + name +
        ".bin\",\"byteLength\":" + std::to_string(bin.size()) + "}]";
    if (!animations.empty())
        json += ",\"animations\":[" + animations + "]";
    json += "}";

    folder.Write(name + ".bin", std::string(reinterpret_cast<const char*>(bin.data()), bin.size()));
    return folder.Write(name + ".gltf", json).string();
}

std::vector<Joint> ArmJoints() {
    return {{"root", -1, {0.0f, 0.5f, 0.0f}}, {"tip", 0, {0.0f, 0.5f, 0.0f}}};
}

std::vector<Clip> ArmClips() {
    return {{"slide", {{1, {0.0f, 0.5f, 0.0f}, {1.0f, 0.5f, 0.0f}}}},
            {"lift", {{0, {0.0f, 0.5f, 0.0f}, {0.0f, 1.5f, 0.0f}}}}};
}

constexpr int SLIDE = 0;
constexpr int LIFT = 1;

void ExpectAt(const BRITE::Vector3& actual, float x, float y, const std::string& what) {
    EXPECT_NEAR(actual.x, x, EPSILON) << what;
    EXPECT_NEAR(actual.y, y, EPSILON) << what;
    EXPECT_NEAR(actual.z, 0.0f, EPSILON) << what;
}

BRITE::Quaternion QuarterTurnAboutZ() {
    return {0.0f, 0.0f, SIN_45, SIN_45};
}

class SkeletalAnimationTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(SIZE, SIZE, "BRITE skeletal animation tests");
    }
    static void TearDownTestSuite() {
        ::CloseWindow();
    }
    void TearDown() override {
        for (const BRITE::ModelHandle model : m_models)
            m_backend.UnloadModel(model);
    }

    BRITE::ModelHandle Arm() {
        const BRITE::ModelHandle model =
            m_backend.LoadModel(WriteSkinnedGltf(m_folder, "arm", ArmJoints(), ArmClips()).c_str());
        EXPECT_NE(model, BRITE::NullModelHandle);
        m_models.push_back(model);
        return model;
    }

    // The arm's six vertices under a pose, read back.
    std::vector<BRITE::Vector3> Posed(BRITE::ModelHandle model, const BRITE::ModelPose& pose) {
        std::vector<BRITE::MeshData> meshes;
        EXPECT_TRUE(m_backend.ReadPosedModelMeshes(model, pose, meshes));
        if (meshes.size() != 1 || meshes[0].Positions.size() != 6) {
            ADD_FAILURE() << "the arm reads back as one mesh of six vertices";
            return std::vector<BRITE::Vector3>(6, BRITE::Vector3{1e9f, 1e9f, 1e9f});
        }
        return meshes[0].Positions;
    }

    // THE PICTURE: the camera at (0.25, 1, 5) looks down -z at (0.25, 1, 0), +y up, orthographic and 8 world units
    // across, so world x -3.75..4.25 and y -3..5 map onto 64 pixels, 8 a unit: column (x + 3.75) * 8, row (5 - y) * 8
    // from the top. Every draw is unlit white over a black clear.
    std::vector<BRITE::Color> Picture(const std::vector<BRITE::ModelDrawCommand>& draws,
                                      const std::vector<BRITE::Light>& lights = {}) {
        BRITE::Camera3D camera{
            {0.25f, 1.0f, 5.0f}, {0.25f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 8.0f, BRITE::CameraProjection::Orthographic};
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
        BRITE::RenderPass pass;
        pass.TargetFramebuffer = target;
        pass.ClearColor = {0, 0, 0, 255};
        pass.Camera3DPtr = &camera;
        pass.ModelCommands = draws;
        pass.Lights = lights;
        m_backend.SubmitRenderPass(pass);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        EXPECT_EQ(width * height, SIZE * SIZE);
        m_backend.UnloadRenderTexture(target);
        return pixels;
    }

    static BRITE::ModelDrawCommand Draw(BRITE::ModelHandle model, float x, BRITE::ModelPose pose = {},
                                        unsigned char alpha = 255) {
        BRITE::ModelDrawCommand draw;
        draw.Model = model;
        draw.Material.Unlit = true;
        draw.Material.AlbedoTint = {255, 255, 255, alpha};
        draw.Position = {x, 0.0f, 0.0f};
        draw.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        draw.Scale = {1.0f, 1.0f, 1.0f};
        draw.Pose = std::move(pose);
        return draw;
    }

    // Whether the pixel under world (x, y) is drawn: white, or at least `least` in every channel.
    static bool Drawn(const std::vector<BRITE::Color>& pixels, float x, float y, int least = 200) {
        const int column = static_cast<int>(std::floor((x + 3.75f) * 8.0f));
        const int row = static_cast<int>(std::floor((5.0f - y) * 8.0f));
        const BRITE::Color c = pixels.at(static_cast<std::size_t>(row * SIZE + column));
        return c.r > least && c.g > least && c.b > least;
    }

    RaylibRenderBackend m_backend;
    BRITE::Test::ScratchFolder m_folder{"skeletal_animation"};
    std::vector<BRITE::ModelHandle> m_models;
};

// The pose from code of several cases: root moved right by 1, to rest translation (1, 0.5, 0); tip turned a quarter
// turn counter-clockwise about +z at its own joint, its translation left at rest, (0, 0.5, 0).
//
//   root placed at (1, 0.5, 0), a move of (1, 0) from its rest placement (0, 0.5, 0): it takes a vertex v to
//   v + (1, 0).
//   tip placed by root: the quarter turn, at (0, 0.5, 0) + (1, 0.5, 0) = (1, 1, 0). It takes v to
//   R(v - (0, 1)) + (1, 1), where its rest placement was (0, 1) and R takes (x, y) to (-y, x).
//
//   v4 (0.5, 2):  tip:  (0.5, 1) -R-> (-1, 0.5)  + (1, 1) = (0, 1.5)
//   v5 (-0.5, 2): tip:  (-0.5, 1) -R-> (-1, -0.5) + (1, 1) = (0, 0.5)
//   v2 (0.5, 1):  root (1.5, 1); tip (0.5, 0) -R-> (0, 0.5) + (1, 1) = (1, 1.5); half each: (1.25, 1.25)
//   v3 (-0.5, 1): root (0.5, 1); tip (-0.5, 0) -R-> (0, -0.5) + (1, 1) = (1, 0.5); half each: (0.75, 0.75)
//   v0 (-0.5, 0) -> (0.5, 0); v1 (0.5, 0) -> (1.5, 0)
BRITE::ModelPose MovedAndTurned() {
    BRITE::BoneTransform root;
    root.Translation = {1.0f, 0.5f, 0.0f};
    BRITE::BoneTransform tip;
    tip.Translation = {0.0f, 0.5f, 0.0f};
    tip.Rotation = QuarterTurnAboutZ();
    return BRITE::ModelPose::FromBones({root, tip});
}

} // namespace

// The skeleton and clips as the file wrote them, names to indices.
//
// Each clip runs from 0 s to 1 s: 61 keyframes at 60 a second, (61 - 1) / 60 = 1 s. Tip's rest is its translation
// relative to root, (0, 0.5, 0), not where it is placed, (0, 1, 0).
//
// Mutations: the rest read as the placement raylib keeps (no RelativeTo) -> tip's rest reads (0, 1, 0); a clip's
// length counted as keyframes / 60 -> 61 / 60 = 1.016667 s.
TEST_F(SkeletalAnimationTest, ASkinnedModelKeepsItsBonesAndTheClipsOfItsFile) {
    const BRITE::ModelHandle model = Arm();
    BRITE::ModelSkeleton skeleton;
    ASSERT_TRUE(m_backend.ReadModelSkeleton(model, skeleton));
    ASSERT_EQ(skeleton.Bones.size(), 2u);
    EXPECT_EQ(skeleton.Bones[0].Name, "root");
    EXPECT_EQ(skeleton.Bones[0].Parent, -1);
    ExpectAt(skeleton.Bones[0].Rest.Translation, 0.0f, 0.5f, "root's rest");
    EXPECT_EQ(skeleton.Bones[1].Name, "tip");
    EXPECT_EQ(skeleton.Bones[1].Parent, 0);
    ExpectAt(skeleton.Bones[1].Rest.Translation, 0.0f, 0.5f, "tip's rest is relative to root");
    EXPECT_NEAR(skeleton.Bones[1].Rest.Rotation.w, 1.0f, EPSILON) << "tip's rest is unturned";
    EXPECT_NEAR(skeleton.Bones[1].Rest.Scale.y, 1.0f, EPSILON) << "tip's rest is unscaled";

    ASSERT_EQ(skeleton.Clips.size(), 2u);
    EXPECT_EQ(skeleton.Clips[SLIDE].Name, "slide");
    EXPECT_NEAR(skeleton.Clips[SLIDE].Duration, 1.0f, EPSILON);
    EXPECT_EQ(skeleton.Clips[LIFT].Name, "lift");
    EXPECT_NEAR(skeleton.Clips[LIFT].Duration, 1.0f, EPSILON);

    EXPECT_EQ(BRITE::FindBone(skeleton, "tip"), 1);
    EXPECT_EQ(BRITE::FindClip(skeleton, "lift"), LIFT);
    EXPECT_EQ(BRITE::FindBone(skeleton, "elbow"), -1);
    EXPECT_EQ(BRITE::FindClip(skeleton, "wave"), -1);

    // The rest as a pose from code draws the model at rest: tip's top corner where it is written.
    std::vector<BRITE::BoneTransform> rest;
    for (const BRITE::SkeletonBone& bone : skeleton.Bones)
        rest.push_back(bone.Rest);
    const std::vector<BRITE::Vector3> atRest = Posed(model, BRITE::ModelPose::FromBones(rest));
    ExpectAt(atRest[4], 0.5f, 2.0f, "v4 under the rest as a pose");
    ExpectAt(atRest[2], 0.5f, 1.0f, "v2 under the rest as a pose");
}

// One clip at a time, at times the caller names.
//
// "slide" at 0.5 s: tip at (0.5, 0.5, 0) relative to root, placed at (0.5, 1, 0): a move of (0.5, 0) from its rest
// placement (0, 1). Root does not move.
//   v4 (0.5, 2) -> (1, 2); v5 (-0.5, 2) -> (0, 2)
//   v2 (0.5, 1) -> half of (0, 0) and (0.5, 0) = a move of (0.25, 0): (0.75, 1); v3 -> (-0.25, 1)
//   v0, v1 stay.
// "lift" at 0.25 s: root at (0, 0.75, 0), a move of (0, 0.25), and tip, placed by root, moves with it: every vertex
// moves (0, 0.25). v0 (-0.5, 0) -> (-0.5, 0.25); v4 (0.5, 2) -> (0.5, 2.25).
//
// Mutations: the time read as a keyframe rather than seconds (0.5 is keyframe 0.5, not 30) -> v4 at (0.508333, 2);
// the clip time read off the wall clock instead of the command -> v4 wherever the clock is; the clip index ignored
// (always clip 0) -> "lift" draws as "slide".
TEST_F(SkeletalAnimationTest, OneClipPosesTheModelAtTheTimeTheDrawNames) {
    const BRITE::ModelHandle model = Arm();

    const std::vector<BRITE::Vector3> slid = Posed(model, BRITE::ModelPose::FromClip(SLIDE, 0.5f));
    ExpectAt(slid[0], -0.5f, 0.0f, "slide 0.5 s: v0");
    ExpectAt(slid[1], 0.5f, 0.0f, "slide 0.5 s: v1");
    ExpectAt(slid[2], 0.75f, 1.0f, "slide 0.5 s: v2");
    ExpectAt(slid[3], -0.25f, 1.0f, "slide 0.5 s: v3");
    ExpectAt(slid[4], 1.0f, 2.0f, "slide 0.5 s: v4");
    ExpectAt(slid[5], 0.0f, 2.0f, "slide 0.5 s: v5");

    const std::vector<BRITE::Vector3> lifted = Posed(model, BRITE::ModelPose::FromClip(LIFT, 0.25f));
    ExpectAt(lifted[0], -0.5f, 0.25f, "lift 0.25 s: v0");
    ExpectAt(lifted[4], 0.5f, 2.25f, "lift 0.25 s: v4");
}

// A time outside the clip is held to its ends: before the start, its first keyframe; past the end, its last. The
// caller loops a clip that loops.
//
// "slide" at -1 s: its first keyframe, tip at its rest (0, 0.5, 0): v4 stays at (0.5, 2).
// "slide" at 5 s: its last, tip at (1, 0.5, 0), a move of (1, 0): v4 at (1.5, 2); v2 moves half of it, (1, 1).
//
// Mutations: the time wrapped into the clip instead (5 s is 0 s of the fifth loop after the first) -> v4 at (0.5, 2)
// at 5 s; the end not held (keyframe 300 handed to raylib, which wraps it to 300 mod 61 = 56, tip at 56/60 =
// 0.933333) -> v4 at (1.433333, 2).
TEST_F(SkeletalAnimationTest, ATimeOutsideTheClipIsHeldToItsEnds) {
    const BRITE::ModelHandle model = Arm();

    const std::vector<BRITE::Vector3> before = Posed(model, BRITE::ModelPose::FromClip(SLIDE, -1.0f));
    ExpectAt(before[4], 0.5f, 2.0f, "slide at -1 s: v4");

    const std::vector<BRITE::Vector3> after = Posed(model, BRITE::ModelPose::FromClip(SLIDE, 5.0f));
    ExpectAt(after[4], 1.5f, 2.0f, "slide at 5 s: v4");
    ExpectAt(after[2], 1.0f, 1.0f, "slide at 5 s: v2");

    const std::vector<BRITE::Vector3> notANumber = Posed(model, BRITE::ModelPose::FromClip(SLIDE, std::nanf("")));
    ExpectAt(notANumber[4], 0.5f, 2.0f, "slide at not-a-number seconds: its first keyframe");
}

// Two clips blended, each at its own time: "slide" (A) at 0.5 s and "lift" (B) at 0.5 s.
//
// A alone places root at (0, 0.5) and tip at (0.5, 1). B alone places root at (0, 1) and tip, by root, at (0, 1.5).
// A weight w moves each bone's placement from A's toward B's by w:
//   root at (0, 0.5 + 0.5 w): a move of (0, 0.5 w) from its rest placement (0, 0.5)
//   tip at (0.5 - 0.5 w, 1 + 0.5 w): a move of (0.5 (1 - w), 0.5 w) from its rest placement (0, 1)
//
// w = 0.5: root moves (0, 0.25), tip (0.25, 0.25).
//   v4 (0.5, 2) -> (0.75, 2.25); v0 (-0.5, 0) -> (-0.5, 0.25); v2 (0.5, 1) moves half of each, (0.125, 0.25):
//   (0.625, 1.25).
// w = 0.25: root moves (0, 0.125), tip (0.375, 0.125).
//   v4 -> (0.875, 2.125); v2 moves (0.1875, 0.125): (0.6875, 1.125).
// w = 1.5 is held to 1, B alone: everything moves (0, 0.5): v4 (0.5, 2.5).
//
// A 50/50 blend reads the same whichever clip the weight favours, so it cannot tell the weight's direction; 0.25 can.
// Mutations: the weight's direction reversed (1 - w) -> at w = 0.25 v4 at (0.625, 2.375); the weight not held to 0..1
// (raylib then leaves the buffers as the last pose left them) -> at 1.5, v4 at the w = 0.25 pose's (0.875, 2.125); B
// sampled at A's time -> the last blend's lift at 0.5 s, v4 (0.5, 2.5).
TEST_F(SkeletalAnimationTest, TwoClipsBlendByWeightEachAtItsOwnTime) {
    const BRITE::ModelHandle model = Arm();

    const std::vector<BRITE::Vector3> even =
        Posed(model, BRITE::ModelPose::FromBlend({SLIDE, 0.5f}, {LIFT, 0.5f}, 0.5f));
    ExpectAt(even[4], 0.75f, 2.25f, "50/50: v4");
    ExpectAt(even[0], -0.5f, 0.25f, "50/50: v0");
    ExpectAt(even[2], 0.625f, 1.25f, "50/50: v2");

    const std::vector<BRITE::Vector3> quarter =
        Posed(model, BRITE::ModelPose::FromBlend({SLIDE, 0.5f}, {LIFT, 0.5f}, 0.25f));
    ExpectAt(quarter[4], 0.875f, 2.125f, "a quarter toward lift: v4");
    ExpectAt(quarter[2], 0.6875f, 1.125f, "a quarter toward lift: v2");

    const std::vector<BRITE::Vector3> beyond =
        Posed(model, BRITE::ModelPose::FromBlend({SLIDE, 0.5f}, {LIFT, 0.5f}, 1.5f));
    ExpectAt(beyond[4], 0.5f, 2.5f, "a weight of 1.5 is lift alone: v4");

    // B at its own time: lift at 0.25 s alone (w = 1) moves everything (0, 0.25).
    const std::vector<BRITE::Vector3> ownTime =
        Posed(model, BRITE::ModelPose::FromBlend({SLIDE, 0.5f}, {LIFT, 0.25f}, 1.0f));
    ExpectAt(ownTime[4], 0.5f, 2.25f, "B at its own time: v4");
}

// A pose from code: every bone relative to its parent, placed down the chain (MovedAndTurned, above).
//
// Mutations: each bone placed as if it had no parent (tip at (0, 0.5) with the quarter turn) -> v4 at
// R(0.5, 1) + (0, 0.5) = (-1, 0.5) + (0, 0.5) = (-1, 1); the rotation dropped -> v4 at (0.5, 1) + (1, 1) = (1.5, 2).
TEST_F(SkeletalAnimationTest, APoseFromCodePlacesEachBoneByItsParent) {
    const BRITE::ModelHandle model = Arm();
    const std::vector<BRITE::Vector3> posed = Posed(model, MovedAndTurned());
    ExpectAt(posed[0], 0.5f, 0.0f, "v0");
    ExpectAt(posed[1], 1.5f, 0.0f, "v1");
    ExpectAt(posed[2], 1.25f, 1.25f, "v2");
    ExpectAt(posed[3], 0.75f, 0.75f, "v3");
    ExpectAt(posed[4], 0.0f, 1.5f, "v4");
    ExpectAt(posed[5], 0.0f, 0.5f, "v5");

    // The normals turn with their bones: a quarter turn about +z leaves +z as it is, and they read back unit length.
    std::vector<BRITE::MeshData> meshes;
    ASSERT_TRUE(m_backend.ReadPosedModelMeshes(model, MovedAndTurned(), meshes));
    ASSERT_EQ(meshes[0].Normals.size(), 6u);
    EXPECT_NEAR(meshes[0].Normals[4].z, 1.0f, EPSILON);
}

// A pose the model cannot take draws it at rest: a clip it does not have, or a pose from code with a transform too
// many. v4 at rest is (0.5, 2), and a posed draw before each leaves the buffers holding another pose.
//
// Mutation: the count checked as "at least one per bone" -> the three-transform pose is taken, v4 at (0, 1.5).
TEST_F(SkeletalAnimationTest, APoseTheModelCannotTakeDrawsItAtRest) {
    const BRITE::ModelHandle model = Arm();
    BRITE::ModelPose tooMany = MovedAndTurned();
    tooMany.Bones.push_back(BRITE::BoneTransform{});

    Posed(model, BRITE::ModelPose::FromClip(SLIDE, 1.0f));
    ExpectAt(Posed(model, tooMany)[4], 0.5f, 2.0f, "a transform too many: v4 at rest");
    Posed(model, BRITE::ModelPose::FromClip(SLIDE, 1.0f));
    ExpectAt(Posed(model, BRITE::ModelPose::FromClip(7, 0.5f))[4], 0.5f, 2.0f, "a clip the model lacks: v4 at rest");
    ExpectAt(Posed(model, BRITE::ModelPose::FromBlend({SLIDE, 0.5f}, {-1, 0.5f}, 0.5f))[4], 0.5f, 2.0f,
             "a blend with a clip the model lacks: v4 at rest");
    ExpectAt(Posed(model, BRITE::ModelPose{})[4], 0.5f, 2.0f, "no pose: v4 at rest");

    std::vector<BRITE::MeshData> rest;
    ASSERT_TRUE(m_backend.ReadModelMeshes(model, rest));
    ExpectAt(rest[0].Positions[4], 0.5f, 2.0f, "ReadModelMeshes reads the rest geometry after a posed draw");
}

// Two draws of ONE model in ONE pass, each in its own pose, drawn side by side (the picture above).
//
//   draw 1 at x = -1.5, MovedAndTurned with root put back at rest: only the tip turned, at its rest placement (0, 1).
//     Its upper square becomes v3 (-0.25, 0.75), v2 (0.25, 1.25), v4 (-1, 1.5), v5 (-1, 0.5) -- v4 is
//     R(0.5, 1) + (0, 1) = (-1, 1.5) -- which covers (-0.75, 1): at x = -0.75 it runs from y 0.58 (the edge v5 v3)
//     to y 1.45 (the edge v2 v4). At rest nothing is left of x = -0.5. In the world: (-2.25, 1).
//   draw 2 at x = +1.5, "slide" at 1 s: tip moved (1, 0). Its upper square is v3 (0, 1), v2 (1, 1), v4 (1.5, 2),
//     v5 (0.5, 2): at y = 1.75 it spans x 0.375..1.375, so it covers (1, 1.75); at rest that row spans -0.5..0.5,
//     and the turned pose rises no higher than 1.5. In the world: (2.5, 1.75).
//
// Each must show its own pose and not the other's: draw 1's turned tip at (-2.25, 1), and nothing at
// (-1.5 + 1, 1.75) = (-0.5, 1.75), where it would show had it drawn the slide; draw 2's slid tip at (2.5, 1.75), and
// nothing at (1.5, 1.75), where it would show had it drawn the turn (which reaches no higher than 1.5) or the rest.
// Both lower squares are drawn: (-1.5, 0.25) and (1.5, 0.25).
//
// Mutations: the pose applied once per model per pass (the first draw's) -> draw 2 draws the turn: nothing at
// (2.5, 1.75); the last draw's pose applied to all (posing before the draw loop) -> draw 1 draws the slide: drawn at
// (-0.5, 1.75), nothing at (-2.25, 1).
TEST_F(SkeletalAnimationTest, TwoDrawsOfOneModelInOnePassEachDrawTheirOwnPose) {
    const BRITE::ModelHandle model = Arm();
    BRITE::ModelPose turned = MovedAndTurned();
    turned.Bones[0].Translation = {0.0f, 0.5f, 0.0f};

    const std::vector<BRITE::Color> picture =
        Picture({Draw(model, -1.5f, turned), Draw(model, 1.5f, BRITE::ModelPose::FromClip(SLIDE, 1.0f))});
    EXPECT_TRUE(Drawn(picture, -1.5f, 0.25f)) << "draw 1's lower square";
    EXPECT_TRUE(Drawn(picture, 1.5f, 0.25f)) << "draw 2's lower square";
    EXPECT_TRUE(Drawn(picture, -2.25f, 1.0f)) << "draw 1's turned tip";
    EXPECT_FALSE(Drawn(picture, -0.5f, 1.75f)) << "draw 1 did not draw draw 2's slide";
    EXPECT_TRUE(Drawn(picture, 2.5f, 1.75f)) << "draw 2's slid tip";
    EXPECT_FALSE(Drawn(picture, 1.5f, 1.75f)) << "draw 2 did not draw draw 1's turn, nor the rest";
}

// The same two draws see-through, at alpha 128: a translucent draw's meshes are drawn after every opaque one, sorted
// farthest first, one mesh at a time -- so a draw is posed where its mesh is drawn, not where the pass lists it. Each
// still shows its own pose. White at alpha 128 over the black clear is 255 x 128 / 255 = 128 a channel, so "drawn"
// here is above 100, and "not drawn" is the clear, below it.
//
// Mutations: the pose put in the buffers only by the opaque stage's call -> neither draw is posed where it is drawn,
// both show the last pose posed: draw 1 the slide, drawn at (-0.5, 1.75); the pose taken as in the buffers whenever
// the model was posed in this pass, whichever command posed it -> draw 2 draws draw 1's turn: nothing at (2.5, 1.75).
TEST_F(SkeletalAnimationTest, TwoSeeThroughDrawsOfOneModelEachDrawTheirOwnPose) {
    const BRITE::ModelHandle model = Arm();
    BRITE::ModelPose turned = MovedAndTurned();
    turned.Bones[0].Translation = {0.0f, 0.5f, 0.0f};

    const std::vector<BRITE::Color> picture =
        Picture({Draw(model, -1.5f, turned, 128), Draw(model, 1.5f, BRITE::ModelPose::FromClip(SLIDE, 1.0f), 128)});
    EXPECT_TRUE(Drawn(picture, -1.5f, 0.25f, 100)) << "draw 1's lower square";
    EXPECT_TRUE(Drawn(picture, -2.25f, 1.0f, 100)) << "draw 1's turned tip";
    EXPECT_FALSE(Drawn(picture, -0.5f, 1.75f, 100)) << "draw 1 did not draw draw 2's slide";
    EXPECT_TRUE(Drawn(picture, 2.5f, 1.75f, 100)) << "draw 2's slid tip";
    EXPECT_FALSE(Drawn(picture, 1.5f, 1.75f, 100)) << "draw 2 did not draw draw 1's turn, nor the rest";
}

// A draw with no pose after a posed one -- in the same pass, and in the next -- draws the model at rest, though the
// posed draw left its pose in the model's vertex buffers.
//
//   draw 1 at x = -1.5, "slide" at 1 s; draw 2 at x = +1.5, no pose: at rest its upper square spans x -0.5..0.5, so
//   it covers (1.5, 1.75) and not (2.5, 1.75), which the slide covers (the case above).
//
// Mutation: the rest geometry not put back -> draw 2 draws the slide: nothing at (1.5, 1.75), drawn at (2.5, 1.75).
TEST_F(SkeletalAnimationTest, ADrawWithNoPoseAfterAPosedOneDrawsTheModelAtRest) {
    const BRITE::ModelHandle model = Arm();
    const std::vector<BRITE::Color> samePass =
        Picture({Draw(model, -1.5f, BRITE::ModelPose::FromClip(SLIDE, 1.0f)), Draw(model, 1.5f)});
    EXPECT_TRUE(Drawn(samePass, -0.5f, 1.75f)) << "draw 1's slid tip";
    EXPECT_TRUE(Drawn(samePass, 1.5f, 1.75f)) << "draw 2 at rest";
    EXPECT_FALSE(Drawn(samePass, 2.5f, 1.75f)) << "draw 2 did not draw the slide";

    Picture({Draw(model, 1.5f, BRITE::ModelPose::FromClip(SLIDE, 1.0f))});
    const std::vector<BRITE::Color> nextPass = Picture({Draw(model, 1.5f)});
    EXPECT_TRUE(Drawn(nextPass, 1.5f, 1.75f)) << "the next pass's draw at rest";
    EXPECT_FALSE(Drawn(nextPass, 2.5f, 1.75f)) << "the next pass's draw did not keep the last pass's slide";
}

// A LIT posed draw is shaded with its posed normals. Tip turned 60 degrees about +y at its joint, (0, 1, 0) -- the
// quaternion (0, sin 30, 0, cos 30) = (0, 0.5, 0, 0.866025). The turn takes (x, y, z) to
// (x cos 60 + z sin 60, y, -x sin 60 + z cos 60) = (0.5 x + 0.866025 z, y, -0.866025 x + 0.5 z), so:
//
//   v4 (0.5, 2, 0):  (0.5, 1, 0) from the joint -> (0.25, 1, -0.433013) -> (0.25, 2, -0.433013)
//   v5 (-0.5, 2, 0): (-0.5, 1, 0) -> (-0.25, 1, 0.433013) -> (-0.25, 2, 0.433013)
//   v2 (0.5, 1, 0):  root (0.5, 1, 0); tip (0.5, 0, 0) -> (0.25, 0, -0.433013) + (0, 1, 0); half each:
//                    (0.375, 1, -0.216506)
//   v3 (-0.5, 1, 0): root (-0.5, 1, 0); tip (-0.25, 1, 0.433013); half each: (-0.375, 1, 0.216506)
//   v0, v1 stay.
//   Normals: +z turned is (sin 60, 0, cos 60) = (0.866025, 0, 0.5) for v4 and v5; v2 and v3 take half of +z and half
//   of that, (0.433013, 0, 0.75), of length sqrt(0.1875 + 0.5625) = 0.866025, so (0.5, 0, 0.866025); v0 and v1 +z.
//
// That geometry, built in code with those normals, is the reference: drawn lit at the same place under the same light
// -- one directional light travelling -x, from the side, and no ambient -- it must shade every pixel as the posed
// skinned draw does. Read at two points well inside the arm: (0, 1.75) on the upper square, (0, 0.75) on the lower.
// And the turn must show: the light comes from +x, so a +z normal at rest receives none (cos 90) and the arm at rest is
// black there, while the turned normal (0.866025, 0, 0.5) receives cos 30 = 0.866025 of it.
//
// Mutation: the posed normals left where raylib's skinner puts them (the colour buffer) -> the posed draw is shaded
// with its rest normals, as dark as the rest draw at (0, 1.75).
TEST_F(SkeletalAnimationTest, ALitPosedDrawIsShadedWithItsPosedNormals) {
    const BRITE::ModelHandle model = Arm();
    BRITE::ModelPose turned = MovedAndTurned();
    turned.Bones[0].Translation = {0.0f, 0.5f, 0.0f};
    turned.Bones[1].Rotation = {0.0f, 0.5f, 0.0f, 0.8660254f};

    BRITE::MeshData reference;
    reference.Positions = {{-0.5f, 0.0f, 0.0f},         {0.5f, 0.0f, 0.0f},         {0.375f, 1.0f, -0.2165064f},
                           {-0.375f, 1.0f, 0.2165064f}, {0.25f, 2.0f, -0.4330127f}, {-0.25f, 2.0f, 0.4330127f}};
    reference.Normals = {{0.0f, 0.0f, 1.0f},       {0.0f, 0.0f, 1.0f},       {0.5f, 0.0f, 0.8660254f},
                         {0.5f, 0.0f, 0.8660254f}, {0.8660254f, 0.0f, 0.5f}, {0.8660254f, 0.0f, 0.5f}};
    reference.Indices = {0, 1, 2, 0, 2, 3, 3, 2, 4, 3, 4, 5};
    const BRITE::ModelHandle baked = m_backend.LoadModelFromMesh(reference);
    ASSERT_NE(baked, BRITE::NullModelHandle);
    m_models.push_back(baked);

    // The posed geometry is the reference's: the read-back agrees with the hand-worked positions.
    const std::vector<BRITE::Vector3> posed = Posed(model, turned);
    for (std::size_t v = 0; v < 6; ++v) {
        EXPECT_NEAR(posed[v].x, reference.Positions[v].x, EPSILON) << "vertex " << v;
        EXPECT_NEAR(posed[v].y, reference.Positions[v].y, EPSILON) << "vertex " << v;
        EXPECT_NEAR(posed[v].z, reference.Positions[v].z, EPSILON) << "vertex " << v;
    }

    auto lit = [&](BRITE::ModelHandle drawn, BRITE::ModelPose pose) {
        BRITE::ModelDrawCommand draw = Draw(drawn, 0.0f, std::move(pose));
        draw.Material.Unlit = false;
        BRITE::Light light;
        light.Type = BRITE::LightType::Directional;
        light.Direction = {-1.0f, 0.0f, 0.0f};
        return Picture({draw}, {light});
    };
    const std::vector<BRITE::Color> skinned = lit(model, turned);
    const std::vector<BRITE::Color> expected = lit(baked, {});
    const std::vector<BRITE::Color> rest = lit(model, {});

    auto red = [](const std::vector<BRITE::Color>& pixels, float x, float y) {
        const int column = static_cast<int>(std::floor((x + 3.75f) * 8.0f));
        const int row = static_cast<int>(std::floor((5.0f - y) * 8.0f));
        return static_cast<int>(pixels.at(static_cast<std::size_t>(row * SIZE + column)).r);
    };
    EXPECT_GT(red(expected, 0.0f, 1.75f), 0) << "the reference is drawn and lit";
    EXPECT_GT(red(expected, 0.0f, 1.75f), red(rest, 0.0f, 1.75f) + 60)
        << "the turn faces the upper square to the light";
    EXPECT_NEAR(red(skinned, 0.0f, 1.75f), red(expected, 0.0f, 1.75f), 2) << "upper square: the posed normals";
    EXPECT_NEAR(red(skinned, 0.0f, 0.75f), red(expected, 0.0f, 0.75f), 2) << "lower square: the posed normals";
}

// A model with no skin has no bones and no clips, ignores any pose, and draws exactly as it did before poses
// existed: the same pixels with a pose set as without one, and the same geometry read back posed or not.
//
// Mutation: a pose applied to a model with no skin (raylib's update on a model without bones does nothing, so the
// only way to break this is to touch its buffers) -> the pictures differ.
TEST_F(SkeletalAnimationTest, AModelWithNoSkinIgnoresAnyPose) {
    BRITE::MeshData square;
    square.Positions = {{-0.5f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {-0.5f, 1.0f, 0.0f}};
    square.Normals.assign(4, {0.0f, 0.0f, 1.0f});
    square.Indices = {0, 1, 2, 0, 2, 3};
    const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(square);
    ASSERT_NE(model, BRITE::NullModelHandle);
    m_models.push_back(model);

    BRITE::ModelSkeleton skeleton;
    ASSERT_TRUE(m_backend.ReadModelSkeleton(model, skeleton));
    EXPECT_TRUE(skeleton.Bones.empty());
    EXPECT_TRUE(skeleton.Clips.empty());
    EXPECT_EQ(m_backend.AddModelClips(model, WriteSkinnedGltf(m_folder, "clips", ArmJoints(), ArmClips()).c_str()), -1)
        << "a model with no skin takes no clips";

    const std::vector<BRITE::Color> plain = Picture({Draw(model, 0.0f)});
    const std::vector<BRITE::Color> posed = Picture({Draw(model, 0.0f, MovedAndTurned())});
    EXPECT_TRUE(Drawn(plain, 0.0f, 0.5f)) << "the square is drawn";
    EXPECT_EQ(plain.size(), posed.size());
    int differing = 0;
    for (std::size_t i = 0; i < plain.size() && i < posed.size(); ++i)
        differing += plain[i].r != posed[i].r || plain[i].g != posed[i].g || plain[i].b != posed[i].b;
    EXPECT_EQ(differing, 0) << "pixels a pose changed on a model with no skin";

    std::vector<BRITE::MeshData> rest, posedMeshes;
    ASSERT_TRUE(m_backend.ReadModelMeshes(model, rest));
    ASSERT_TRUE(m_backend.ReadPosedModelMeshes(model, MovedAndTurned(), posedMeshes));
    ASSERT_EQ(posedMeshes.size(), 1u);
    for (std::size_t v = 0; v < 4; ++v)
        ExpectAt(posedMeshes[0].Positions[v], rest[0].Positions[v].x, rest[0].Positions[v].y, "a vertex read posed");

    BRITE::ModelSkeleton none;
    EXPECT_FALSE(m_backend.ReadModelSkeleton(BRITE::NullModelHandle, none)) << "a handle that names no model";
    std::vector<BRITE::MeshData> nothing;
    EXPECT_FALSE(m_backend.ReadPosedModelMeshes(BRITE::NullModelHandle, MovedAndTurned(), nothing));
}

// The clips of another file made for the same skeleton join the model's, after its own; a file for another skeleton
// is refused whole.
//
//   "reach", in a file of its own on the same two joints: tip's translation (0, 0.5, 0) -> (0, 1.5, 0) over 1 s. At
//   1 s tip is placed at (0, 0.5) + (0, 1.5) = (0, 2), a move of (0, 1): v4 (0.5, 2) -> (0.5, 3).
//   A file whose skeleton has three joints (a "hand" below tip) moves three bones, not two: refused, and the model
//   keeps its three clips.
//
// Mutation: the bone count not checked -> the three-joint file's clip is added: 1 for -1, and four clips.
TEST_F(SkeletalAnimationTest, ClipsFromAnotherFileJoinTheModelsWhenTheyFitItsSkeleton) {
    const BRITE::ModelHandle model = Arm();
    const std::string reach =
        WriteSkinnedGltf(m_folder, "reach", ArmJoints(), {{"reach", {{1, {0.0f, 0.5f, 0.0f}, {0.0f, 1.5f, 0.0f}}}}});
    EXPECT_EQ(m_backend.AddModelClips(model, reach.c_str()), 1);

    std::vector<Joint> withHand = ArmJoints();
    withHand.push_back({"hand", 1, {0.0f, 0.5f, 0.0f}});
    const std::string threeBones =
        WriteSkinnedGltf(m_folder, "hand", withHand, {{"grip", {{2, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}}}}});
    EXPECT_EQ(m_backend.AddModelClips(model, threeBones.c_str()), -1);

    BRITE::ModelSkeleton skeleton;
    ASSERT_TRUE(m_backend.ReadModelSkeleton(model, skeleton));
    ASSERT_EQ(skeleton.Clips.size(), 3u);
    EXPECT_EQ(skeleton.Clips[2].Name, "reach");
    ExpectAt(Posed(model, BRITE::ModelPose::FromClip(2, 1.0f))[4], 0.5f, 3.0f, "reach at 1 s: v4");
}

#ifdef BRITE_TEST_HAS_CONTENT
// A skinned model named by its content name is read through the content root in force, and so are its clips, which
// raylib reads from the file a second time: by name, outside the working directory, they could come from nowhere else.
TEST_F(SkeletalAnimationTest, AModelsClipsAreReadThroughTheContentRootAsTheModelIs) {
    WriteSkinnedGltf(m_folder, "arm", ArmJoints(), ArmClips());
    const BRITE::Content::ContentRoot root(std::make_shared<BRITE::Content::FolderSource>(m_folder.path));
    std::vector<std::string> outside;
    BRITE::Raylib::FileLoadRouting routing;
    routing.root = &root;
    routing.outside = [&](std::string_view path) { outside.emplace_back(path); };
    BRITE::ModelHandle model = BRITE::NullModelHandle;
    {
        const BRITE::Raylib::ScopedFileLoadRouting routed(routing);
        model = m_backend.LoadModel("arm.gltf");
    }
    ASSERT_NE(model, BRITE::NullModelHandle);
    m_models.push_back(model);
    BRITE::ModelSkeleton skeleton;
    ASSERT_TRUE(m_backend.ReadModelSkeleton(model, skeleton));
    EXPECT_EQ(skeleton.Bones.size(), 2u);
    EXPECT_EQ(skeleton.Clips.size(), 2u) << "the clips were read through the root";
    EXPECT_TRUE(outside.empty()) << (outside.empty() ? "" : outside.front());
}
#endif

// The two helpers that turn a command's numbers into raylib's, which need no window.
//
// ClipFrame: 0.5 s at 60 keyframes a second is keyframe 30; a 61-keyframe clip's last is 60; before 0 is 0.
// HeldBlendWeight: 0.25 stays; -1 is 0; 2 is 1; not a number is 0.
TEST(SkeletalAnimationNumbers, ATimeIsAKeyframeHeldToTheClipAndAWeightIsHeldToZeroToOne) {
    EXPECT_FLOAT_EQ(RaylibRenderBackend::ClipFrame(0.5f, 61), 30.0f);
    EXPECT_FLOAT_EQ(RaylibRenderBackend::ClipFrame(2.0f, 61), 60.0f);
    EXPECT_FLOAT_EQ(RaylibRenderBackend::ClipFrame(-0.25f, 61), 0.0f);
    EXPECT_FLOAT_EQ(RaylibRenderBackend::ClipFrame(std::nanf(""), 61), 0.0f);
    EXPECT_FLOAT_EQ(RaylibRenderBackend::HeldBlendWeight(0.25f), 0.25f);
    EXPECT_FLOAT_EQ(RaylibRenderBackend::HeldBlendWeight(-1.0f), 0.0f);
    EXPECT_FLOAT_EQ(RaylibRenderBackend::HeldBlendWeight(2.0f), 1.0f);
    EXPECT_FLOAT_EQ(RaylibRenderBackend::HeldBlendWeight(std::nanf("")), 0.0f);
}

// A backend written before skeletal animation existed still builds, and says it cannot.
TEST(SkeletalAnimationDefault, ABackendThatCannotAnimateSaysSo) {
    struct Minimal : BRITE::Backends::IRenderBackend {
        void SubmitRenderPass(const BRITE::RenderPass&) override {}
        BRITE::TextureHandle LoadRenderTexture(int, int) override {
            return BRITE::NullTextureHandle;
        }
        void UnloadRenderTexture(BRITE::TextureHandle) override {}
        bool ReadRenderTexture(BRITE::TextureHandle, int&, int&, std::vector<BRITE::Color>&) override {
            return false;
        }
        BRITE::TextureHandle LoadTexture(const char*) override {
            return BRITE::NullTextureHandle;
        }
        void UnloadTexture(BRITE::TextureHandle) override {}
        BRITE::ModelHandle LoadModel(const char*) override {
            return BRITE::NullModelHandle;
        }
        BRITE::ModelHandle LoadModelFromMesh(const BRITE::MeshData&) override {
            return BRITE::NullModelHandle;
        }
        void UnloadModel(BRITE::ModelHandle) override {}
        BRITE::ShaderHandle LoadShader(const char*, const char*) override {
            return BRITE::NullShaderHandle;
        }
        BRITE::ShaderHandle LoadShaderFromMemory(const char*, const char*) override {
            return BRITE::NullShaderHandle;
        }
        void UnloadShader(BRITE::ShaderHandle) override {}
        BRITE::EnvironmentMap LoadEnvironmentMap(const char*) override {
            return {};
        }
        void UnloadEnvironmentMap(BRITE::EnvironmentMap) override {}
        int GetShaderLocation(BRITE::ShaderHandle, const char*) override {
            return -1;
        }
        void SetShaderValue(BRITE::ShaderHandle, int, const void*, BRITE::Backends::ShaderUniformDataType) override {}
    } backend;
    BRITE::ModelSkeleton skeleton;
    skeleton.Bones.resize(1);
    EXPECT_FALSE(backend.ReadModelSkeleton(1, skeleton));
    EXPECT_TRUE(skeleton.Bones.empty());
    EXPECT_EQ(backend.AddModelClips(1, "clips.gltf"), -1);
    std::vector<BRITE::MeshData> meshes(1);
    EXPECT_FALSE(backend.ReadPosedModelMeshes(1, BRITE::ModelPose{}, meshes));
    EXPECT_TRUE(meshes.empty());
}

// A POSED SKINNED CASTER casts the shadow of its pose (RenderPass.hpp, ShadowSettings): the depth pass skins it as its
// draw does. The arm, posed MovedAndTurned, in front of a lit wall at z = -1, under a sun travelling (0.5, 0, -1) -- so
// its shadow falls on the wall half a unit to the right of the arm -- draws the same picture, shadow and all, as a
// plain model of exactly the posed geometry (ReadPosedModelMeshes) drawn the same way; at rest it draws another.
// Mutation: the depth pass does not pose its casters -> the skinned arm casts its rest shadow, red.
TEST_F(SkeletalAnimationTest, APosedSkinnedModelCastsTheShadowOfItsPose) {
    const BRITE::ModelHandle arm = Arm();
    std::vector<BRITE::MeshData> posed;
    ASSERT_TRUE(m_backend.ReadPosedModelMeshes(arm, MovedAndTurned(), posed));
    ASSERT_EQ(posed.size(), 1u);
    const BRITE::ModelHandle still = m_backend.LoadModelFromMesh(posed[0]);
    ASSERT_NE(still, BRITE::NullModelHandle);
    m_models.push_back(still);
    BRITE::MeshData wallMesh;
    wallMesh.Positions = {{-6, -4, -1}, {6, -4, -1}, {6, 6, -1}, {-6, 6, -1}};
    wallMesh.Normals.assign(4, {0, 0, 1});
    wallMesh.Indices = {0, 1, 2, 0, 2, 3};
    const BRITE::ModelHandle wall = m_backend.LoadModelFromMesh(wallMesh);
    m_models.push_back(wall);

    auto shadowed = [&](BRITE::ModelHandle model, const BRITE::ModelPose& pose) {
        BRITE::Camera3D camera{
            {0.25f, 1.0f, 5.0f}, {0.25f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 8.0f, BRITE::CameraProjection::Orthographic};
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
        BRITE::RenderPass pass;
        pass.TargetFramebuffer = target;
        pass.Camera3DPtr = &camera;
        BRITE::Light sun;
        sun.Direction = {0.5f, 0.0f, -1.0f};
        pass.Lights = {sun};
        pass.AmbientIntensity = 0.3f;
        pass.Shadows.Enabled = true;
        pass.Shadows.MapSize = 1024;
        pass.Shadows.Distance = 20.0f;
        BRITE::ModelDrawCommand body{};
        body.Model = model;
        body.Material.DoubleSided = true;
        body.Rotation = {0, 0, 0, 1};
        body.Scale = {1, 1, 1};
        body.Pose = pose;
        BRITE::ModelDrawCommand back = body;
        back.Model = wall;
        back.Pose = {};
        pass.ModelCommands = {back, body};
        m_backend.SubmitRenderPass(pass);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        m_backend.UnloadRenderTexture(target);
        return pixels;
    };
    // At rest first: reading the posed meshes above left the pose in the arm's buffers, and the posed draw must find
    // them at rest, so only the depth pass's own posing can put the pose there in time for its shadow.
    const std::vector<BRITE::Color> rest = shadowed(arm, {});
    const std::vector<BRITE::Color> skinned = shadowed(arm, MovedAndTurned());
    const std::vector<BRITE::Color> plain = shadowed(still, {});
    int differing = 0, fromRest = 0;
    for (std::size_t i = 0; i < skinned.size(); ++i) {
        differing += std::abs(skinned[i].r - plain[i].r) > 2 ? 1 : 0;
        fromRest += std::abs(skinned[i].r - rest[i].r) > 2 ? 1 : 0;
    }
    EXPECT_EQ(differing, 0);
    EXPECT_GT(fromRest, 20);
}
