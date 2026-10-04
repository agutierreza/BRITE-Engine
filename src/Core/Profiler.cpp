#include "Profiler.hpp"

#include <atomic>
#include <mutex>

namespace BRITE::Profiler {

namespace {
// Start and Stop take the lock; IsRunning, which every zone asks, only reads the state. The state
// says Running only after Tracy has started, and stops saying it before Tracy is shut down, so a
// zone that sees Running finds a profiler there.
std::mutex g_lock;
std::atomic<State> g_state{State::NeverStarted};
} // namespace

bool Start() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_state.load() != State::NeverStarted)
        return false;
#if defined(TRACY_ENABLE) && defined(TRACY_MANUAL_LIFETIME)
    tracy::StartupProfiler();
#endif
    g_state.store(State::Running, std::memory_order_release);
    return true;
}

void Stop() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_state.load() != State::Running)
        return;
    g_state.store(State::Stopped, std::memory_order_release);
#if defined(TRACY_ENABLE) && defined(TRACY_MANUAL_LIFETIME)
    tracy::ShutdownProfiler(); // joins Tracy's threads: they end here, inside main
#endif
}

State CurrentState() {
    return g_state.load(std::memory_order_acquire);
}

bool IsRunning() {
    return CurrentState() == State::Running;
}

} // namespace BRITE::Profiler
