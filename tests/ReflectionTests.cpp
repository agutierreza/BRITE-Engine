// Reflections (RenderPass.hpp, ReflectionSettings; PBRMaterial::Reflection), on the GPU under a hidden window
// (HiddenWindowScene.hpp). A reflecting surface here is LIT but black, under no light and no ambient, so all it shows
// is its reflection times Schlick's Fresnel, F = F0 + (1 - F0)(1 - N.V)^5. Every expected value is worked by hand above
// its assertion. Beside each case is the mutation that turns it red.

#include "HiddenWindowScene.hpp"
#include "ScratchFolder.hpp"

#include <cmath>

using HiddenWindow::SIZE;

namespace {

class Reflection : public HiddenWindow::Scene {
  protected:
    static BRITE::ModelDrawCommand Mirror(BRITE::ModelHandle model, BRITE::ReflectionMode mode) {
        BRITE::ModelDrawCommand draw = Unlit(model, {0, 0, 0, 255});
        draw.Material.Unlit = false;
        draw.Material.DoubleSided = false;
        draw.Material.Reflection = mode;
        draw.Material.ReflectionF0 = 0.02f;
        draw.Material.ReflectionDistortion = 0.0f;
        return draw;
    }
    static float Red(const std::vector<float>& linear, int column, int row) {
        return linear[static_cast<std::size_t>((SIZE - 1 - row) * SIZE + column) * 4];
    }

    // A square of water 20 across in the plane y = `height`, its normals `normal` (straight up unless given).
    BRITE::ModelHandle Water(float height, BRITE::Vector3 normal = {0, 1, 0}) {
        BRITE::MeshData water;
        water.Positions = {{-10, height, 10}, {10, height, 10}, {10, height, -10}, {-10, height, -10}};
        water.Normals.assign(4, normal);
        water.Indices = {0, 1, 2, 0, 2, 3};
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(water);
        m_models.push_back(model);
        return model;
    }
    // A cube 1 across at (0, 1, -3), seen from any side.
    BRITE::ModelHandle Cube() {
        return Box(0.0f, 1.0f, -3.0f, 0.5f);
    }
    // The water scene's camera: at (0, 2, 6), looking at the origin, 45 degrees.
    static BRITE::Camera3D WaterCamera() {
        return {{0, 2, 6}, {0, 0, 0}, {0, 1, 0}, 45.0f, BRITE::CameraProjection::Perspective};
    }
    // `pass` drawn under the water camera into a fresh SIZE x SIZE target, read back top row first.
    std::vector<BRITE::Color> WaterPicture(BRITE::RenderPass& pass, int size = SIZE) {
        BRITE::Camera3D camera = WaterCamera();
        const BRITE::TextureHandle target = m_backend.LoadRenderTexture(size, size);
        pass.TargetFramebuffer = target;
        pass.Camera3DPtr = &camera;
        m_backend.SubmitRenderPass(pass);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        m_backend.UnloadRenderTexture(target);
        pass.Camera3DPtr = nullptr;
        return pixels;
    }
    // A planar-reflection pass over the water scene: a reflecting black lake, a red unlit cube, a blue sky.
    BRITE::RenderPass PlanarPass(BRITE::ModelHandle water, bool hdr) {
        BRITE::RenderPass pass;
        pass.Post.Hdr = hdr;
        pass.Post.Samples = 1;
        pass.ClearColor = {0, 0, 255, 255};
        pass.Reflections.Planar = true;
        pass.Reflections.PlaneHeight = 0.0f;
        pass.Reflections.Resolution = 0.5f;
        pass.ModelCommands = {Mirror(water, BRITE::ReflectionMode::Planar), Unlit(Cube(), {255, 0, 0, 255})};
        return pass;
    }
};

} // namespace

// THE SKY, by its Fresnel. A black quad facing the fixture's camera, reflecting a white sky, read at column 31, row 32:
// x = y = -0.03125, so V = normalize(0.03125, 0.03125, 5) -- the lit shader takes the view from the camera's position.
//   Head on, N = (0, 0, 1): N.V = 5 / 5.000195 = 0.999961, (1 - N.V)^5 is 1e-22, so F = F0 = 0.02, and white, 7.241658
//   of light under HDR (ToneCurve), gives 0.02 x 7.241658 = 0.144833.
//   Turned 60 degrees, N = (sin 60, 0, cos 60) = (0.866025, 0, 0.5): N.V = (0.866025 x 0.03125 + 0.5 x 5) / 5.000195
//   = 0.505393; (1 - 0.505393)^5 = 0.494607^5 = 0.029601; F = 0.02 + 0.98 x 0.029601 = 0.049009; x 7.241658 = 0.354905.
// In an 8-bit pass the reflection is mixed over the finished colour (black) as authored: 0.049009 x 255 = 12.5.
// Mutation: the Fresnel's power 5 -> 4 -> (0.494607)^4 = 0.059847, F = 0.078650: 0.5696 and 20.1, red.
TEST_F(Reflection, TheSkyByItsFresnel) {
    const float s = std::sin(BRITE::Pi / 3.0f), c = std::cos(BRITE::Pi / 3.0f);
    const BRITE::ModelHandle facing = Quad(-3, -3, 3, 3, 0);
    const BRITE::ModelHandle turned = Quad(-3, -3, 3, 3, 0, {{s, 0, c}, {s, 0, c}, {s, 0, c}, {s, 0, c}});
    for (const auto& [model, expected] : {std::pair{facing, 0.144833f}, std::pair{turned, 0.354905f}}) {
        BRITE::RenderPass pass;
        pass.Post.Hdr = true;
        pass.Post.Samples = 1;
        pass.Reflections.SkyColor = {255, 255, 255, 255};
        pass.ModelCommands = {Mirror(model, BRITE::ReflectionMode::Sky)};
        Picture(pass);
        EXPECT_NEAR(Red(LinearScene(), 31, 32), expected, 2e-3f);
    }
    BRITE::RenderPass eightBit;
    eightBit.Reflections.SkyColor = {255, 255, 255, 255};
    eightBit.ModelCommands = {Mirror(turned, BRITE::ReflectionMode::Sky)};
    const std::vector<BRITE::Color> pixels = Picture(eightBit);
    EXPECT_NEAR(static_cast<float>(pixels[static_cast<std::size_t>(32 * SIZE + 31)].r), 12.5f, 1.0f);
}

// A PLANAR REFLECTION. A perspective camera at (0, 2, 6) looks at the origin over calm black water in the plane y = 0;
// a red unlit cube 1 across floats at (0, 1, -3); a green lake bed lies at y = -0.5, under the water and hidden by it;
// the sky is blue. The camera mirrored in the water sits at (0, -2, 6), and the line from it to the cube's centre
// crosses the water at t = 2/3: (0, 0, 6 - 9 x 2/3) = (0, 0, 0), the very point the camera looks at. So the middle of
// the picture (column 31, row 31) shows the cube's reflection: red 7.241658 of light times F, where N = (0, 1, 0) and
// V = (0, 2, 6) / 6.324555, N.V = 0.316228, (1 - 0.316228)^5 = 0.683772^5 = 0.149470, F = 0.02 + 0.98 x 0.149470 =
// 0.166481: 1.2056 of red, and neither green nor blue. Half a pixel off the middle moves N.V by a fraction of a
// percent; the tolerance is 5%. Mutation: no oblique near plane (the mirrored camera's own projection) -> the lake bed,
// between the mirrored camera
//   and the water, hides the cube: green, red.
TEST_F(Reflection, APlanarReflectionShowsWhatStandsAboveTheWaterAndNothingBelow) {
    const BRITE::ModelHandle water = Water(0.0f);
    BRITE::RenderPass pass = PlanarPass(water, true);
    pass.ModelCommands.push_back(Unlit(Water(-0.5f), {0, 255, 0, 255})); // the lake bed
    WaterPicture(pass);
    const std::vector<float> linear = LinearScene();
    const std::size_t middle = static_cast<std::size_t>((SIZE - 1 - 31) * SIZE + 31) * 4;
    EXPECT_NEAR(linear[middle + 0], 1.2056f, 0.06f);
    EXPECT_NEAR(linear[middle + 1], 0.0f, 0.01f);
    EXPECT_NEAR(linear[middle + 2], 0.0f, 0.01f);
    int width = 0, height = 0;
    std::vector<float> reflection;
    ASSERT_TRUE(m_backend.ReadLastPlanarReflection(width, height, reflection));
    EXPECT_EQ(width, SIZE / 2);
    EXPECT_EQ(height, SIZE / 2);
}

// UNDER HDR THE REFLECTION HOLDS LIGHT, and is read as light, not as a colour run through the curve again. The planar
// scene with the cube a mid red, (128, 0, 0): 128/255 = 0.501961 is the light 0.501961^2.2 = 0.219520 on the display,
// so the cube is drawn as InverseAcesFit(0.219520) -- the root of (2.51 - 2.43 x 0.219520) x^2 + (0.03 - 0.59 x
// 0.219520) x - 0.14 x 0.219520 = 1.976567 x^2 - 0.099517 x - 0.030733 = 0, x = (0.099517 + sqrt(0.009904 + 0.242983))
// / 3.953134 = 0.152384 -- of red light, and the water shows it times F = 0.166481: 0.025369.
// Mutation: the planar map taken through AuthoredToLinear as the sky is -> AuthoredToLinear(0.152384) = 0.026170,
//   times F 0.004357, red. (A full red cube cannot tell the two apart: its 7.241658 of light is held at white by the
//   curve and comes back as 7.241658.)
TEST_F(Reflection, UnderHdrTheReflectionIsReadAsLight) {
    const BRITE::ModelHandle water = Water(0.0f);
    BRITE::RenderPass pass = PlanarPass(water, true);
    pass.ModelCommands[1].Material.AlbedoTint = {128, 0, 0, 255};
    WaterPicture(pass);
    const std::vector<float> linear = LinearScene();
    const std::size_t middle = static_cast<std::size_t>((SIZE - 1 - 31) * SIZE + 31) * 4;
    EXPECT_NEAR(linear[middle + 0], 0.025369f, 0.0015f);
}

// A ROLLED CAMERA'S REFLECTION REACHES THE PICTURE'S CORNERS: the mirrored camera mirrors its up as well. The water
// camera rolled 30 degrees about its view: forward f = (0, -2, -6)/6.324555, right r = (1, 0, 0), level up
// u0 = r x f = (0, 0.948683, -0.316228), rolled up cos 30 u0 + sin 30 r = (0.5, 0.821584, -0.273861). Near the
// picture's bottom-left corner, column 2, row 60 (ndc -0.921875, -0.890625), the view ray runs
// d = (-0.454993, -0.387020, -0.801996) and meets the water at P = (0, 2, 6) + (2/0.387020) d = (-2.351266,
// 0, 1.855530). A red unlit cube 0.4 across is put 3 along the reflected ray, d with its y turned up: at
// (-3.716246, 1.161059, -0.550457). Mirrored with its up, the mirror camera sees P exactly where the real one does,
// mirrored across: ndc (0.921875, -0.890625), inside its view. With the up left as it was, it would see P at
// (-1.190109, -0.476137) -- outside its view by 0.19, more than the cube 8.17 away reaches (0.2 sqrt 3 / (8.17
// tan 22.5) = 0.10), so the read, clamped to the reflection's edge, finds no cube: the sky. At P, V = ((0, 2, 6) -
// P)/|...| has y 0.387020: (1 - 0.387020)^5 = 0.086543, F = 0.02 + 0.98 x 0.086543 = 0.104812, and the red 7.241658 of
// light times F is 0.759015 (5%: the sample is the cube's middle, so a texel either way is still red). Mutation: the up
// not mirrored -> the sky's blue at that pixel and no red, red.
TEST_F(Reflection, ARolledCamerasReflectionReachesTheCorners) {
    const BRITE::ModelHandle water = Water(0.0f);
    BRITE::RenderPass pass;
    pass.Post.Hdr = true;
    pass.Post.Samples = 1;
    pass.ClearColor = {0, 0, 255, 255};
    pass.Reflections.Planar = true;
    pass.Reflections.Resolution = 1.0f;
    pass.ModelCommands = {Mirror(water, BRITE::ReflectionMode::Planar),
                          Unlit(Box(-3.716246f, 1.161059f, -0.550457f, 0.2f), {255, 0, 0, 255})};
    BRITE::Camera3D camera{
        {0, 2, 6}, {0, 0, 0}, {0.5f, 0.821584f, -0.273861f}, 45.0f, BRITE::CameraProjection::Perspective};
    const BRITE::TextureHandle target = m_backend.LoadRenderTexture(SIZE, SIZE);
    pass.TargetFramebuffer = target;
    pass.Camera3DPtr = &camera;
    m_backend.SubmitRenderPass(pass);
    m_backend.UnloadRenderTexture(target);
    const std::vector<float> linear = LinearScene();
    const std::size_t corner = static_cast<std::size_t>((SIZE - 1 - 60) * SIZE + 2) * 4;
    EXPECT_NEAR(linear[corner + 0], 0.759015f, 0.038f);
    EXPECT_NEAR(linear[corner + 2], 0.0f, 0.01f);
}

// THE SKY TIER'S ENVIRONMENT MAP IS LIGHT, as the lit shader's ambient reads it. The first case's black quad, head on,
// under a pass whose environment (irradiance and prefilter) is one white texel -- light 1, read as it is stored. The
// quad's own lit colour is then not black: roughness 1 and metal 0 leave the ambient's specular, the map times
// F x A + B, where F = F0 = 0.04 (Schlick's with roughness 1 adds nothing) and EnvBRDFApprox(1, N.V) gives A = 0.4524,
// B = -0.0024 (r = c0 + c1 = (0, 0.015, 0.468, -0.018); a004 = min(0, ...) x 0 + 0.015; A = -1.04 x 0.015 + 0.468,
// B = 1.04 x 0.015 - 0.018): 0.04 x 0.4524 - 0.0024 = 0.015696. Its diffuse is the map times a black albedo, 0. The
// reflection, F = 0.02 head on, mixes the map's light over it: 0.98 x 0.015696 + 0.02 x 1 = 0.035382.
// Mutation: the map read as a colour authored as the screen shows it -> AuthoredToLinear(1) = 7.241658, mixed:
//   0.015382 + 0.144833 = 0.160215, red.
TEST_F(Reflection, TheSkyTiersEnvironmentMapIsLight) {
    BRITE::Test::ScratchFolder folder("reflection_environment");
    const std::string path = (folder.path / "white.png").string();
    ::Image image = ::GenImageColor(1, 1, {255, 255, 255, 255});
    ::ExportImage(image, path.c_str());
    ::UnloadImage(image);
    const BRITE::TextureHandle white = m_backend.LoadTexture(path.c_str());
    ASSERT_NE(white, BRITE::NullTextureHandle);
    BRITE::RenderPass pass;
    pass.Post.Hdr = true;
    pass.Post.Samples = 1;
    pass.Environment.IrradianceMap = white;
    pass.Environment.PrefilterMap = white;
    pass.ModelCommands = {Mirror(Quad(-3, -3, 3, 3, 0), BRITE::ReflectionMode::Sky)};
    Picture(pass);
    EXPECT_NEAR(Red(LinearScene(), 31, 32), 0.035382f, 1.5e-3f);
    m_backend.UnloadTexture(white);
}

// IN AN 8-BIT PASS THE ENVIRONMENT MAP LIGHTS AND IS REFLECTED THROUGH THE LIT SHADER'S OWN CURVE, irradiance and
// prefilter each from its own map: irradiance one texel of 200, 200/255 = 0.784314 of light -- not white, so it can be
// told apart from raylib's white default texture on unit 0 -- and prefilter one grey texel, 128/255 = 0.501961.
//   A white lit quad that reflects nothing, head on: its ambient is kD x irradiance x albedo + prefilter x (F A + B),
//   with F = F0 = 0.04 at roughness 1, kD = 1 - 0.04 = 0.96, and A x 0.04 + B = 0.015696 (the case above):
//   0.96 x 0.784314 x 1 + 0.501961 x 0.015696 = 0.752941 + 0.007879 = 0.760820. Through Reinhard, 0.760820 / 1.760820 =
//   0.432083, and the gamma, 0.432083^(1/2.2) = 0.682887: 174.1 of 255.
//   A black quad reflecting the sky at a grazing angle (the grazing case's normal, F = 1): all reflection, the
//   prefilter's light through the same curve, (0.501961 / 1.501961)^(1/2.2) = 0.334204^(1/2.2) = 0.607633: 154.9.
// Mutations: the light shown as it is, not through the curve -> 128 for the reflection, red; the irradiance left
//   unbound -> the lit quad's diffuse 0, 28.1, red; irradiance and prefilter swapped -> 154.2 and 175.5, red; the
//   irradiance sampler's unit never set -> it reads unit 0, raylib's white texel: 184.7, red.
TEST_F(Reflection, InAnEightBitPassTheEnvironmentMapLightsThroughTheCurve) {
    BRITE::Test::ScratchFolder folder("reflection_environment_8bit");
    auto texel = [&](const char* name, unsigned char v) {
        const std::string path = (folder.path / name).string();
        ::Image image = ::GenImageColor(1, 1, {v, v, v, 255});
        ::ExportImage(image, path.c_str());
        ::UnloadImage(image);
        return m_backend.LoadTexture(path.c_str());
    };
    const BRITE::TextureHandle irradiance = texel("irradiance.png", 200);
    const BRITE::TextureHandle grey = texel("grey.png", 128);
    ASSERT_NE(irradiance, BRITE::NullTextureHandle);
    ASSERT_NE(grey, BRITE::NullTextureHandle);

    BRITE::RenderPass lit;
    lit.Environment.IrradianceMap = irradiance;
    lit.Environment.PrefilterMap = grey;
    BRITE::ModelDrawCommand quad = Mirror(Quad(-3, -3, 3, 3, 0), BRITE::ReflectionMode::None);
    quad.Material.AlbedoTint = {255, 255, 255, 255};
    lit.ModelCommands = {quad};
    const BRITE::Color shownLit = Picture(lit)[static_cast<std::size_t>(32 * SIZE + 31)];
    EXPECT_NEAR(shownLit.r, 174.1f, 1.0f);

    const float length = std::sqrt(25.0f + 0.03125f * 0.03125f);
    const BRITE::Vector3 grazing = {5.0f / length, 0.0f, -0.03125f / length};
    BRITE::RenderPass reflecting;
    reflecting.Environment.IrradianceMap = irradiance;
    reflecting.Environment.PrefilterMap = grey;
    reflecting.ModelCommands = {
        Mirror(Quad(-3, -3, 3, 3, 0, {grazing, grazing, grazing, grazing}), BRITE::ReflectionMode::Sky)};
    const BRITE::Color shownReflected = Picture(reflecting)[static_cast<std::size_t>(32 * SIZE + 31)];
    EXPECT_NEAR(shownReflected.r, 154.9f, 1.0f);
    m_backend.UnloadTexture(irradiance);
    m_backend.UnloadTexture(grey);
}

// A CAMERA AT OR BELOW THE PLANE SEES ITS UNDERSIDE: no planar reflection is drawn, and a surface that asks for one
// reflects the sky -- the head-on 0.144833 of the first case. The fixture's camera stands at y = 0: the plane at 1 is
// above it, and then a plane at 0 runs through it, exactly at the camera.
// Mutations: the camera's height not checked -> the reflection drawn from below, read at the quad: the black clear
// colour, 0, red; a camera exactly on the plane let through ("<" for "<=") -> the same at the plane through it, red.
TEST_F(Reflection, ACameraBelowThePlaneReflectsTheSky) {
    BRITE::RenderPass pass;
    pass.Post.Hdr = true;
    pass.Post.Samples = 1;
    pass.Reflections.SkyColor = {255, 255, 255, 255};
    pass.Reflections.Planar = true;
    pass.ModelCommands = {Mirror(Quad(-3, -3, 3, 3, 0), BRITE::ReflectionMode::Planar)};
    for (const float plane : {1.0f, 0.0f}) {
        pass.Reflections.PlaneHeight = plane;
        Picture(pass);
        EXPECT_NEAR(Red(LinearScene(), 31, 32), 0.144833f, 2e-3f) << "the plane at " << plane;
        int width = 0, height = 0;
        std::vector<float> reflection;
        EXPECT_FALSE(m_backend.ReadLastPlanarReflection(width, height, reflection)) << "the plane at " << plane;
    }
}

// THE SAME IN AN 8-BIT PASS: the reflection is drawn in the colours the screen shows, into an 8-bit target, and mixed
// over the finished colour. The water is black, so the middle shows the cube's red, 1, times the same F, 0.166481:
// 0.166481 x 255 = 42.45, and neither green nor blue.
// Mutation: the reflection not mixed over the finished colour -> the black water, 0, red.
TEST_F(Reflection, APlanarReflectionInAnEightBitPass) {
    const BRITE::ModelHandle water = Water(0.0f);
    BRITE::RenderPass pass = PlanarPass(water, false);
    pass.ModelCommands.push_back(Unlit(Water(-0.5f), {0, 255, 0, 255}));
    const std::vector<BRITE::Color> pixels = WaterPicture(pass);
    const BRITE::Color middle = pixels[static_cast<std::size_t>(31 * SIZE + 31)];
    EXPECT_NEAR(static_cast<float>(middle.r), 42.45f, 2.5f);
    EXPECT_LE(middle.g, 1);
    EXPECT_LE(middle.b, 1);
}

// A surface that asks for the planar reflection in a pass that draws none reflects the sky instead: the head-on value
// of the first case, 0.144833. (That a pass with no reflecting draw draws as before is the byte-for-byte witness of
// the change, not a case here.)
// Mutation: a Planar material read as no reflection when the pass has none -> 0, red.
TEST_F(Reflection, WithoutAPlaneTheSkyIsReflected) {
    BRITE::RenderPass pass;
    pass.Post.Hdr = true;
    pass.Post.Samples = 1;
    pass.Reflections.SkyColor = {255, 255, 255, 255};
    pass.ModelCommands = {Mirror(Quad(-3, -3, 3, 3, 0), BRITE::ReflectionMode::Planar)};
    Picture(pass);
    EXPECT_NEAR(Red(LinearScene(), 31, 32), 0.144833f, 2e-3f);
}

// AT A GRAZING ANGLE THE SKY SHOWS EXACTLY AS AUTHORED, with or without HDR. The quad faces the camera, but its normal
// is turned so that, at column 31, row 32 -- V = normalize(0.03125, 0.03125, 5) -- N is normalize(5, 0, -0.03125):
// N.V = (5 x 0.03125 - 0.03125 x 5) / ... = 0. So (1 - N.V)^5 = 1 and F = 0.02 + 0.98 = 1: all reflection, and the
// black surface shows the sky (128, 170, 220) itself. Under HDR the sky goes in as the light that shows it
// (AuthoredToLinear) and the tone curve brings it back: within 1 of each channel, the curve's dither. In 8 bits it is
// mixed in as it is. Mutation: the sky under HDR not taken through AuthoredToLinear -> 128/255 = 0.50 of light,
// tone-mapped far darker, red.
TEST_F(Reflection, AtAGrazingAngleTheSkyShowsExactlyAsAuthored) {
    const float length = std::sqrt(25.0f + 0.03125f * 0.03125f);
    const BRITE::Vector3 grazing = {5.0f / length, 0.0f, -0.03125f / length};
    const BRITE::ModelHandle quad = Quad(-3, -3, 3, 3, 0, {grazing, grazing, grazing, grazing});
    for (const bool hdr : {true, false}) {
        BRITE::RenderPass pass;
        pass.Post.Hdr = hdr;
        pass.Post.Samples = 1;
        pass.Reflections.SkyColor = {128, 170, 220, 255};
        pass.ModelCommands = {Mirror(quad, BRITE::ReflectionMode::Sky)};
        const BRITE::Color shown = Picture(pass)[static_cast<std::size_t>(32 * SIZE + 31)];
        EXPECT_NEAR(shown.r, 128, 1) << (hdr ? "HDR" : "8-bit");
        EXPECT_NEAR(shown.g, 170, 1) << (hdr ? "HDR" : "8-bit");
        EXPECT_NEAR(shown.b, 220, 1) << (hdr ? "HDR" : "8-bit");
    }
}

// THE NORMAL MAP MOVES THE FRESNEL. A quad facing the camera, its normal map tilting the surface 60 degrees along its
// texture's u: stored as (237, 128, 191), read as (237, 128, 191)/255 x 2 - 1 = (0.858824, 0.003922, 0.498039), of
// length 0.992792, so the tilted normal is (0.865059, 0.003950, 0.501655) in the tangent frame. Which way u runs in the
// world does not matter here: the four pixels round the middle (columns 31 and 32, rows 31 and 32, each at x, y =
// +-0.03125, so V = normalize(-+0.03125, -+0.03125, 5)) hold each sign of the tilt's lean against V twice, so their
// average is the same for u along +x, -x, +y or -y. Per pixel V = (+-0.03125, +-0.03125, 5) / 5.000195, so
// N.V = 0.501655 x 0.999961 +- 0.865059 x 0.006250 = 0.501635 +- 0.005407, that is 0.507042 or 0.496229 (the 0.003950
// part adds +-0.000025, which cancels in the average too); F = 0.02 + 0.98 (1 - N.V)^5 = 0.048528 or 0.051797, which
// average 0.050163; times white's 7.241658 of light, 0.363262. With the map ignored (the vertex normal, straight at
// the camera) it is the head-on 0.144833 of the first case.
// Mutation: the reflection takes the vertex normal, not the mapped one -> 0.1448, red.
TEST_F(Reflection, TheNormalMapMovesTheFresnel) {
    BRITE::Test::ScratchFolder folder("reflection_normal_map");
    const std::string path = (folder.path / "tilt.png").string();
    ::Image image = ::GenImageColor(4, 4, {237, 128, 191, 255});
    ::ExportImage(image, path.c_str());
    ::UnloadImage(image);
    const BRITE::TextureHandle tilt = m_backend.LoadTexture(path.c_str());
    ASSERT_NE(tilt, BRITE::NullTextureHandle);
    BRITE::MeshData mesh;
    mesh.Positions = {{-3, -3, 0}, {3, -3, 0}, {3, 3, 0}, {-3, 3, 0}};
    mesh.Normals.assign(4, {0, 0, 1});
    for (const BRITE::Vector3& p : mesh.Positions)
        mesh.TexCoords.push_back({(p.x + 3.0f) / 6.0f, (3.0f - p.y) / 6.0f});
    mesh.Indices = {0, 1, 2, 0, 2, 3};
    const BRITE::ModelHandle quad = m_backend.LoadModelFromMesh(mesh);
    m_models.push_back(quad);
    BRITE::RenderPass pass;
    pass.Post.Hdr = true;
    pass.Post.Samples = 1;
    pass.Reflections.SkyColor = {255, 255, 255, 255};
    BRITE::ModelDrawCommand draw = Mirror(quad, BRITE::ReflectionMode::Sky);
    draw.Material.NormalMap = tilt;
    pass.ModelCommands = {draw};
    Picture(pass);
    const std::vector<float> linear = LinearScene();
    const float average =
        (Red(linear, 31, 31) + Red(linear, 32, 31) + Red(linear, 31, 32) + Red(linear, 32, 32)) / 4.0f;
    EXPECT_NEAR(average, 0.363262f, 4e-3f);
    m_backend.UnloadTexture(tilt);
}

// DISTORTION: a planar reflection is read where the point projects, moved by the normal's horizontal part times
// ReflectionDistortion. The water scene of the planar case, but its normals leant 30 degrees toward +x,
// N = (0.5, 0.866025, 0), and a distortion of 0.5: the read moves 0.5 x 0.5 = 0.25 across the reflection, from the
// cube's reflection at its middle to open sky -- the cube's reflection is about 1/8 of the reflection across (1 wide at
// 9.5 from the mirrored camera, whose view is 2 tan 22.5 x 9.5 = 7.9 across), so a quarter is well clear of it. The sky
// is the clear colour, blue, 7.241658 of light under HDR. N.V = 0.866025 x 2/6.324555 = 0.273861 (V has no x),
// (1 - 0.273861)^5 = 0.201862, F = 0.02 + 0.98 x 0.201862 = 0.217845: blue 7.241658 x 0.217845 = 1.5776, no red.
// Mutation: the distortion dropped -> the cube's red, 1.58, and no blue: red.
TEST_F(Reflection, ANormalsLeanMovesWhereThePlanarReflectionIsRead) {
    const float s = std::sin(BRITE::Pi / 6.0f), c = std::cos(BRITE::Pi / 6.0f);
    const BRITE::ModelHandle water = Water(0.0f, {s, c, 0.0f});
    BRITE::RenderPass pass = PlanarPass(water, true);
    pass.ModelCommands[0].Material.ReflectionDistortion = 0.5f;
    WaterPicture(pass);
    const std::vector<float> linear = LinearScene();
    const std::size_t middle = static_cast<std::size_t>((SIZE - 1 - 31) * SIZE + 31) * 4;
    EXPECT_NEAR(linear[middle + 0], 0.0f, 0.01f);
    EXPECT_NEAR(linear[middle + 2], 1.5776f, 0.08f);
}

// THE REFLECTION'S TARGETS ARE KEPT BY SIZE AND FREED WHEN IDLE, as the HDR chain's sets are. A planar pass at the full
// size and one at half make two targets (32 x 32 and 16 x 16). Then only the full one, for IdlePasses + 10 rounds:
// each planar pass counts two passes (the reflection's own draw and the pass's), so the half target, last used before
// the rounds, stands unused for 2 (IdlePasses + 10) passes -- more than IdlePasses: freed. The full one is drawn every
// round: kept. A pass with no plane draws none, and the last pass's reflection then reads as none.
// Mutations: one target kept, whatever the sizes (MaxSlots 1) -> one held after the first two passes, red; ForgetIdle
//   freeing nothing -> two held after the rounds, red.
TEST_F(Reflection, ItsTargetsAreKeptBySizeAndFreedWhenIdle) {
    const BRITE::ModelHandle water = Water(0.0f);
    BRITE::RenderPass full = PlanarPass(water, true);
    WaterPicture(full);
    BRITE::RenderPass half = PlanarPass(water, true);
    WaterPicture(half, SIZE / 2);
    int width = 0, height = 0;
    std::vector<float> reflection;
    ASSERT_TRUE(m_backend.ReadLastPlanarReflection(width, height, reflection));
    EXPECT_EQ(width, SIZE / 4);
    EXPECT_EQ(m_backend.PlanarReflectionTargetsHeld(), 2);
    const std::uint64_t rounds = BRITE::Backends::Raylib::RaylibRenderBackend::HdrIdlePasses() + 10;
    for (std::uint64_t round = 0; round < rounds; ++round) {
        BRITE::RenderPass again = PlanarPass(water, true);
        WaterPicture(again);
    }
    EXPECT_EQ(m_backend.PlanarReflectionTargetsHeld(), 1) << "the half-size target, idle, is freed";
    BRITE::RenderPass none = PlanarPass(water, true);
    none.Reflections.Planar = false;
    WaterPicture(none);
    EXPECT_FALSE(m_backend.ReadLastPlanarReflection(width, height, reflection));
}
