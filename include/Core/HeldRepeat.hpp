#pragma once

namespace BRITE {

// A repeat for an input the operating system does not repeat: a gamepad button,
// a mouse button, an axis held past a threshold. It turns "held on this step"
// into presses -- one when the hold begins, then one when it has lasted `delay`
// seconds, then one every `period` seconds after that, until it is let go.
//
// A pure clock: it reads no input and no time of its own, so a caller steps it
// with whatever it calls held and with its own step's length, and a test steps
// it with no backend. The delay and the period are the caller's; there are no
// defaults, because how soon and how fast a held input should repeat is the
// application's to choose. (A key held down is repeated by the operating system
// at the user's own settings: read InputManager::IsKeyPressedOrRepeated for that
// instead of timing it here.)
//
// Times are the time the input has been held, counted from the step on which
// the hold began: a repeat due at `delay` fires on the first step that ends at
// or after `delay` seconds of holding, and so on for each period. A step longer
// than a period fires every repeat that fell due inside it, so none is lost when
// steps are long.
//
// An axis held one way, then the other, is two inputs: step one HeldRepeat with
// "pushed past the threshold toward one side" and a second with "toward the
// other", so a stick swung across restarts the delay.
class HeldRepeat {
  public:
    // `delaySeconds` from the hold beginning to the first repeat, at least 0;
    // `periodSeconds` between repeats after that, above 0. Asserted in Debug; in
    // Release a period that is not above 0 repeats once, at the delay, and never
    // again, rather than without end.
    HeldRepeat(float delaySeconds, float periodSeconds);

    // One step of `dtSeconds`, with the input held during it or not. Returns the
    // number of presses this step: 1 on the step the hold begins (the press
    // itself), then the repeats that fell due during this step -- usually 0 or 1,
    // more only for a step longer than the period. 0 while the input is not held.
    // A step that is not held ends the hold, so the next hold starts over with
    // its press and the full delay.
    int Step(bool held, float dtSeconds);

    // End the hold as if the input had been let go, for a reader that stops
    // stepping while it is still held -- a list that lost focus -- so that it
    // starts over when it is stepped again.
    void Reset();

    // Whether a hold is in progress: the last step was held and nothing has
    // ended it since.
    bool IsHeld() const {
        return m_held;
    }

  private:
    float m_delaySeconds;
    float m_periodSeconds;
    bool m_held = false;
    // Seconds held since the step the hold began, summed in double so a hold of
    // minutes still lands its repeats on the right step.
    double m_heldSeconds = 0.0;
    int m_repeatsFired = 0; // repeats fired in this hold, the press not counted
};

} // namespace BRITE
