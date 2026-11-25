// HeldRepeat: a repeat for an input the operating system does not repeat -- a
// press when the hold begins, a repeat at the delay, then one every period.
//
// Stepped by hand, with no backend and no clock: the step length, the delay and
// the period are chosen so every time involved is a sum of exact binary
// fractions (0.125, 0.25, 0.5), so each expected step is the arithmetic below
// and not a rounding. The delay (0.5 s) is twice the period (0.25 s), so a clock
// that used one for the other fires on different steps. Beside each case is the
// mutation that turns it red; each was run.

#include <Core/HeldRepeat.hpp>
#include <gtest/gtest.h>

#include <vector>

using BRITE::HeldRepeat;

namespace {
constexpr float Delay = 0.5f;
constexpr float Period = 0.25f;
constexpr float Dt = 0.125f;

// Steps `clock` held for `steps` steps of Dt and returns what each fired.
std::vector<int> Hold(HeldRepeat& clock, int steps) {
    std::vector<int> fired;
    for (int i = 0; i < steps; ++i)
        fired.push_back(clock.Step(true, Dt));
    return fired;
}
} // namespace

TEST(HeldRepeatClock, APressWhenTheHoldBeginsThenARepeatAtTheDelayThenOneEveryPeriod) {
    // Step 0 begins the hold: its press, 1, held 0 s. Each later step adds
    // 0.125 s, so step k ends k * 0.125 s into the hold:
    //   k = 1, 2, 3 -> 0.125, 0.25, 0.375 s: before the 0.5 s delay, 0;
    //   k = 4       -> 0.5 s: the delay, the first repeat, 1;
    //   k = 5       -> 0.625 s: 0;
    //   k = 6       -> 0.75 s = 0.5 + 0.25: one period on, 1;
    //   k = 7       -> 0.875 s: 0;
    //   k = 8       -> 1.0 s = 0.5 + 2 * 0.25: 1.
    // MUTATION: the first repeat at the period instead of the delay -- k = 2
    // fires, red; the repeats every delay instead of every period -- k = 6 reads
    // 0, red; the press not fired (0 at k = 0) -- red.
    HeldRepeat clock(Delay, Period);
    const std::vector<int> expected{1, 0, 0, 0, 1, 0, 1, 0, 1};
    EXPECT_EQ(Hold(clock, 9), expected);
    EXPECT_TRUE(clock.IsHeld());
}

TEST(HeldRepeatClock, ALongStepFiresEveryRepeatThatFellDueInsideIt) {
    // The press, then one step of 1.0 s: the hold has lasted 1.0 s, and the
    // repeats due by then are at 0.5, 0.75 and 1.0 s -- three, all fired on that
    // step. Then a step of 0.125 s ends at 1.125 s, before the next at 1.25 s: 0.
    // MUTATION: at most one repeat a step -- 1, red.
    HeldRepeat clock(Delay, Period);
    EXPECT_EQ(clock.Step(true, Dt), 1);
    EXPECT_EQ(clock.Step(true, 1.0f), 3);
    EXPECT_EQ(clock.Step(true, Dt), 0);
}

TEST(HeldRepeatClock, TheStepTheHoldBeginsDoesNotCountItsOwnLength) {
    // The hold's time is counted from the step on which it began: a first step
    // as long as the delay is still only the press, and the step after it, of
    // 0.125 s, ends 0.125 s into the hold, well before the 0.5 s delay: 0.
    // MUTATION: the first step's length counted -- the second step ends at
    // 0.625 s, past the delay, and fires 1, red.
    HeldRepeat clock(Delay, Period);
    EXPECT_EQ(clock.Step(true, Delay), 1);
    EXPECT_EQ(clock.Step(true, Dt), 0);
}

TEST(HeldRepeatClock, LettingGoStartsOverWithAPressAndTheFullDelay) {
    // Held to its first repeat (the press and four steps: 1, 0, 0, 0, 1), then
    // let go: 0, and no longer held. Held again, the hold begins anew: its
    // press, 1, then the full 0.5 s delay before a repeat -- 0, 0, 0, then 1 at
    // the fourth step. Steps of 0.125 s, as in the first case.
    // MUTATION: a step not held not ending the hold -- the second hold fires no
    // press, red; the reset keeping the time held (0.5 s) and the repeats fired
    // (1) -- the second hold's second step ends at 0.75 s, due 2 with 1 fired,
    // and fires 1, red.
    HeldRepeat clock(Delay, Period);
    const std::vector<int> toFirstRepeat{1, 0, 0, 0, 1};
    EXPECT_EQ(Hold(clock, 5), toFirstRepeat);
    EXPECT_EQ(clock.Step(false, Dt), 0);
    EXPECT_FALSE(clock.IsHeld());
    EXPECT_EQ(Hold(clock, 5), toFirstRepeat);
}

TEST(HeldRepeatClock, ResetEndsAHoldThatIsStillHeld) {
    // A reader that stops stepping mid-hold resets: the next held step is a new
    // hold's press, 1, though the input never went up.
    // MUTATION: Reset doing nothing -- the step after it continues the old hold,
    // 0.375 s in, and fires 0, red.
    HeldRepeat clock(Delay, Period);
    Hold(clock, 3);
    clock.Reset();
    EXPECT_FALSE(clock.IsHeld());
    EXPECT_EQ(clock.Step(true, Dt), 1);
}

TEST(HeldRepeatClock, NotHeldFiresNothing) {
    // An input never held fires nothing, however long it is stepped.
    HeldRepeat clock(Delay, Period);
    EXPECT_EQ(clock.Step(false, 1.0f), 0);
    EXPECT_EQ(clock.Step(false, 1.0f), 0);
    EXPECT_FALSE(clock.IsHeld());
}
