// Application::TickFraction: the residual of the fixed timestep, exposed.
//
// A fixed-timestep loop that renders faster than it simulates draws the same
// state on every frame between two ticks. The fraction of a tick the clock has
// advanced past the last one is what lets a consumer interpolate; without it
// every consumer draws in steps. These cases drive the real Application::Run
// loop against a scripted clock -- a window backend whose GetTime returns the
// frame times fed to it and whose window "closes" when they run out
// (ScriptedFrames.hpp) -- and read the fraction from the phase it is
// documented for, OnRender.
//
// Frame times are binary-exact fractions of a second: a tick of 1/64 s and a
// frame every 1/512 s, so eight frames make exactly one tick and every expected
// fraction is exact rather than a rounding away from the tick boundary. Beside
// each case is the mutation that turns it red; each was run.

#include "ScriptedFrames.hpp"
#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

using namespace scripted_frames;

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
