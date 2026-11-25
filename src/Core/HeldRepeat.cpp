#include "Core/HeldRepeat.hpp"

#include <cassert>
#include <cmath>

namespace BRITE {

HeldRepeat::HeldRepeat(float delaySeconds, float periodSeconds)
    : m_delaySeconds(delaySeconds), m_periodSeconds(periodSeconds) {
    assert(delaySeconds >= 0.0f && "a repeat cannot come before the press that began the hold");
    assert(periodSeconds > 0.0f && "a period of zero would repeat without end");
}

int HeldRepeat::Step(bool held, float dtSeconds) {
    if (!held) {
        Reset();
        return 0;
    }
    if (!m_held) {
        // The hold begins on this step, and this step is its press. Its time is
        // counted from here, so this step's own length is not part of it. The
        // time and the repeats start from zero: nothing but Reset ends a hold,
        // and a clock starts as Reset leaves it.
        m_held = true;
        return 1;
    }
    m_heldSeconds += dtSeconds;
    // The repeats due by now: none before the delay, then one at the delay and
    // one more at the end of each whole period after it.
    int due = 0;
    if (m_heldSeconds >= m_delaySeconds) {
        due = 1;
        if (m_periodSeconds > 0.0f)
            due += static_cast<int>(std::floor((m_heldSeconds - m_delaySeconds) / m_periodSeconds));
    }
    const int fired = due - m_repeatsFired;
    m_repeatsFired = due;
    return fired;
}

void HeldRepeat::Reset() {
    m_held = false;
    m_heldSeconds = 0.0;
    m_repeatsFired = 0;
}

} // namespace BRITE
