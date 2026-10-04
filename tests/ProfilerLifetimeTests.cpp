// The profiler's lifetime (Core/Profiler.hpp): it runs while an Application lives, a profiler
// client can connect to it then, its threads have ended by the time the Application is gone, and
// a zone with no profiler running records nothing and is harmless.
//
// The lifetime case needs a process in which the profiler has never run, so it runs this binary
// again as a probe (PROBE in its environment, the case's own name as the filter) and reads what
// the probe printed. The probe finds its OWN profiler's port in the system's table of listening
// sockets, by process id, so it never connects to another process's profiler.
//
// Beside each case is the mutation that turns it red, and it was run.

#include <Core/Profiler.hpp>
#include <Framework/Application.hpp>
#include <gtest/gtest.h>

#if defined(_WIN32) && defined(TRACY_ENABLE)

#include <common/TracyProtocol.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <thread>

using brite::framework::Application;
namespace Profiler = BRITE::Profiler;

namespace {

constexpr const char* PROBE = "BRITE_PROFILER_PROBE";

std::unique_ptr<Application> Bare() {
    return std::make_unique<Application>(nullptr, nullptr, nullptr, "ProfilerLifetime", "BRITE", "Engine", 1, 1);
}

int g_zonesRun = 0;
void ARoutineWithAZone() {
    BRITE_PROFILE_ZONE;
    ++g_zonesRun;
}

const char* Name(Profiler::State state) {
    switch (state) {
    case Profiler::State::NeverStarted:
        return "never started";
    case Profiler::State::Running:
        return "running";
    case Profiler::State::Stopped:
        return "stopped";
    }
    return "?";
}

/// How many of this process's threads are the profiler's: their names begin "Tracy".
int TracysThreads() {
    int count = 0;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return -1;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof entry;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId())
            continue;
        const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (thread == nullptr)
            continue;
        PWSTR name = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &name)) && name != nullptr) {
            if (std::wcsncmp(name, L"Tracy", 5) == 0)
                ++count;
            LocalFree(name);
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return count;
}

/// The TCP port this process listens on (IPv6 or IPv4), or 0: the profiler's, since nothing else
/// in this binary listens.
unsigned short OwnListeningPort() {
    const DWORD self = GetCurrentProcessId();
    for (const ULONG family : {AF_INET6, AF_INET}) {
        DWORD size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        std::string buffer(size, '\0');
        if (GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
            continue;
        if (family == AF_INET6) {
            const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i)
                if (table->table[i].dwOwningPid == self)
                    return ntohs(static_cast<u_short>(table->table[i].dwLocalPort));
        } else {
            const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i)
                if (table->table[i].dwOwningPid == self)
                    return ntohs(static_cast<u_short>(table->table[i].dwLocalPort));
        }
    }
    return 0;
}

/// Connect to `port` on this machine as a profiler client does -- the handshake, then the welcome
/// -- and return the process id the welcome names, or 0 if the handshake did not complete.
unsigned long long HandshakeWelcomesPid(unsigned short port) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    unsigned long long pid = 0;
    const SOCKET s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(port);
    address.sin6_addr = in6addr_loopback;
    DWORD timeout = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    if (s != INVALID_SOCKET && connect(s, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0) {
        const uint32_t version = tracy::ProtocolVersion;
        send(s, tracy::HandshakeShibboleth, tracy::HandshakeShibbolethSize, 0);
        send(s, reinterpret_cast<const char*>(&version), sizeof version, 0);
        tracy::HandshakeStatus status = tracy::HandshakePending;
        if (recv(s, reinterpret_cast<char*>(&status), sizeof status, MSG_WAITALL) == sizeof status &&
            status == tracy::HandshakeWelcome) {
            tracy::WelcomeMessage welcome{};
            if (recv(s, reinterpret_cast<char*>(&welcome), sizeof welcome, MSG_WAITALL) == sizeof welcome)
                pid = welcome.pid;
        }
    }
    if (s != INVALID_SOCKET)
        closesocket(s);
    WSACleanup();
    return pid;
}

/// The probe: one Application's life, said line by line.
void PlayTheProbe() {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // every line out as it is said, should the probe die
    std::printf("[profiler] before any Application: %s, %d Tracy thread(s)\n", Name(Profiler::CurrentState()),
                TracysThreads());
    {
        auto app = Bare();
        std::printf("[profiler] with an Application: %s, %d Tracy thread(s)\n", Name(Profiler::CurrentState()),
                    TracysThreads());
        // The profiler opens its port once it has calibrated its clock: wait for it, bounded.
        unsigned short port = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((port = OwnListeningPort()) == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const unsigned long long pid = port != 0 ? HandshakeWelcomesPid(port) : 0;
        std::printf("[profiler] a profiler client connecting to port %u was welcomed by process %llu, this is %lu\n",
                    port, pid, GetCurrentProcessId());
        ARoutineWithAZone();
        std::printf("[profiler] a zone inside it: run\n");
    }
    std::printf("[profiler] after the Application: %s, %d Tracy thread(s)\n", Name(Profiler::CurrentState()),
                TracysThreads());
    ARoutineWithAZone();
    std::printf("[profiler] a zone after it: run, %d zone(s) in all\n", g_zonesRun);
    std::fflush(stdout);
}

std::string ThisExecutable() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return path;
}

/// Run this binary's current case as the probe; its exit code and everything it printed.
std::string RunTheProbe(DWORD& exitCode) {
    const ::testing::TestInfo* self = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string commandLine =
        "\"" + ThisExecutable() + "\" --gtest_filter=" + self->test_suite_name() + "." + self->name();
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    CreatePipe(&readEnd, &writeEnd, &inheritable, 0);
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION process{};
    std::string line = commandLine;
    _putenv_s(PROBE, "1");
    const BOOL created = CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &startup, &process);
    _putenv_s(PROBE, "");
    CloseHandle(writeEnd);
    std::string out;
    if (!created) {
        CloseHandle(readEnd);
        exitCode = static_cast<DWORD>(-1);
        return "the probe did not start";
    }
    // Read on a thread, so a probe that hangs is ended at the bound instead of holding the read forever.
    std::thread reader([&] {
        char buffer[4096];
        DWORD got = 0;
        while (ReadFile(readEnd, buffer, sizeof buffer, &got, nullptr) && got > 0)
            out.append(buffer, got);
    });
    if (WaitForSingleObject(process.hProcess, 60000) != WAIT_OBJECT_0)
        TerminateProcess(process.hProcess, 1);
    reader.join();
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(readEnd);
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end()); // its stdout is in text mode
    return out;
}

bool Has(const std::string& out, const std::string& text) {
    return out.find(text) != std::string::npos;
}

} // namespace

TEST(ProfilerLifetime, ItRunsForAnApplicationsLifeAndItsThreadsEndBeforeTheApplicationIsGone) {
    // In a fresh process the profiler has never run and has no thread. An Application starts it:
    // running, with threads, and a profiler client that connects is welcomed -- by THIS process,
    // the welcome says. When the Application is gone the profiler is stopped and not one of its
    // threads remains: they ended inside main. A zone after that runs and records nothing.
    // MUTATIONS, run: the Application never starts the profiler -> "with an Application: never
    // started", red; it never stops it -> "after the Application: running" with threads, red; Stop()
    // forgets to shut Tracy down -> "stopped" with threads still there, red. In a sanitizer build,
    // Tracy's crash handler left in -> the probe dies at its first zone with a client connected, red.
    if (std::getenv(PROBE) != nullptr) {
        PlayTheProbe();
        return;
    }
    DWORD exitCode = 0;
    const std::string out = RunTheProbe(exitCode);
    EXPECT_EQ(exitCode, 0u) << out;
    EXPECT_TRUE(Has(out, "[profiler] before any Application: never started, 0 Tracy thread(s)\n")) << out;
    EXPECT_TRUE(Has(out, "[profiler] with an Application: running, ")) << out;
    EXPECT_FALSE(Has(out, "[profiler] with an Application: running, 0 Tracy thread(s)")) << out;
    EXPECT_TRUE(Has(out, "[profiler] after the Application: stopped, 0 Tracy thread(s)\n")) << out;
    EXPECT_TRUE(Has(out, "[profiler] a zone after it: run, 2 zone(s) in all\n")) << out;
    // The welcome names the probe itself: "welcomed by process N, this is N".
    const std::size_t at = out.find("was welcomed by process ");
    ASSERT_NE(at, std::string::npos) << out;
    unsigned long long welcomed = 0;
    unsigned long self = 0;
    ASSERT_EQ(std::sscanf(out.c_str() + at, "was welcomed by process %llu, this is %lu", &welcomed, &self), 2) << out;
    EXPECT_NE(welcomed, 0ull) << out;
    EXPECT_EQ(welcomed, self) << out;
}

TEST(ProfilerLifetime, AZoneWithNoProfilerRunningIsHarmlessAndStartsNothing) {
    // With no Application alive the profiler is not running -- never started, or stopped by one
    // that has gone -- and a zone must not touch it: Tracy built for a manual lifetime has no
    // profiler to touch, and dereferences nothing only because the zone is opened inactive. A
    // thousand zones later: the same state, and still not one Tracy thread.
    // MUTATION, run: BRITE_PROFILE_ZONE opened active regardless -> the first zone dereferences a
    // profiler that does not exist and the process dies, red.
    if (std::getenv(PROBE) != nullptr)
        return;
    ASSERT_FALSE(Profiler::IsRunning());
    const Profiler::State before = Profiler::CurrentState();
    const int zonesBefore = g_zonesRun;
    for (int i = 0; i < 1000; ++i)
        ARoutineWithAZone();
    EXPECT_EQ(g_zonesRun - zonesBefore, 1000);
    EXPECT_EQ(Profiler::CurrentState(), before);
    EXPECT_EQ(TracysThreads(), 0);
}

#endif // _WIN32 && TRACY_ENABLE
