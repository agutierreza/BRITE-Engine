#include <ECS/Components.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <Systems/Box2DBackend.hpp>
#include <Systems/PhysicsSystem.hpp>
#include <gtest/gtest.h>

using namespace brite::framework;
using namespace BRITE;

class FuzzApp;

class FuzzScene : public Scene {
    int m_frameCount = 0;

  public:
    FuzzScene(Application* app) : Scene(app) {}

    void OnInstantiation() override {
        auto entity = m_registry.create();
        m_registry.emplace<TransformComponent>(entity);
        m_registry.emplace<RigidBodyComponent>(entity);
    }

    void OnLogicStep(double dt) override;
};

#include <Backends/Raylib/RaylibApplicationBackend.hpp>
#include <Backends/Raylib/RaylibInputBackend.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>

class FuzzApp : public Application {
  public:
    int totalFrames = 0;

    FuzzApp()
        : Application(std::make_unique<BRITE::Backends::Raylib::RaylibApplicationBackend>(),
                      std::make_unique<BRITE::Backends::Raylib::RaylibInputBackend>(),
                      std::make_unique<BRITE::Backends::Raylib::RaylibRenderBackend>(), "BRITE Fuzz Test", "BRITE",
                      "Engine", 800, 600) {}

    void OnStart() override {
        ChangeScene(std::make_shared<FuzzScene>(this));
    }
};

inline void FuzzScene::OnLogicStep(double dt) {
    m_frameCount++;

    auto view = m_registry.view<RigidBodyComponent>();
    for (auto entity : view) {
        b2BodyId body = GetValidBody(m_registry, entity);
        if (b2Body_IsValid(body)) {
            b2Body_ApplyForceToCenter(body, {10.0f, 0.0f}, true);
        }
    }

    // Randomly stress the transition system
    if (m_frameCount % 5 == 0) {
        GetApp()->ChangeScene(std::make_shared<FuzzScene>(GetApp()));
    }

    // FuzzApp stores the total frame count across scene changes
    FuzzApp* fuzzApp = static_cast<FuzzApp*>(GetApp());
    fuzzApp->totalFrames++;
    if (fuzzApp->totalFrames > 500) {
        GetApp()->Quit();
    }
}

TEST(EngineRobustness, FuzzSceneTransitions) {
    // This will run the application for 500 logic frames,
    // rapidly swapping scenes and applying physics forces.
    // If our strict phase ordering is correct, we will not hit
    // any Box2D assertions about invalid bodies.
    FuzzApp app;
    app.Run();
}
