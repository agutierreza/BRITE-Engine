// Application's lifetime against state that belongs to the whole process.
//
// An Application is one object; the logger is the process's. When the first
// Application's destructor shut the logger down for good, the next one in the
// same process logged its first line through a logger that no longer existed,
// and the process died there. A test run as separate processes never met a
// second Application, which is how it stayed hidden.
//
// Beside the case is the mutation that turns it red, and it was run.

#include <Backends/IInputBackend.hpp>
#include <Core/InputManager.hpp>
#include <Framework/Application.hpp>
#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <spdlog/spdlog.h>

#include <memory>

using brite::framework::Application;

namespace {
// An Application with no window, no input and no renderer: enough to run its
// constructor and destructor, which is all this is about.
std::unique_ptr<Application> Bare(const char* name) {
    return std::make_unique<Application>(nullptr, nullptr, nullptr, name, "BRITE", "Engine", 1, 1);
}

// An input backend that reports nothing pressed, anywhere.
class QuietInput : public BRITE::Backends::IInputBackend {
  public:
    void PollEvents() override {}
    bool IsKeyDown(BRITE::KeyCode) override {
        return false;
    }
    bool IsKeyPressed(BRITE::KeyCode) override {
        return false;
    }
    bool IsKeyReleased(BRITE::KeyCode) override {
        return false;
    }
    bool IsMouseButtonDown(BRITE::MouseButtonCode) override {
        return false;
    }
    bool IsMouseButtonPressed(BRITE::MouseButtonCode) override {
        return false;
    }
    bool IsMouseButtonReleased(BRITE::MouseButtonCode) override {
        return false;
    }
    float GetMouseX() override {
        return 0.0f;
    }
    float GetMouseY() override {
        return 0.0f;
    }
    float GetMouseDeltaX() override {
        return 0.0f;
    }
    float GetMouseDeltaY() override {
        return 0.0f;
    }
    bool IsGamepadButtonDown(BRITE::GamepadButtonCode) override {
        return false;
    }
    bool IsGamepadButtonPressed(BRITE::GamepadButtonCode) override {
        return false;
    }
    bool IsGamepadButtonReleased(BRITE::GamepadButtonCode) override {
        return false;
    }
    float GetGamepadAxis(BRITE::GamepadAxisCode axis) override {
        return BRITE::GamepadAxisRestValue(axis);
    }
};

// An Application whose only backend is input; `input` is left pointing at it,
// alive for exactly as long as the Application is.
std::unique_ptr<Application> WithInput(const char* name, BRITE::Backends::IInputBackend*& input) {
    auto backend = std::make_unique<QuietInput>();
    input = backend.get();
    return std::make_unique<Application>(nullptr, std::move(backend), nullptr, name, "BRITE", "Engine", 1, 1);
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

// The input manager is process-wide too, and keeps a pointer to the backend it
// polls. An Application with an input backend hands it over; destroyed, it takes
// it back, so a second Application without one -- whose Run polls the manager
// all the same -- finds none, the case the manager guards, and not a pointer to
// a freed backend. The manager is checked with ASSERT before the poll, so the
// mutation fails an assertion rather than reading freed memory.
//
// Mutation: the destructor leaving the manager's backend set -> it still points
// at the first Application's destroyed backend, and the ASSERT fails.
TEST(ApplicationLifetime, ADestroyedApplicationTakesItsInputBackendBackFromTheManager) {
    BRITE::Backends::IInputBackend* first = nullptr;
    auto withInput = WithInput("first", first);
    ASSERT_EQ(BRITE::InputManager::Backend(), first) << "handed over";
    withInput.reset();
    ASSERT_EQ(BRITE::InputManager::Backend(), nullptr) << "taken back with the Application";

    auto withoutInput = Bare("second");
    EXPECT_EQ(BRITE::InputManager::Backend(), nullptr);
    entt::dispatcher dispatcher;
    BRITE::InputManager::PollVariable(dispatcher); // the poll Run makes: with no backend, nothing
    withoutInput.reset();
}

// An Application without an input backend leaves the manager alone, coming and
// going: a live Application's input is still the manager's afterwards.
//
// Mutations: an Application without input handing the manager null when it
// starts -> the live one's input is switched off; clearing the manager whether
// or not the backend is its own -> the same, when it is destroyed.
TEST(ApplicationLifetime, AnApplicationWithoutInputLeavesALiveOnesInputAlone) {
    BRITE::Backends::IInputBackend* live = nullptr;
    auto withInput = WithInput("live", live);
    Bare("passing").reset();
    EXPECT_EQ(BRITE::InputManager::Backend(), live);
    withInput.reset();
    EXPECT_EQ(BRITE::InputManager::Backend(), nullptr);
}

// A later Application that brings its own input backend takes the manager
// over; the earlier one, destroyed after that, does not clear the later one's.
//
// Mutation: the destructor clearing the manager without asking whose backend
// it holds -> null while the later Application is alive.
TEST(ApplicationLifetime, AnApplicationClearsOnlyTheInputBackendThatIsStillItsOwn) {
    BRITE::Backends::IInputBackend* earlier = nullptr;
    BRITE::Backends::IInputBackend* later = nullptr;
    auto first = WithInput("earlier", earlier);
    auto second = WithInput("later", later);
    ASSERT_EQ(BRITE::InputManager::Backend(), later) << "taken over";
    first.reset();
    EXPECT_EQ(BRITE::InputManager::Backend(), later) << "the later one's, still";
    second.reset();
    EXPECT_EQ(BRITE::InputManager::Backend(), nullptr);
}
