// hello: the smallest BRITE application that draws something that moves.
//
// A window, one scene, one entity with a 3D cube, and a camera. Everything here is the engine's real API: the README
// quotes this file, and it is built with every engine build, so it cannot go stale.
//
//   hello            runs until the window is closed
//   hello --frames N runs N fixed ticks (60 a second) and exits, for a script to start it and see it end

#include <Backends/Raylib/RaylibApplicationBackend.hpp>
#include <Backends/Raylib/RaylibInputBackend.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <ECS/Components.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <Systems/RenderSystem.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace brite::framework;

class HelloScene : public Scene {
  public:
    HelloScene(Application* app, int ticksToRun) : Scene(app), m_ticksToRun(ticksToRun) {}

    // Spawn the entities and aim the camera. Called once, when the scene is pushed.
    void OnStart() override {
        m_cube = m_registry.create();
        m_registry.emplace<BRITE::TransformComponent>(m_cube);
        auto& cube = m_registry.emplace<BRITE::Primitive3DComponent>(m_cube);
        cube.Type = BRITE::Primitive3DType::Cube;
        cube.Size = {2.0f, 2.0f, 2.0f};
        cube.Tint = BRITE::Color{230, 90, 40, 255};

        m_camera.position = {6.0f, 5.0f, 6.0f};
        m_camera.target = {0.0f, 0.0f, 0.0f};
        m_camera.up = {0.0f, 1.0f, 0.0f};
        m_camera.fovy = 45.0f;
        m_camera.projection = BRITE::CameraProjection::Perspective;
    }

    // Game logic, 60 times a second whatever the frame rate. `dt` is the fixed step, 1/60 s.
    void OnLogicStep(double dt) override {
        m_seconds += dt;
        auto& transform = m_registry.get<BRITE::TransformComponent>(m_cube);
        const float halfTurn = static_cast<float>(m_seconds) * 0.5f;
        transform.Rotation = {0.0f, std::sin(halfTurn), 0.0f, std::cos(halfTurn)}; // a quaternion: spin about +y
        transform.Position.x = 2.0f * std::sin(static_cast<float>(m_seconds));

        if (m_ticksToRun > 0 && ++m_ticksRun >= m_ticksToRun)
            m_app->Quit();
    }

    // Describe what to draw. RenderSystem turns every entity with a TransformComponent and a
    // Primitive3DComponent (or a SpriteComponent) into draw commands in `pass`; the backend draws the pass.
    void OnRender(BRITE::RenderPass& pass) override {
        BRITE::RenderSystem::Update(m_registry, pass, nullptr, &m_camera);
    }

  private:
    entt::entity m_cube{entt::null};
    BRITE::Camera3D m_camera{};
    double m_seconds = 0.0;
    int m_ticksToRun;
    int m_ticksRun = 0;
};

class HelloApp : public Application {
  public:
    HelloApp(int ticksToRun)
        : Application(std::make_unique<BRITE::Backends::Raylib::RaylibApplicationBackend>(),
                      std::make_unique<BRITE::Backends::Raylib::RaylibInputBackend>(),
                      std::make_unique<BRITE::Backends::Raylib::RaylibRenderBackend>(), "Hello BRITE", "hello", "hello",
                      800, 600),
          m_ticksToRun(ticksToRun) {}

  protected:
    void OnStart() override {
        PushScene(std::make_shared<HelloScene>(this, m_ticksToRun));
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
    HelloApp app(ticksToRun);
    app.Run();
    return 0;
}
