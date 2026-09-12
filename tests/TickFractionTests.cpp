// Application::TickFraction: the residual of the fixed timestep, exposed.
//
// A fixed-timestep loop that renders faster than it simulates draws the same
// state on every frame between two ticks. The fraction of a tick the clock has
// advanced past the last one is what lets a consumer interpolate; without it
// every consumer draws in steps. These cases drive the real Application::Run
// loop against a scripted clock -- a window backend whose GetTime returns the
// frame times fed to it and whose window "closes" when they run out -- and read
// the fraction from the phase it is documented for, OnRender.
//
// Frame times are binary-exact fractions of a second: a tick of 1/64 s and a
// frame every 1/512 s, so eight frames make exactly one tick and every expected
// fraction is exact rather than a rounding away from the tick boundary. Beside
// each case is the mutation that turns it red; each was run.

#include <Backends/IApplicationBackend.hpp>
#include <Backends/IRenderBackend.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using brite::framework::Application;
using brite::framework::Scene;

namespace {

/// A clock that answers GetTime with the next scripted instant and closes the
/// window once the script is spent. Run() reads the first instant before its
/// loop as the starting time, then one per frame.
class ScriptedClock : public BRITE::Backends::IApplicationBackend {
  public:
    explicit ScriptedClock(std::vector<double> times) : m_times(std::move(times)) {}

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

/// Draws nothing. Present only so that Run() reaches OnRender, which it does
/// not without a render backend.
class NullRenderBackend : public BRITE::Backends::IRenderBackend {
  public:
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
        return BRITE::EnvironmentMap{};
    }
    void UnloadEnvironmentMap(BRITE::EnvironmentMap) override {}
    int GetShaderLocation(BRITE::ShaderHandle, const char*) override {
        return -1;
    }
    void SetShaderValue(BRITE::ShaderHandle, int, const void*, BRITE::Backends::ShaderUniformDataType) override {}
};

/// What one drawn frame saw: the fraction read in OnRender, and how many
/// ticks ran since the previous frame.
struct FrameReading {
    double fraction;
    int ticks;
};

/// Records the fraction where it is valid, and also what it reads during a
/// tick, to pin the documented staleness there.
class RecordingScene : public Scene {
  public:
    explicit RecordingScene(Application* app) : Scene(app) {}

    std::vector<FrameReading> frames;
    std::vector<double> readDuringTicks; // one per OnLogicStep

    void OnLogicStep(double) override {
        ++m_ticksSinceRender;
        readDuringTicks.push_back(GetApp()->TickFraction());
    }
    void OnRender(BRITE::RenderPass&) override {
        frames.push_back({GetApp()->TickFraction(), m_ticksSinceRender});
        m_ticksSinceRender = 0;
    }

  private:
    int m_ticksSinceRender = 0;
};

class RecordingApp : public Application {
  public:
    explicit RecordingApp(std::vector<double> times)
        : Application(std::make_unique<ScriptedClock>(std::move(times)), nullptr, std::make_unique<NullRenderBackend>(),
                      "TickFraction", "BRITE", "Engine", 1, 1) {}

    std::shared_ptr<RecordingScene> scene;

  protected:
    void OnStart() override {
        scene = std::make_shared<RecordingScene>(this);
        ChangeScene(scene);
    }
};

constexpr double TICK = 1.0 / 64.0;   // 0.015625 s, exact in binary
constexpr double FRAME = 1.0 / 512.0; // 0.001953125 s: eight frames per tick

} // namespace

TEST(TickFraction, IsZeroAfterATickAndClimbsTowardOneBeforeTheNext) {
    // Frames at k / 512 s for k = 0..8 after the starting instant. Frame k
    // leaves k / 8 of a tick in the accumulator until frame 8, where exactly
    // one tick's worth has gathered, the tick runs, and nothing is left:
    //     frame 1: 0.125   frame 2: 0.250   ...   frame 7: 0.875   (no tick)
    //     frame 8: 0.0, and it carried the one tick
    //
    // Mutations: store the fraction BEFORE the tick loop -> frame 8 reads 1.0
    // -> red (and the Debug assert fires). Divide by the frame time instead of
    // the tick -> frame 1 reads 1.0 -> red. Store the accumulator undivided ->
    // frame 1 reads 0.00195 -> red.
    std::vector<double> times;
    for (int k = 0; k <= 8; ++k)
        times.push_back(k * FRAME);
    RecordingApp app(times);
    app.SetFixedTimeStep(TICK);
    app.Run();

    const auto& frames = app.scene->frames;
    ASSERT_EQ(frames.size(), 8u);
    for (int k = 1; k <= 7; ++k) {
        EXPECT_DOUBLE_EQ(frames[static_cast<std::size_t>(k - 1)].fraction, k / 8.0) << "frame " << k;
        EXPECT_EQ(frames[static_cast<std::size_t>(k - 1)].ticks, 0) << "frame " << k;
    }
    EXPECT_DOUBLE_EQ(frames[7].fraction, 0.0);
    EXPECT_EQ(frames[7].ticks, 1);
}

TEST(TickFraction, ALongFrameRunsSeveralTicksAndKeepsOnlyTheRemainder) {
    // Two ordinary frames, then one 3.5 ticks long: 3.5 / 64 = 28 / 512 s.
    //     frame 1 at  1/512: 0.125, no tick
    //     frame 2 at  2/512: 0.250, no tick
    //     frame 3 at 30/512: 0.25 + 3.5 = 3.75 ticks gathered; three run, 0.75 left
    // The fraction never leaves [0, 1) whatever the frame length.
    //
    // Mutation: run at most one tick per frame (`if` for the `while`) -> frame 3
    // reads 2.75, the assert fires -> red.
    RecordingApp app({0.0, 1 * FRAME, 2 * FRAME, 30 * FRAME});
    app.SetFixedTimeStep(TICK);
    app.Run();

    const auto& frames = app.scene->frames;
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_DOUBLE_EQ(frames[0].fraction, 0.125);
    EXPECT_DOUBLE_EQ(frames[1].fraction, 0.25);
    EXPECT_DOUBLE_EQ(frames[2].fraction, 0.75);
    EXPECT_EQ(frames[2].ticks, 3);
    for (const FrameReading& frame : frames) {
        EXPECT_GE(frame.fraction, 0.0);
        EXPECT_LT(frame.fraction, 1.0);
    }
}

TEST(TickFraction, DuringATickItStillHoldsThePreviousFramesValue) {
    // The header says a read during OnLogicStep is the previous frame's
    // residual, not the tick's. Frames at 4/512 and 12/512: frame 1 gathers
    // half a tick and renders 0.5; frame 2 gathers another full tick (1.5 in
    // all), runs one, renders 0.5 again. The one tick runs inside frame 2,
    // and what it reads is frame 1's 0.5 -- not 0.0, not 1.5.
    //
    // Mutation: also refresh the fraction from the live accumulator at the top
    // of each tick -> the tick reads 1.5 -> red.
    RecordingApp app({0.0, 4 * FRAME, 12 * FRAME});
    app.SetFixedTimeStep(TICK);
    app.Run();

    ASSERT_EQ(app.scene->frames.size(), 2u);
    EXPECT_DOUBLE_EQ(app.scene->frames[0].fraction, 0.5);
    EXPECT_DOUBLE_EQ(app.scene->frames[1].fraction, 0.5);
    EXPECT_EQ(app.scene->frames[1].ticks, 1);
    ASSERT_EQ(app.scene->readDuringTicks.size(), 1u);
    EXPECT_DOUBLE_EQ(app.scene->readDuringTicks[0], 0.5);
}
