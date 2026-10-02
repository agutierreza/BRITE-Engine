// The scene stack's transitions, as the application applies them: what ChangeScene
// ends, and how to replace the top of the stack while keeping what is under it.
//
// Every scene writes what happens to it into one log, in order, so a case reads
// the whole sequence: which scenes started and stopped, in which order, and which
// ticked on each frame. The application runs on a scripted clock that advances
// exactly one fixed tick per frame, with no window, input or renderer.

#include <Backends/IApplicationBackend.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
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

// A scene that logs its start, its stop and each tick, and does `onFirstTick`
// during its first tick.
class LoggingScene : public Scene {
  public:
    LoggingScene(Application* app, std::string name, Log& log) : Scene(app), m_name(std::move(name)), m_log(log) {}

    std::function<void(Application&)> onFirstTick;

    void OnStart() override {
        m_log.push_back("start " + m_name);
    }
    void OnShutdown() override {
        m_log.push_back("stop " + m_name);
    }
    void OnLogicStep(double) override {
        m_log.push_back("tick " + m_name);
        if (m_ticks++ == 0 && onFirstTick)
            onFirstTick(*GetApp());
    }
    // An overlay: the scenes under it go on ticking and drawing.
    bool BlocksUpdate() const override {
        return false;
    }
    bool BlocksRender() const override {
        return false;
    }

  private:
    std::string m_name;
    Log& m_log;
    int m_ticks = 0;
};

// A base scene A with an overlay B over it, both started on the first frame.
// `whenBTicks` is what B does on its first tick.
class BaseAndOverlayApp : public Application {
  public:
    // `log` outlives the application: its destructor stops the scenes still on
    // the stack, and they log that too.
    BaseAndOverlayApp(Log& log, int frames, std::function<void(Application&, BaseAndOverlayApp&)> whenBTicks)
        : Application(std::make_unique<OneTickPerFrame>(frames), nullptr, nullptr, "SceneStack", "BRITE", "Engine", 1,
                      1),
          log(log), m_whenBTicks(std::move(whenBTicks)) {}

    Log& log;

    std::shared_ptr<LoggingScene> Make(const char* name) {
        return std::make_shared<LoggingScene>(this, name, log);
    }

  protected:
    void OnStart() override {
        ChangeScene(Make("A"));
        auto overlay = Make("B");
        overlay->onFirstTick = [this](Application& app) { m_whenBTicks(app, *this); };
        PushScene(overlay);
    }

  private:
    std::function<void(Application&, BaseAndOverlayApp&)> m_whenBTicks;
};

} // namespace

TEST(SceneStack, ChangeSceneEndsEverySceneOnTheStackTopFirstThenStartsTheNewOne) {
    // B, an overlay over A, changes scene to C on its first tick. The change is
    // applied at the top of the next frame: B stops, then A -- the whole stack,
    // from the top down -- and only then does C start. From then on C is the
    // whole stack and it alone ticks, one tick a frame for the remaining frames.
    // MUTATIONS: shutting down only the top scene -- "stop A" is missing;
    // red. Shutting down bottom first -- "stop A" before "stop
    // B"; red.
    Log log;
    BaseAndOverlayApp app(log, 4, [](Application& a, BaseAndOverlayApp& self) { a.ChangeScene(self.Make("C")); });
    app.SetFixedTimeStep(TICK);
    app.Run();

    const Log expected = {"start A", "start B", "tick B",  "tick A", // frame 1: the stack ticks top-down
                          "stop B",  "stop A",  "start C",           // frame 2: the change, applied
                          "tick C",  "tick C",  "tick C"};           // frames 2 to 4
    EXPECT_EQ(log, expected);
}

TEST(SceneStack, PopThenPushInOneTickReplacesOnlyTheTopAndKeepsTheScenesUnderIt) {
    // The counterpart of ChangeScene: B pops itself and pushes C in the same
    // tick. Both are applied at the top of the next frame, in the order they were
    // asked for and before any scene ticks, so no tick ever sees the stack
    // without its top: B stops, C starts, and A -- under it -- is never stopped
    // and ticks on every frame beneath C.
    // MUTATIONS: applying Pop as ChangeScene does (ending the whole stack) --
    // "stop A" appears and A stops ticking; red.
    Log log;
    BaseAndOverlayApp app(log, 4, [](Application& a, BaseAndOverlayApp& self) {
        a.PopScene();
        a.PushScene(self.Make("C"));
    });
    app.SetFixedTimeStep(TICK);
    app.Run();

    const Log expected = {"start A", "start B", "tick B", "tick A", // frame 1
                          "stop B",  "start C",                     // frame 2: the top replaced
                          "tick C",  "tick A",                      // frames 2 to 4, A under C throughout
                          "tick C",  "tick A",  "tick C", "tick A"};
    EXPECT_EQ(log, expected);
}
