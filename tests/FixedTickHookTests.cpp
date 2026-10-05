// Application::OnFixedTick: what the application itself does on a fixed tick,
// before any scene.
//
// Input that belongs to the application rather than to a scene -- a full-screen
// key, a screenshot key -- has to be read where no scene can take it first and
// where it can keep a key from the scenes: after the tick's input is applied,
// and before the scene stack is walked. These cases drive the real Run loop on
// a scripted clock, one tick a frame, with an input backend that reports one
// key press on one frame, and record who saw what, in which order.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/IApplicationBackend.hpp>
#include <Backends/IInputBackend.hpp>
#include <Core/InputManager.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using BRITE::KeyCode;
using brite::framework::Application;
using brite::framework::Scene;

namespace {

// Action ids of their own: bindings are the process's and only ever added to.
enum class HookAction : uint32_t {
    Claimed = 7100, // bound to a key the application claims
    Passed,         // bound to a key it leaves alone
};

constexpr int FRAMES = 4;
constexpr int PRESS_FRAME = 1; // the frame whose poll sees the keys go down

// One tick a frame: each GetTime a sixty-fourth of a second after the last,
// closing after FRAMES frames.
class FrameClock : public BRITE::Backends::IApplicationBackend {
  public:
    void Init(const std::string&, int, int) override {}
    void Shutdown() override {}
    bool WindowShouldClose() override {
        return m_reads > FRAMES;
    }
    void SetTargetFPS(int) override {}
    double GetTime() override {
        return (m_reads++) / 64.0;
    }
    int GetScreenWidth() override {
        return 1;
    }
    int GetScreenHeight() override {
        return 1;
    }

  private:
    int m_reads = 0;
};

// Reports Enter and F going down on frame PRESS_FRAME and up on the next. Run()
// polls the input manager, then calls PollEvents: so the frame counted here is
// the one whose poll comes next.
class OnePress : public BRITE::Backends::IInputBackend {
  public:
    void PollEvents() override {
        ++m_frame;
    }
    bool IsKeyDown(KeyCode) override {
        return false;
    }
    bool IsKeyPressed(KeyCode key) override {
        return m_frame == PRESS_FRAME && (key == KeyCode::Enter || key == KeyCode::F);
    }
    bool IsKeyReleased(KeyCode key) override {
        return m_frame == PRESS_FRAME + 1 && (key == KeyCode::Enter || key == KeyCode::F);
    }
    bool IsMouseButtonDown(BRITE::MouseButtonCode) override {
        return false;
    }
    bool IsMouseButtonPressed(BRITE::MouseButtonCode) override {
        return false;
    }
    bool IsMouseButtonReleased(BRITE::MouseButtonCode) override {
        return false;
    }
    float GetMouseX() override {
        return 0.0f;
    }
    float GetMouseY() override {
        return 0.0f;
    }
    float GetMouseDeltaX() override {
        return 0.0f;
    }
    float GetMouseDeltaY() override {
        return 0.0f;
    }
    bool IsGamepadButtonDown(BRITE::GamepadButtonCode) override {
        return false;
    }
    bool IsGamepadButtonPressed(BRITE::GamepadButtonCode) override {
        return false;
    }
    bool IsGamepadButtonReleased(BRITE::GamepadButtonCode) override {
        return false;
    }
    float GetGamepadAxis(BRITE::GamepadAxisCode axis) override {
        return BRITE::GamepadAxisRestValue(axis);
    }

  private:
    int m_frame = 0;
};

// Who ticked, in order, and what each read.
struct TickLog {
    std::vector<std::string> order;      // "app", or a scene's name
    std::vector<double> appDt;           // the dt the application's tick was given
    std::vector<bool> appSawClaimed;     // per tick
    std::vector<bool> sceneSawClaimed;   // per tick of the scene "top"
    std::vector<bool> sceneSawPassed;    // per tick of the scene "top"
};

class LoggingScene : public Scene {
  public:
    LoggingScene(Application* app, TickLog& log, std::string name, bool blocks)
        : Scene(app), m_log(log), m_name(std::move(name)), m_blocks(blocks) {}

    void OnLogicStep(double) override {
        m_log.order.push_back(m_name);
        if (m_name == "top") {
            m_log.sceneSawClaimed.push_back(BRITE::InputManager::IsActionPressed(HookAction::Claimed));
            m_log.sceneSawPassed.push_back(BRITE::InputManager::IsActionPressed(HookAction::Passed));
        }
    }
    bool BlocksUpdate() const override {
        return m_blocks;
    }

  private:
    TickLog& m_log;
    std::string m_name;
    bool m_blocks;
};

class HookedApp : public Application {
  public:
    HookedApp(TickLog& log, bool withScenes, std::unique_ptr<BRITE::Backends::IInputBackend> input = nullptr)
        : Application(std::make_unique<FrameClock>(), std::move(input), nullptr, "Hook", "BRITE", "Engine", 1, 1),
          m_log(log), m_withScenes(withScenes) {
        SetFixedTimeStep(1.0 / 64.0);
    }

  protected:
    void OnStart() override {
        if (!m_withScenes)
            return;
        // "bottom" under "top", and "top" blocks the updates of everything under it.
        PushScene(std::make_shared<LoggingScene>(this, m_log, "bottom", false));
        PushScene(std::make_shared<LoggingScene>(this, m_log, "top", true));
    }
    void OnFixedTick(double dt) override {
        m_log.order.push_back("app");
        m_log.appDt.push_back(dt);
        m_log.appSawClaimed.push_back(BRITE::InputManager::IsActionPressed(HookAction::Claimed));
        BRITE::InputManager::ClaimKey(KeyCode::Enter);
    }

  private:
    TickLog& m_log;
    bool m_withScenes;
};

} // namespace

// Four frames, one tick each. Each tick the application ticks first, with the
// fixed timestep of 1/64 s, and then the top scene -- and only the top, because
// it blocks the updates of the scene under it: the application's tick is not a
// scene's, and no scene's blocking stops it. With no scene at all it still
// ticks, four times.
//
// Mutations: the hook called after the scene walk -> "top" before "app"; called
// only when the stack is not empty -> no "app" at all with no scene; given 0
// rather than the fixed step -> dt 0.
TEST(FixedTickHook, TheApplicationTicksBeforeEverySceneEvenOneThatBlocks) {
    TickLog log;
    {
        HookedApp app(log, true);
        app.Run();
    }
    const std::vector<std::string> oneTick = {"app", "top"};
    std::vector<std::string> expected;
    for (int i = 0; i < FRAMES; ++i)
        expected.insert(expected.end(), oneTick.begin(), oneTick.end());
    EXPECT_EQ(log.order, expected);
    ASSERT_EQ(log.appDt.size(), static_cast<std::size_t>(FRAMES));
    for (double dt : log.appDt)
        EXPECT_EQ(dt, 1.0 / 64.0);

    TickLog empty;
    {
        HookedApp app(empty, false);
        app.Run();
    }
    EXPECT_EQ(empty.order, (std::vector<std::string>(FRAMES, "app"))) << "no scene, and it still ticks";
}

// Enter (bound to Claimed) and F (bound to Passed) go down on frame 1. In tick
// 1 the application sees Claimed pressed -- the tick's input is applied before
// it runs -- and claims Enter. The scene in the same tick sees Claimed NOT
// pressed and Passed pressed: the claim kept Enter from it, and only Enter. No
// other tick sees either.
//
// Mutations: the hook called before the input is flushed -> it sees nothing
// pressed in tick 1, and its claim is wiped by the flush, so the scene sees
// Claimed; called after the scene walk -> the scene sees Claimed before the
// claim; ClaimKey not hiding the key from IsActionPressed -> the scene sees it.
TEST(FixedTickHook, TheApplicationSeesTheTicksPressesAndAKeyItClaimsNeverReachesTheScene) {
    BRITE::InputManager::BindAction(HookAction::Claimed, KeyCode::Enter);
    BRITE::InputManager::BindAction(HookAction::Passed, KeyCode::F);
    TickLog log;
    {
        HookedApp app(log, true, std::make_unique<OnePress>());
        app.Run();
    }
    const std::vector<bool> onlyTickOne = {false, true, false, false};
    EXPECT_EQ(log.appSawClaimed, onlyTickOne) << "the application sees the press in its tick";
    EXPECT_EQ(log.sceneSawPassed, onlyTickOne) << "the scene sees an unclaimed key pressed in the same tick";
    EXPECT_EQ(log.sceneSawClaimed, (std::vector<bool>{false, false, false, false}))
        << "and never the key the application claimed";
}
