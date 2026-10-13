#pragma once

// The profiler's lifetime, owned by the framework.
//
// Tracy is built with TRACY_MANUAL_LIFETIME (CMakeLists.txt): it starts when Start() is called and
// stops when Stop() is called, instead of starting during static initialisation and stopping during
// static destruction. The Application starts it in its constructor and stops it in its destructor,
// so the profiler -- and every thread it runs -- ends while main() is still running.
//
// Why not static destruction. Tracy's worker thread keeps a pointer from Tracy's event queue to a
// thread_local token of its own, cleared by that token's destructor when the thread ends. When the
// profiler is a static object, its destructor joins the worker during exit(), and a runtime that
// skips thread_local destructors for threads ending during exit() (mingw-w64's does) leaves the
// pointer behind; the queue's destructor, which runs next, then writes through it into the dead
// thread's freed storage. Ending the threads inside main() lets their destructors run.
//
// Two consequences a caller can rely on:
//   * A zone opened with BRITE_PROFILE_ZONE while the profiler is not running records nothing and
//     touches nothing of Tracy's -- in a process with no Application, and after it is gone. (A raw
//     Tracy macro there would dereference a profiler that does not exist.)
//   * The profiler runs ONCE per process: Start() after Stop() does nothing and returns false. Tracy
//     makes no promise about starting a second time, so a second Application in the same process
//     runs without the profiler and says so.
//
// Stop() must be called when no zone is open on any thread: a zone begun while the profiler ran
// and ended after it stopped would end in a profiler that no longer exists. The framework's own
// zones run on the thread that runs the Application, which is the thread that stops it.

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#endif

namespace BRITE::Profiler {

enum class State { NeverStarted, Running, Stopped };

/// Start the profiler, if it has never run in this process. Returns whether this call started it.
bool Start();

/// Stop the profiler and end its threads, if it is running. Every zone must be closed first.
void Stop();

State CurrentState();

/// Is the profiler running now? What BRITE_PROFILE_ZONE asks.
bool IsRunning();

} // namespace BRITE::Profiler

#if defined(TRACY_ENABLE)
/// A profiling zone for the enclosing scope, recorded while the profiler runs and inert otherwise.
#define BRITE_PROFILE_ZONE ZoneNamed(___brite_profile_zone, ::BRITE::Profiler::IsRunning())
#else
#define BRITE_PROFILE_ZONE
#endif
