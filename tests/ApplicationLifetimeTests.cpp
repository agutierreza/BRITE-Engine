// Application's lifetime against state that belongs to the whole process.
//
// An Application is one object; the logger is the process's. When the first
// Application's destructor shut the logger down for good, the next one in the
// same process logged its first line through a logger that no longer existed,
// and the process died there. A test run as separate processes never met a
// second Application, which is how it stayed hidden.
//
// Beside the case is the mutation that turns it red, and it was run.

#include <Framework/Application.hpp>
#include <gtest/gtest.h>

#include <spdlog/spdlog.h>

#include <memory>

using brite::framework::Application;

namespace {
// An Application with no window, no input and no renderer: enough to run its
// constructor and destructor, which is all this is about.
std::unique_ptr<Application> Bare(const char* name) {
    return std::make_unique<Application>(nullptr, nullptr, nullptr, name, "BRITE", "Engine", 1, 1);
}
} // namespace

// Two Applications, one after the other, in one process. After the first is
// destroyed the process's logger is still there to log through; the second is
// built and destroyed on it, and the logger is still there after that too.
//
// The logger is checked with ASSERT before the second is built, so the
// mutation fails an assertion here rather than crashing inside the second
// constructor, which would not say what broke.
//
// Mutation: the unconditional spdlog::shutdown() restored in ~Application ->
// the default logger is null after the first, and the ASSERT fails.
TEST(ApplicationLifetime, ASecondApplicationInOneProcessLogsThroughTheSameLogger) {
    Bare("first").reset();
    ASSERT_NE(spdlog::default_logger_raw(), nullptr) << "the first Application took the logger with it";

    auto second = Bare("second");
    spdlog::info("a line from between the two Applications' lifetimes");
    second.reset();
    EXPECT_NE(spdlog::default_logger_raw(), nullptr);
}
