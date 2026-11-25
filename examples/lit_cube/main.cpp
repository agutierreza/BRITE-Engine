// lit_cube: a cube shaded by the engine's lit (PBR) path -- a model built from geometry in code, a sun and an ambient
// light -- turning slowly.
//
// The same source builds for the desktop and for the web platform (Emscripten: WebAssembly and WebGL 2), where it is
// the smallest proof that the engine's lit shader compiles and draws in a browser. Nothing here knows which platform it
// is on: Application::Run hands the frames to the browser there.
//
//   lit_cube            runs until the window is closed (on the web, until the page goes)
//   lit_cube --frames N runs N fixed ticks (60 a second) and exits

#include <Backends/MeshData.hpp>
#include <Backends/Raylib/RaylibApplicationBackend.hpp>
#include <Backends/Raylib/RaylibInputBackend.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace brite::framework;

namespace {

// A cube two units on a side, centred on the origin: four vertices a face, each face with its own outward normal so
// the lighting changes at the edges, and two counter-clockwise triangles a face seen from outside. A face's corners
// are its normal n plus each of (-u-v), (+u-v), (+u+v), (-u+v), with u x v = n -- which is what makes the winding
// counter-clockwise from outside.
BRITE::MeshData CubeMesh() {
    struct Face {
        BRITE::Vector3 n, u, v;
    };
    const Face faces[] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    BRITE::MeshData mesh;
    for (const Face& f : faces) {
        const auto first = static_cast<std::uint32_t>(mesh.Positions.size());
        for (const auto& c : corners) {
            mesh.Positions.push_back({f.n.x + c[0] * f.u.x + c[1] * f.v.x, f.n.y + c[0] * f.u.y + c[1] * f.v.y,
                                      f.n.z + c[0] * f.u.z + c[1] * f.v.z});
            mesh.Normals.push_back(f.n);
        }
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            mesh.Indices.push_back(first + i);
    }
    return mesh;
}

} // namespace

class LitCubeScene : public Scene {
  public:
    LitCubeScene(Application* app, int ticksToRun) : Scene(app), m_ticksToRun(ticksToRun) {}

    void OnStart() override {
        m_model = GetApp()->GetRenderBackend()->LoadModelFromMesh(CubeMesh());

        m_camera.position = {4.5f, 3.5f, 5.5f};
        m_camera.target = {0.0f, 0.0f, 0.0f};
        m_camera.up = {0.0f, 1.0f, 0.0f};
        m_camera.fovy = 45.0f;
        m_camera.projection = BRITE::CameraProjection::Perspective;
    }

    void OnShutdown() override {
        GetApp()->GetRenderBackend()->UnloadModel(m_model);
    }

    void OnLogicStep(double dt) override {
        m_seconds += dt;
        if (m_ticksToRun > 0 && ++m_ticksRun >= m_ticksToRun)
            GetApp()->Quit();
    }

    void OnRender(BRITE::RenderPass& pass) override {
        pass.ClearColor = BRITE::Color{24, 28, 40, 255};
        pass.Camera3DPtr = &m_camera;

        // A sun from above, in front and to the right, and a little ambient so the faces it misses are not black.
        BRITE::Light sun;
        sun.Type = BRITE::LightType::Directional;
        sun.Direction = {-0.4f, -1.0f, -0.6f};
        sun.Intensity = 3.0f;
        pass.Lights.push_back(sun);
        pass.AmbientColor = BRITE::White;
        pass.AmbientIntensity = 0.15f;

        BRITE::ModelDrawCommand cube;
        cube.Model = m_model;
        cube.Material.AlbedoTint = BRITE::Color{230, 90, 40, 255};
        cube.Material.Metallic = 0.0f;
        cube.Material.Roughness = 0.45f;
        cube.Position = {0.0f, 0.0f, 0.0f};
        const float halfTurn = static_cast<float>(m_seconds) * 0.25f;
        cube.Rotation = {0.0f, std::sin(halfTurn), 0.0f, std::cos(halfTurn)}; // a quaternion: spin about +y
        cube.Scale = {1.0f, 1.0f, 1.0f};
        pass.ModelCommands.push_back(cube);
    }

  private:
    BRITE::ModelHandle m_model = BRITE::NullModelHandle;
    BRITE::Camera3D m_camera{};
    double m_seconds = 0.0;
    int m_ticksToRun;
    int m_ticksRun = 0;
};

class LitCubeApp : public Application {
  public:
    explicit LitCubeApp(int ticksToRun)
        : Application(std::make_unique<BRITE::Backends::Raylib::RaylibApplicationBackend>(),
                      std::make_unique<BRITE::Backends::Raylib::RaylibInputBackend>(),
                      std::make_unique<BRITE::Backends::Raylib::RaylibRenderBackend>(), "Lit cube", "lit_cube",
                      "lit_cube", 800, 600),
          m_ticksToRun(ticksToRun) {}

  protected:
    void OnStart() override {
        PushScene(std::make_shared<LitCubeScene>(this, m_ticksToRun));
    }

  private:
    int m_ticksToRun;
};

int main(int argc, char** argv) {
    int ticksToRun = 0; // 0: until the window is closed
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0)
            ticksToRun = std::atoi(argv[i + 1]);
    }
    LitCubeApp app(ticksToRun);
    app.Run();
    return 0;
}
