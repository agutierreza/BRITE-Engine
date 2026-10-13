// A scene's physics: none unless it is given a backend, and a backend it is given is stepped by the framework in the
// fixed tick's phases 2 and 4 -- after OnInstantiation, around OnLogicStep, before OnRenderPrepStep.
//
// The application runs on a scripted clock that advances exactly one fixed tick per frame, with no window, input or
// renderer, and every scene and backend writes what happens to it into one log, in order.

#include <Backends/IApplicationBackend.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <Systems/IPhysicsBackend.hpp>
#include <Systems/PhysicsSystem.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using brite::framework::Application;
using brite::framework::Scene;

namespace {

constexpr double TICK = 1.0 / 64.0; // exact in binary, so each frame is exactly one tick

class OneTickPerFrame : public BRITE::Backends::IApplicationBackend {
  public:
    explicit OneTickPerFrame(int frames) {
        for (int k = 0; k <= frames; ++k)
            m_times.push_back(k * TICK);
    }

    void Init(const std::string&, int, int) override {}
    void Shutdown() override {}
    bool WindowShouldClose() override {
        return m_next >= m_times.size();
    }
    void SetTargetFPS(int) override {}
    double GetTime() override {
        if (m_next < m_times.size())
            return m_times[m_next++];
        return m_times.back();
    }
    int GetScreenWidth() override {
        return 1;
    }
    int GetScreenHeight() override {
        return 1;
    }

  private:
    std::vector<double> m_times;
    std::size_t m_next = 0;
};

using Log = std::vector<std::string>;

// A physics backend that only says what is asked of it, and on which registry.
class LoggingPhysics : public BRITE::IPhysicsBackend {
  public:
    LoggingPhysics(Log& log, const entt::registry*& initialisedOn) : m_log(log), m_initialisedOn(initialisedOn) {}

    void Init(entt::registry& registry) override {
        m_log.push_back("physics init");
        m_initialisedOn = &registry;
    }
    void PreStep(entt::registry&) override {
        m_log.push_back("physics prestep");
    }
    void Step(entt::registry&, float dt) override {
        m_log.push_back("physics step " + std::to_string(dt));
    }
    void SetMaterial(entt::registry&, entt::entity, std::shared_ptr<BRITE::PhysicsMaterial>) override {}
    float GetJointForce(entt::registry&, entt::entity) override {
        return 0.0f;
    }
    void DestroyJoint(entt::registry&, entt::entity) override {}

  private:
    Log& m_log;
    const entt::registry*& m_initialisedOn;
};

// A scene that logs the three phases that are its own.
class PhaseScene : public Scene {
  public:
    PhaseScene(Application* app, Log& log) : Scene(app), m_log(log) {}

    void OnInstantiation() override {
        m_log.push_back("instantiation");
    }
    void OnLogicStep(double) override {
        m_log.push_back("logic");
    }
    void OnRenderPrepStep(double) override {
        m_log.push_back("render prep");
    }

  private:
    Log& m_log;
};

class OneSceneApp : public Application {
  public:
    OneSceneApp(int frames, std::shared_ptr<Scene>& scene, std::function<std::shared_ptr<Scene>(Application*)> make)
        : Application(std::make_unique<OneTickPerFrame>(frames), nullptr, nullptr, "ScenePhysics", "BRITE", "Engine", 1,
                      1),
          m_scene(scene), m_make(std::move(make)) {
        SetFixedTimeStep(TICK);
    }

  protected:
    void OnStart() override {
        m_scene = m_make(this);
        ChangeScene(m_scene);
    }

  private:
    std::shared_ptr<Scene>& m_scene;
    std::function<std::shared_ptr<Scene>(Application*)> m_make;
};

} // namespace

TEST(ScenePhysics, ASceneGivenNoBackendHasNoPhysicsAndTicksItsOwnPhasesAlone) {
    // Until the framework made physics optional, every scene built a Box2D world in its constructor and stepped it
    // on every tick, whether it used it or not. Now a scene has none unless it asks: its three phases run, and
    // nothing else does.
    // MUTATION: the Scene constructor giving every scene a backend again -> GetBackend not null, red.
    Log log;
    std::shared_ptr<Scene> scene;
    {
        OneSceneApp app(2, scene, [&log](Application* a) { return std::make_shared<PhaseScene>(a, log); });
        app.Run(); // the scene is made in OnStart, which Run calls
        ASSERT_NE(scene, nullptr);
        EXPECT_EQ(scene->GetPhysicsSystem().GetBackend(), nullptr);
    }
    // BY HAND: two frames of exactly one tick each (the scripted clock), the scene started before the first.
    const Log expected = {"instantiation", "logic", "render prep", "instantiation", "logic", "render prep"};
    EXPECT_EQ(log, expected);
}

TEST(ScenePhysics, ABackendASceneIsGivenIsInitialisedOnItsRegistryAndSteppedInPhasesTwoAndFour) {
    // The five phases of a fixed tick, in order: OnInstantiation, the physics' PreStep, OnLogicStep, the physics'
    // Step with the fixed timestep, OnRenderPrepStep. The backend is initialised once, on the scene's own registry,
    // when it is given.
    // MUTATION: the Application not calling PreStep -> "physics prestep" missing, red. Step before OnLogicStep ->
    // order wrong, red. Init not called by PhysicsSystem::Init -> "physics init" missing and the registry null, red.
    Log log;
    const entt::registry* initialisedOn = nullptr;
    std::shared_ptr<Scene> scene;
    {
        OneSceneApp app(2, scene, [&log, &initialisedOn](Application* a) {
            auto made = std::make_shared<PhaseScene>(a, log);
            made->GetPhysicsSystem().Init(made->GetRegistry(), std::make_unique<LoggingPhysics>(log, initialisedOn));
            return made;
        });
        app.Run(); // the scene is made in OnStart, which Run calls
        ASSERT_NE(scene, nullptr);
        EXPECT_NE(scene->GetPhysicsSystem().GetBackend(), nullptr);
        EXPECT_EQ(initialisedOn, &scene->GetRegistry());
    }
    // BY HAND: the fixed timestep is TICK = 1/64 s, handed to Step as a float: 0.015625, exact, printed by
    // std::to_string with six decimals.
    const std::string step = "physics step 0.015625";
    const Log expected = {"physics init",  "instantiation",   "physics prestep", "logic", step,         "render prep",
                          "instantiation", "physics prestep", "logic",           step,    "render prep"};
    EXPECT_EQ(log, expected);
}
