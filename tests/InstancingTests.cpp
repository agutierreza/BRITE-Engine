// Instanced model draws (RenderPass.hpp, InstancedModelDrawCommand), on the GPU under a hidden window
// (HiddenWindowScene.hpp): many copies of one model, one draw per mesh. A copy must draw what a ModelDrawCommand of the
// same model and material draws at its placement; its tint multiplies the material's; it casts per copy; and a
// translucent mesh's copies are drawn farthest first. Every expected value is worked by hand above its assertion, or
// is the picture the same scene draws as model draws. Beside each case is the mutation that turns it red.

#include "HiddenWindowScene.hpp"

#include <cmath>

using HiddenWindow::SIZE;

namespace {

class Instancing : public HiddenWindow::Scene {
  protected:
    // THE SHADOW SCENE (CopiesCastAndEachCascadeDrawsOnlyThoseThatReachIt): an orthographic camera 10 above the origin
    // looking down, the top of the picture toward -z, 4 across; a sun travelling (1, -1, 0); an ambient of 0.25; one
    // cascade 20 deep. A flat square of half-size `half` in the plane y = 0, facing up.
    BRITE::ModelHandle Flat(float half) {
        BRITE::MeshData flat;
        flat.Positions = {{-half, 0, half}, {half, 0, half}, {half, 0, -half}, {-half, 0, -half}};
        flat.Normals.assign(4, {0, 1, 0});
        flat.Indices = {0, 1, 2, 0, 2, 3};
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(flat);
        m_models.push_back(model);
        return model;
    }
    static BRITE::ModelDrawCommand Lit(BRITE::ModelHandle model) {
        BRITE::ModelDrawCommand draw = Unlit(model, {255, 255, 255, 255});
        draw.Material.Unlit = false;
        draw.Material.DoubleSided = false;
        return draw;
    }
    std::vector<float> Shade(std::vector<BRITE::ModelDrawCommand> models,
                             std::vector<BRITE::InstancedModelDrawCommand> instanced, int& casterDraws) {
        BRITE::RenderPass pass;
        pass.Post.Hdr = true;
        pass.Post.Samples = 1;
        BRITE::Light sun;
        sun.Direction = {1.0f, -1.0f, 0.0f};
        pass.Lights = {sun};
        pass.AmbientColor = {255, 255, 255, 255};
        pass.AmbientIntensity = 0.25f;
        pass.Shadows.Enabled = true;
        pass.Shadows.MapSize = 512;
        pass.Shadows.Cascades = 1;
        pass.Shadows.Distance = 20.0f;
        pass.ModelCommands = std::move(models);
        pass.InstancedModelCommands = std::move(instanced);
        BRITE::Camera3D camera{{0, 10, 0}, {0, 0, 0}, {0, 0, -1}, 4.0f, BRITE::CameraProjection::Orthographic};
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
        pass.TargetFramebuffer = target;
        pass.Camera3DPtr = &camera;
        m_backend.SubmitRenderPass(pass);
        m_backend.UnloadRenderTexture(target);
        const auto& cascades = m_backend.LastShadowCascades();
        casterDraws = cascades.empty() ? -1 : cascades.front().CasterDraws;
        return LinearScene();
    }
    // The linear light of channel `channel` on the ground at (x, z): column (x + 2) x 16, row (z + 2) x 16 from the
    // top.
    static float Ground(const std::vector<float>& linear, float x, float z, int channel = 0) {
        const int column = static_cast<int>((x + 2.0f) * 16.0f);
        const int row = SIZE - 1 - static_cast<int>((z + 2.0f) * 16.0f);
        return linear[static_cast<std::size_t>(row * SIZE + column) * 4 + static_cast<std::size_t>(channel)];
    }
    /// An octahedron with corners h out along each axis, flat-shaded, each face wound both ways.
    BRITE::ModelHandle Octahedron(float h) {
        BRITE::MeshData mesh;
        for (float sx : {-1.0f, 1.0f})
            for (float sy : {-1.0f, 1.0f})
                for (float sz : {-1.0f, 1.0f}) {
                    const float n = 1.0f / std::sqrt(3.0f);
                    const std::uint32_t base = static_cast<std::uint32_t>(mesh.Positions.size());
                    mesh.Positions.push_back({sx * h, 0, 0});
                    mesh.Positions.push_back({0, sy * h, 0});
                    mesh.Positions.push_back({0, 0, sz * h});
                    for (int k = 0; k < 3; ++k)
                        mesh.Normals.push_back({sx * n, sy * n, sz * n});
                    mesh.Indices.insert(mesh.Indices.end(), {base, base + 1, base + 2, base, base + 2, base + 1});
                }
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
        EXPECT_NE(model, BRITE::NullModelHandle);
        m_models.push_back(model);
        return model;
    }
    static BRITE::ModelInstance Copy(BRITE::Vector3 position, BRITE::Quaternion rotation = {0, 0, 0, 1},
                                     BRITE::Vector3 scale = {1, 1, 1}, BRITE::Color tint = {255, 255, 255, 255}) {
        BRITE::ModelInstance copy;
        copy.Position = position;
        copy.Rotation = rotation;
        copy.Scale = scale;
        copy.Tint = tint;
        return copy;
    }
    static BRITE::InstancedModelDrawCommand Instanced(const BRITE::ModelDrawCommand& like,
                                                      std::vector<BRITE::ModelInstance> copies) {
        BRITE::InstancedModelDrawCommand draw;
        draw.Model = like.Model;
        draw.Material = like.Material;
        draw.CastsShadow = like.CastsShadow;
        draw.Instances = std::move(copies);
        return draw;
    }
    // The model draw of `like` that a copy stands for: its placement, and its tint times the material's.
    static BRITE::ModelDrawCommand AsModelDraw(const BRITE::ModelDrawCommand& like, const BRITE::ModelInstance& copy) {
        BRITE::ModelDrawCommand draw = like;
        draw.Position = copy.Position;
        draw.Rotation = copy.Rotation;
        draw.Scale = copy.Scale;
        auto times = [](unsigned char a, unsigned char b) {
            return static_cast<unsigned char>((static_cast<int>(a) * static_cast<int>(b)) / 255);
        };
        draw.Material.AlbedoTint = {
            times(like.Material.AlbedoTint.r, copy.Tint.r), times(like.Material.AlbedoTint.g, copy.Tint.g),
            times(like.Material.AlbedoTint.b, copy.Tint.b), times(like.Material.AlbedoTint.a, copy.Tint.a)};
        return draw;
    }
};

// Rotations: 30 degrees about z, (0, 0, sin 15, cos 15); 50 degrees about (1, 1, 0)/sqrt 2, (0.707107 sin 25, ...,
// cos 25) with sin 25 = 0.422618 and cos 25 = 0.906308.
constexpr BRITE::Quaternion ABOUT_Z_30 = {0.0f, 0.0f, 0.258819f, 0.965926f};
constexpr BRITE::Quaternion TILTED_50 = {0.298836f, 0.298836f, 0.0f, 0.906308f};

} // namespace

// A COPY STANDS WHERE ITS PLACEMENT PUTS IT, IN ITS TINT. An unlit white box 0.5 across, two copies: at (1, 0.5) tinted
// (128, 255, 64), and at (-1, -0.5) tinted (255, 0, 0). Unlit, a copy shows its colour as authored -- white times its
// tint -- so the picture holds (128, 255, 64) at world (1, 0.5) and (255, 0, 0) at (-1, -0.5), and the clear colour,
// black, at the origin, where the model itself stands. Both copies are one draw: the box is one mesh.
// Mutations: the tint not read (pbr.vs) -> white at both, red; the instanced vertex stage never chosen -> the copies
//   drawn at the model's own place, the origin, red.
TEST_F(Instancing, ACopyStandsWhereItsPlacementPutsItInItsTint) {
    const BRITE::ModelDrawCommand box = Unlit(Box(0, 0, 0, 0.25f), {255, 255, 255, 255});
    BRITE::RenderPass pass;
    pass.InstancedModelCommands = {
        Instanced(box, {Copy({1.0f, 0.5f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}, {128, 255, 64, 255}),
                        Copy({-1.0f, -0.5f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}, {255, 0, 0, 255})})};
    const std::vector<BRITE::Color> pixels = Picture(pass);
    EXPECT_TRUE(HiddenWindow::Within(pixels[HiddenWindow::At(1.0f, 0.5f)], 128, 255, 64, 1));
    EXPECT_TRUE(HiddenWindow::Within(pixels[HiddenWindow::At(-1.0f, -0.5f)], 255, 0, 0, 1));
    EXPECT_TRUE(HiddenWindow::Within(pixels[HiddenWindow::At(0.0f, 0.0f)], 0, 0, 0, 0));
    EXPECT_EQ(m_backend.LastPassModelDrawCalls(), 1);
}

// COPIES DRAW AS MODEL DRAWS WOULD, lit, in 8 bits and under HDR. Four copies of a lit octahedron -- its faces' normals
// oblique, (+-1, +-1, +-1)/sqrt 3, so an uneven scale bends them, and only the inverse-transpose bends them right --
// under a sun and an ambient: rotated, scaled unevenly, one MIRRORED (a negative scale) and tinted, drawn once as one
// instanced draw and once as four model draws of the same placements and tints. Every pixel away from an edge (its
// eight neighbours all within 4 of it in the model draws' picture, so a rounding at a triangle's edge cannot flip it)
// must match within 2, and at least 200 such pixels lie on the shapes (a face's highlight moves a step or two a pixel;
// an edge, far more). The instanced pass takes one draw call, the model draws four. Mutations: the normal matrix taken
// as the placement's 3x3 (no cofactor) -> the unevenly scaled copies' faces lit
//   along the wrong normals, red; the cofactor's sign dropped -> the mirrored copy's normals point in, red.
TEST_F(Instancing, CopiesDrawAsModelDrawsWould) {
    BRITE::ModelDrawCommand box = Unlit(Octahedron(0.6f), {255, 255, 255, 255});
    box.Material.Unlit = false;
    box.Material.DoubleSided = false;
    const std::vector<BRITE::ModelInstance> copies = {
        Copy({-1.2f, 1.0f, 0.0f}, ABOUT_Z_30, {1.5f, 0.6f, 1.0f}),
        Copy({0.8f, 1.1f, 0.0f}, TILTED_50, {1.0f, 1.0f, 1.0f}, {255, 200, 120, 255}),
        Copy({-0.9f, -1.0f, 0.0f}, ABOUT_Z_30, {-1.0f, 1.0f, 1.0f}, {120, 220, 255, 255}),
        Copy({1.0f, -0.9f, 0.0f}, TILTED_50, {0.7f, 1.8f, 0.5f}),
    };
    for (const bool hdr : {false, true}) {
        auto lit = [&](BRITE::RenderPass& pass) {
            pass.Post.Hdr = hdr;
            pass.Post.Samples = 1;
            BRITE::Light sun;
            sun.Direction = {0.3f, -0.5f, -0.8f};
            pass.Lights = {sun};
            pass.AmbientColor = {255, 255, 255, 255};
            pass.AmbientIntensity = 0.2f;
        };
        BRITE::RenderPass models;
        lit(models);
        for (const BRITE::ModelInstance& copy : copies)
            models.ModelCommands.push_back(AsModelDraw(box, copy));
        const std::vector<BRITE::Color> expected = Picture(models);
        EXPECT_EQ(m_backend.LastPassModelDrawCalls(), 4);
        BRITE::RenderPass instanced;
        lit(instanced);
        instanced.InstancedModelCommands = {Instanced(box, copies)};
        const std::vector<BRITE::Color> actual = Picture(instanced);
        EXPECT_EQ(m_backend.LastPassModelDrawCalls(), 1);

        int compared = 0, differing = 0;
        for (int row = 1; row < SIZE - 1; ++row)
            for (int column = 1; column < SIZE - 1; ++column) {
                const BRITE::Color c = expected[static_cast<std::size_t>(row * SIZE + column)];
                bool interior = c.r != 0 || c.g != 0 || c.b != 0; // on a box, not the black clear colour
                for (int dy = -1; dy <= 1 && interior; ++dy)
                    for (int dx = -1; dx <= 1 && interior; ++dx)
                        interior = HiddenWindow::Within(
                            expected[static_cast<std::size_t>((row + dy) * SIZE + column + dx)], c.r, c.g, c.b, 4);
                if (!interior)
                    continue;
                ++compared;
                const BRITE::Color a = actual[static_cast<std::size_t>(row * SIZE + column)];
                if (!HiddenWindow::Within(a, c.r, c.g, c.b, 2)) {
                    if (++differing <= 5)
                        ADD_FAILURE() << (hdr ? "HDR" : "8-bit") << " at column " << column << ", row " << row
                                      << ": model draws (" << int(c.r) << ", " << int(c.g) << ", " << int(c.b)
                                      << "), instanced (" << int(a.r) << ", " << int(a.g) << ", " << int(a.b) << ")";
                }
            }
        EXPECT_GE(compared, 200) << (hdr ? "HDR" : "8-bit");
        EXPECT_EQ(differing, 0) << (hdr ? "HDR" : "8-bit");
    }
}

// TRANSLUCENT COPIES ARE DRAWN FARTHEST FIRST; a layer's in the order listed. Two copies of an unlit white quad over
// the same pixels, listed nearest first: red at z = 1, blue at z = 0, each at alpha 128 -- a = 128/255 = 0.501961. The
// camera looks down -z from z = 5, so blue is the farther. Over the black clear colour, farthest first: blue
// 1 x a = 0.501961, 128; then red over it, red 0.501961 -> 128, blue 0.501961 x (1 - a) = 0.250000 -> 63.75, 64:
// (128, 0, 64). As a layer (DepthWrite off), in the order listed, the other way round: (64, 0, 128).
// Mutations: the sorted copies drawn in the order listed -> (64, 0, 128) for the sorted draw, red; the faintest copy's
//   alpha not read -> the mesh drawn opaque, the nearer copy hiding the farther with its depth: (128, 0, 0), red.
TEST_F(Instancing, TranslucentCopiesAreDrawnFarthestFirst) {
    const BRITE::ModelDrawCommand quad = Unlit(Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f), {255, 255, 255, 255});
    const std::vector<BRITE::ModelInstance> copies = {
        Copy({0.0f, 0.0f, 1.0f}, {0, 0, 0, 1}, {1, 1, 1}, {255, 0, 0, 128}),
        Copy({0.0f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}, {0, 0, 255, 128}),
    };
    BRITE::RenderPass sorted;
    sorted.InstancedModelCommands = {Instanced(quad, copies)};
    EXPECT_TRUE(HiddenWindow::Within(Picture(sorted)[HiddenWindow::At(0.0f, 0.0f)], 128, 0, 64, 1));

    BRITE::ModelDrawCommand layer = quad;
    layer.Material.DepthWrite = false;
    BRITE::RenderPass layered;
    layered.InstancedModelCommands = {Instanced(layer, copies)};
    EXPECT_TRUE(HiddenWindow::Within(Picture(layered)[HiddenWindow::At(0.0f, 0.0f)], 64, 0, 128, 1));
}

// COPIES CAST, AND EACH CASCADE DRAWS ONLY THE COPIES THAT REACH IT. An orthographic camera 10 above the origin looks
// down at a lit white ground (y = 0, x and z -3..3); a sun travels (1, -1, 0); one cascade, 20 deep. The caster is a
// copy of a sheet 1 across, high above the camera at (-14, 15, 0): it is never in the picture, but the sun throws its
// shadow 15 along +x, onto the ground at x 0.5..1.5 -- so the ground at (1, 0) is lit by the ambient alone, 0.25, and
// at (-1.5, 0) also by the sun, at least 0.45 (the sun-shadow cases' figures, SunShadowTests.cpp). The sheet lies about
// 20.5 nearer the sun than the origin, beyond the cascade's slice of the view (radius sqrt(10^2 + 2^2 + 2^2) = 10.4):
// the cascade's box is stretched toward the sun to take it in, as it is for a model draw. Two draws reach the cascade:
// the ground and the copies' one.
// Then the copy moved to (60, 15, 0), far beyond the cascade: not drawn into it -- one draw, the ground's -- and the
// ground at (1, 0) in the sun. And a copy at the first place but faint (alpha 128): a translucent mesh casts nothing.
// Last, the ground itself drawn as one instanced copy at the origin: a copy RECEIVES the shadow as a model draw does,
// the same 0.25 at (1, 0).
// Mutations: instanced casters not drawn -> the ground at (1, 0) in the sun, red; the box not stretched toward the sun
//   for them -> the sheet clipped, the same, red; every copy drawn into every cascade -> two draws for the far copy,
//   red; the faintest copy's alpha not read -> the faint sheet casts, red.
TEST_F(Instancing, CopiesCastAndEachCascadeDrawsOnlyThoseThatReachIt) {
    const BRITE::ModelHandle sheet = Flat(0.5f);
    const BRITE::ModelHandle ground = Flat(3.0f);
    int draws = 0;
    std::vector<float> linear = Shade({Lit(ground)}, {Instanced(Lit(sheet), {Copy({-14.0f, 15.0f, 0.0f})})}, draws);
    EXPECT_NEAR(Ground(linear, 1.0f, 0.0f), 0.25f, 0.01f) << "the copy's shadow";
    EXPECT_GT(Ground(linear, -1.5f, 0.0f), 0.45f);
    EXPECT_EQ(draws, 2);

    linear = Shade({Lit(ground)}, {Instanced(Lit(sheet), {Copy({60.0f, 15.0f, 0.0f})})}, draws);
    EXPECT_GT(Ground(linear, 1.0f, 0.0f), 0.45f) << "a copy beyond the cascade casts nothing into it";
    EXPECT_EQ(draws, 1);

    linear = Shade(
        {Lit(ground)},
        {Instanced(Lit(sheet), {Copy({-14.0f, 15.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}, {255, 255, 255, 128})})}, draws);
    EXPECT_GT(Ground(linear, 1.0f, 0.0f), 0.45f) << "a faint copy is translucent, and casts nothing";
    EXPECT_EQ(draws, 1);

    // And a copy RECEIVES a shadow as a model draw does: the ground drawn as one instanced copy, under the first sheet.
    linear = Shade(
        {}, {Instanced(Lit(ground), {Copy({0.0f, 0.0f, 0.0f})}), Instanced(Lit(sheet), {Copy({-14.0f, 15.0f, 0.0f})})},
        draws);
    EXPECT_NEAR(Ground(linear, 1.0f, 0.0f), 0.25f, 0.01f) << "an instanced ground in the copy's shadow";
    EXPECT_GT(Ground(linear, -1.5f, 0.0f), 0.45f);
    EXPECT_EQ(draws, 2);
}

// A REFUSED INSTANCED VARIANT LEAVES THE OTHER DRAWS THEIR SHADOWS. On a device that refuses the lit shader's instanced
// variant with shadows (the test seam RefuseInstancedShadowsForTest makes it fail to compile), the shadow scene with
// the sheet as a MODEL draw and a red lit box 0.6 across as one instanced copy at (-1.5, 0.3, -1): the copy is drawn
// without shadows -- its top red, no green (a red albedo reflects none) -- and the ground still takes the sheet's
// shadow, 0.25 at (1, 0). In the pass after, with no instanced draw at all, the same shadow: the refusal was the
// instanced variant's, not the shadows'.
// Mutations: the refusal recorded as the shadows' whatever the variant -> no shadow in the next pass, the ground at
//   (1, 0) in the sun, red; no fallback for the refused instanced variant -> the copy skipped, the white ground in its
//   place, green, red.
TEST_F(Instancing, ARefusedInstancedVariantLeavesTheOtherDrawsTheirShadows) {
    m_backend.RefuseInstancedShadowsForTest();
    const BRITE::ModelHandle ground = Flat(3.0f);
    BRITE::ModelDrawCommand sheet = Lit(Flat(0.5f));
    sheet.Position = {-14.0f, 15.0f, 0.0f};
    BRITE::ModelDrawCommand redBox = Lit(Box(0, 0, 0, 0.3f));
    redBox.Material.AlbedoTint = {255, 0, 0, 255};
    int draws = 0;
    std::vector<float> linear = Shade({Lit(ground), sheet}, {Instanced(redBox, {Copy({-1.5f, 0.3f, -1.0f})})}, draws);
    EXPECT_NEAR(Ground(linear, 1.0f, 0.0f), 0.25f, 0.01f);
    EXPECT_GT(Ground(linear, -1.5f, -1.0f, 0), 0.3f) << "the copy, drawn without shadows";
    EXPECT_LT(Ground(linear, -1.5f, -1.0f, 1), 0.05f) << "the copy, drawn without shadows";

    linear = Shade({Lit(ground), sheet}, {}, draws);
    EXPECT_NEAR(Ground(linear, 1.0f, 0.0f), 0.25f, 0.01f) << "the next pass keeps its shadows";
}

// AN INSTANCED DRAW WITH NO COPIES DRAWS NOTHING, and a pass of nothing but instanced draws needs no model draw to
// choose its shader: the black clear colour everywhere, no draw call.
// Mutation: the empty-copies check dropped from drawInstanced -> one draw call counted, red.
TEST_F(Instancing, NoCopiesDrawNothing) {
    BRITE::RenderPass pass;
    pass.InstancedModelCommands = {Instanced(Unlit(Box(0, 0, 0, 0.25f), {255, 255, 255, 255}), {})};
    const std::vector<BRITE::Color> pixels = Picture(pass);
    EXPECT_TRUE(HiddenWindow::Within(pixels[HiddenWindow::At(0.0f, 0.0f)], 0, 0, 0, 0));
    EXPECT_EQ(m_backend.LastPassModelDrawCalls(), 0);
}
