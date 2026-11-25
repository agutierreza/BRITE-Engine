// Application::RunFrame: an application its host steps one frame at a time.
//
// A host that owns the loop -- a browser asks for a frame and must get control
// back after it -- cannot call Run, which returns only when the application
// stops. RunFrame runs one frame and returns; Run is a loop over it. These
// cases drive both against the same scripted clock (ScriptedFrames.hpp) and
// check that a stepped application sees exactly the frames, ticks and
// fractions a run one does, and that a stepped run starts once and stops once.
//
// Frame times are binary-exact: a tick of 1/64 s, frame lengths in multiples
// of 1/512 s (an eighth of a tick), so every expected value is exact. Beside
// each case is the mutation that turns it red; each was run.

#include "ScriptedFrames.hpp"
#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

using namespace scripted_frames;

namespace {

// Starts at 1.0 s, not 0: an application that measured its first frame from
// zero rather than from the clock's first instant would see a first frame a
// second long.
std::vector<double> MixedFrames() {
    const double start = 1.0;
    return {
        start,
        start + 1 * FRAME,        // an eighth of a tick
        start + 30 * FRAME,       // 29 / 8 = 3.625 ticks
        start + 34 * FRAME,       // half a tick
        start + 34 * FRAME + 0.5, // half a second: longer than the 0.25 s clamp
        start + 40 * FRAME + 0.5, // three-quarters of a tick
    };
}

// The frames MixedFrames makes, worked by hand. The accumulator, in ticks:
//   start: the clock's first instant, 1.0, is where frame 1 is measured from.
//   frame 1: + 1/8            = 0.125  -> no tick,  0.125 left
//   frame 2: + 29/8 = 3.625   = 3.75   -> 3 ticks,  0.75 left
//   frame 3: + 4/8  = 0.5     = 1.25   -> 1 tick,   0.25 left
//   frame 4: 0.5 s is clamped to 0.25 s = 0.25 * 64 = 16 ticks;
//            0.25 + 16        = 16.25  -> 16 ticks, 0.25 left
//   frame 5: + 6/8  = 0.75    = 1.0    -> 1 tick,   0.0 left
// 21 ticks in all. The window closes once the six instants are spent: the
// start and five frames.
const std::vector<FrameReading> EXPECTED = {
    {0.125, 0}, {0.75, 3}, {0.25, 1}, {0.25, 16}, {0.0, 1},
};

void ExpectFrames(const std::vector<FrameReading>& frames, const char* how) {
    ASSERT_EQ(frames.size(), EXPECTED.size()) << how;
    for (std::size_t i = 0; i < EXPECTED.size(); ++i) {
        EXPECT_DOUBLE_EQ(frames[i].fraction, EXPECTED[i].fraction) << how << ", frame " << i + 1;
        EXPECT_EQ(frames[i].ticks, EXPECTED[i].ticks) << how << ", frame " << i + 1;
    }
}

/// Quits as it starts.
class QuitsAtStart : public RecordingApp {
  public:
    QuitsAtStart() : RecordingApp({0.0, 1 * FRAME, 2 * FRAME}) {}

  protected:
    void OnStart() override {
        RecordingApp::OnStart();
        Quit();
    }
};

} // namespace

TEST(FrameStep, SteppedFramesSeeTheSameTicksAndFractionsAsRun) {
    // Mutations, each red here: measure each frame from the start rather than
    // from the frame before (drop `m_previousTime = currentTime`) -> frame 2 is
    // 30/8 ticks long, not 29/8. Take the start as 0 rather than the clock's
    // first instant -> frame 1 is a second long, clamped to 16 ticks. Drop the
    // clamp -> frame 4 runs 32 ticks. Set the accumulator to the frame's
    // length instead of adding it -> frame 2 leaves 0.625, not 0.75.
    RecordingApp run(MixedFrames());
    run.SetFixedTimeStep(TICK);
    run.Run();
    ExpectFrames(run.scene->frames, "Run");

    RecordingApp stepped(MixedFrames());
    stepped.SetFixedTimeStep(TICK);
    int framesRun = 0;
    while (stepped.RunFrame()) {
        ++framesRun;
        // Control is back with the host between frames: the frame it just ran
        // has been drawn, and nothing more.
        ASSERT_EQ(stepped.scene->frames.size(), static_cast<std::size_t>(framesRun));
    }
    EXPECT_EQ(framesRun, 5);
    ExpectFrames(stepped.scene->frames, "RunFrame");

    // Frame by frame, the same as Run's, bit for bit.
    ASSERT_EQ(stepped.scene->frames.size(), run.scene->frames.size());
    for (std::size_t i = 0; i < run.scene->frames.size(); ++i) {
        EXPECT_EQ(stepped.scene->frames[i].fraction, run.scene->frames[i].fraction) << "frame " << i + 1;
        EXPECT_EQ(stepped.scene->frames[i].ticks, run.scene->frames[i].ticks) << "frame " << i + 1;
    }
    EXPECT_EQ(stepped.scene->readDuringTicks, run.scene->readDuringTicks);
}

TEST(FrameStep, ASteppedApplicationStartsOnceAndStaysStopped) {
    // The first call starts it (OnStart once); a call after it stopped runs
    // nothing and starts nothing.
    //
    // Mutation: on stopping, go back to not started rather than stopped -> the
    // call after the stop runs OnStart a second time -> red.
    RecordingApp app(MixedFrames());
    app.SetFixedTimeStep(TICK);
    EXPECT_EQ(app.starts, 0);
    ASSERT_TRUE(app.RunFrame());
    EXPECT_EQ(app.starts, 1);
    while (app.RunFrame()) {
    }
    EXPECT_FALSE(app.RunFrame());
    EXPECT_FALSE(app.RunFrame());
    EXPECT_EQ(app.starts, 1);
    EXPECT_EQ(app.scene->frames.size(), EXPECTED.size());
}

TEST(FrameStep, RunStartsAfreshEachTime) {
    // As before stepping existed: a second Run starts the application again,
    // OnStart included, though its window has already closed.
    //
    // Mutation: Run does not reset the run to not started -> the second Run
    // finds it stopped and runs no OnStart -> red.
    RecordingApp app(MixedFrames());
    app.SetFixedTimeStep(TICK);
    app.Run();
    EXPECT_EQ(app.starts, 1);
    app.Run();
    EXPECT_EQ(app.starts, 2);
}

TEST(FrameStep, AnApplicationThatQuitsAsItStartsRunsNoFrame) {
    // Whether to go on is asked before every frame, the first included, as
    // Run's loop condition was.
    //
    // Mutation: run the frame first and ask afterwards -> one frame drawn ->
    // red.
    QuitsAtStart stepped;
    EXPECT_FALSE(stepped.RunFrame());
    EXPECT_EQ(stepped.starts, 1);
    EXPECT_TRUE(stepped.scene->frames.empty());

    QuitsAtStart run;
    run.Run();
    EXPECT_EQ(run.starts, 1);
    EXPECT_TRUE(run.scene->frames.empty());
}
