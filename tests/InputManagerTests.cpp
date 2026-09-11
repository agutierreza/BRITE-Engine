// InputManager::ActionDownDevices: which kinds of device are holding an action
// down.
//
// An action is usually bound to more than one device, and IsActionDown cannot say
// which of them is holding it. Anything that has to know what a player is
// playing with -- a prompt showing the right button glyphs, a record of how a
// session was played -- needs the answer without re-reading the binding table,
// which would be a second copy of it.
//
// Inputs reach InputManager the way they do in a running application: the
// backend reports a press, PollVariable queues it, FlushFixed applies it. Beside
// each case is the mutation that turns it red; each was run.

#include <Backends/IInputBackend.hpp>
#include <Core/InputEvents.hpp>
#include <Core/InputManager.hpp>
#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <set>

using BRITE::GamepadButtonCode;
using BRITE::InputDevice;
using BRITE::InputManager;
using BRITE::KeyCode;
using BRITE::MouseButtonCode;

namespace {

// Action ids of their own, so these bindings cannot collide with another suite's
// in the same process: bindings are static and only ever added to.
enum class Action : uint32_t {
    Unbound = 7000,
    KeyOnly,
    Everywhere,
    BothForRelease,
    MouseOnly,
    Stray,
};

/// Reports each input as pressed on exactly one poll and released on exactly
/// one, which is how a real backend reports an edge.
class FakeInputBackend : public BRITE::Backends::IInputBackend {
  public:
    std::set<KeyCode> keysPressed, keysReleased;
    std::set<GamepadButtonCode> padPressed, padReleased;
    std::set<MouseButtonCode> mousePressed, mouseReleased;

    void PollEvents() override {}
    bool IsKeyDown(KeyCode) override {
        return false;
    }
    bool IsKeyPressed(KeyCode key) override {
        return keysPressed.count(key) > 0;
    }
    bool IsKeyReleased(KeyCode key) override {
        return keysReleased.count(key) > 0;
    }
    bool IsMouseButtonDown(MouseButtonCode) override {
        return false;
    }
    bool IsMouseButtonPressed(MouseButtonCode button) override {
        return mousePressed.count(button) > 0;
    }
    bool IsMouseButtonReleased(MouseButtonCode button) override {
        return mouseReleased.count(button) > 0;
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
    bool IsGamepadButtonDown(GamepadButtonCode) override {
        return false;
    }
    bool IsGamepadButtonPressed(GamepadButtonCode button) override {
        return padPressed.count(button) > 0;
    }
    bool IsGamepadButtonReleased(GamepadButtonCode button) override {
        return padReleased.count(button) > 0;
    }
    float GetGamepadAxis(BRITE::GamepadAxisCode axis) override {
        return BRITE::GamepadAxisRestValue(axis);
    }

    void Clear() {
        keysPressed.clear();
        keysReleased.clear();
        padPressed.clear();
        padReleased.clear();
        mousePressed.clear();
        mouseReleased.clear();
    }
};

class InputManagerDevices : public ::testing::Test {
  protected:
    void SetUp() override {
        InputManager::Initialize(&m_backend);
    }

    // Leave nothing held for the next case in this process: every input these
    // cases touch is released, then the backend is detached.
    void TearDown() override {
        m_backend.Clear();
        m_backend.keysReleased = {KeyCode::G, KeyCode::H, KeyCode::J, KeyCode::K};
        m_backend.padReleased = {GamepadButtonCode::RightFaceDown, GamepadButtonCode::RightFaceUp};
        m_backend.mouseReleased = {MouseButtonCode::Right};
        Tick();
        InputManager::Initialize(nullptr);
    }

    // One render-frame poll and one fixed tick, then the edges are over.
    void Tick() {
        InputManager::PollVariable(m_dispatcher);
        InputManager::FlushFixed(m_dispatcher);
        m_backend.Clear();
    }

    FakeInputBackend m_backend;
    entt::dispatcher m_dispatcher;
};

} // namespace

TEST_F(InputManagerDevices, NothingHeldIsNoDevice) {
    // An action with no binding at all, and one whose binding is up.
    InputManager::BindAction(Action::KeyOnly, KeyCode::G);
    Tick();
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Unbound), InputDevice::None);
    EXPECT_EQ(InputManager::ActionDownDevices(Action::KeyOnly), InputDevice::None);
    EXPECT_FALSE(InputManager::IsActionDown(Action::KeyOnly));
}

TEST_F(InputManagerDevices, EachDeviceIsReportedAsItself) {
    // One action bound to a key, a pad button and nothing else. Held on the key
    // it is the keyboard; released and held on the pad it is the gamepad;
    // held on both it is both.
    //
    // Mutations: report a pad button as Keyboard -> the second check -> red.
    // Stop at the first device found (return after the key loop) -> the third
    // reads Keyboard alone -> red. Drop the gamepad loop -> the second and third
    // -> red.
    InputManager::BindAction(Action::Everywhere, KeyCode::H);
    InputManager::BindAction(Action::Everywhere, GamepadButtonCode::RightFaceDown);

    m_backend.keysPressed = {KeyCode::H};
    Tick();
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Everywhere), InputDevice::Keyboard);

    m_backend.keysReleased = {KeyCode::H};
    m_backend.padPressed = {GamepadButtonCode::RightFaceDown};
    Tick();
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Everywhere), InputDevice::Gamepad);

    m_backend.keysPressed = {KeyCode::H};
    Tick();
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Everywhere), InputDevice::Keyboard | InputDevice::Gamepad);
    EXPECT_TRUE(InputManager::IsActionDown(Action::Everywhere));
}

TEST_F(InputManagerDevices, ReleasingOneDeviceLeavesTheOther) {
    // Held on the key and the pad, then the key lets go: the pad still holds
    // it, so the action is still down, on the gamepad alone.
    //
    // Mutation: never clear a device once seen (accumulate across calls) ->
    // Keyboard | Gamepad after the release -> red.
    InputManager::BindAction(Action::BothForRelease, KeyCode::J);
    InputManager::BindAction(Action::BothForRelease, GamepadButtonCode::RightFaceUp);
    m_backend.keysPressed = {KeyCode::J};
    m_backend.padPressed = {GamepadButtonCode::RightFaceUp};
    Tick();
    ASSERT_EQ(InputManager::ActionDownDevices(Action::BothForRelease), InputDevice::Keyboard | InputDevice::Gamepad);

    m_backend.keysReleased = {KeyCode::J};
    Tick();
    EXPECT_EQ(InputManager::ActionDownDevices(Action::BothForRelease), InputDevice::Gamepad);
    EXPECT_TRUE(InputManager::IsActionDown(Action::BothForRelease));
}

TEST_F(InputManagerDevices, TheMouseIsADeviceToo) {
    // Mutation: report a mouse button as Keyboard -> red.
    InputManager::BindAction(Action::MouseOnly, MouseButtonCode::Right);
    m_backend.mousePressed = {MouseButtonCode::Right};
    Tick();
    EXPECT_EQ(InputManager::ActionDownDevices(Action::MouseOnly), InputDevice::Mouse);
}

TEST_F(InputManagerDevices, AnInputNotBoundToTheActionDoesNotCount) {
    // K is held but bound to nothing; the action is bound to a pad button that
    // is up. The keyboard is in use, but not for this action.
    //
    // Mutation: report Keyboard whenever any key is down -> red.
    InputManager::BindAction(Action::Stray, GamepadButtonCode::RightFaceDown);
    m_backend.keysPressed = {KeyCode::K};
    Tick();
    ASSERT_TRUE(InputManager::IsKeyDown(KeyCode::K));
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Stray), InputDevice::None);
    EXPECT_FALSE(InputManager::IsActionDown(Action::Stray));
}

TEST(InputDeviceFlags, CombineAndTest) {
    // Keyboard is bit 0 and Gamepad bit 1: together 0b011 = 3, and each flag is
    // included in the pair while Mouse (bit 2) is not.
    //
    // Mutation: make operator| an AND -> None -> red.
    const InputDevice both = InputDevice::Keyboard | InputDevice::Gamepad;
    EXPECT_EQ(static_cast<uint8_t>(both), 3u);
    EXPECT_TRUE(BRITE::Includes(both, InputDevice::Keyboard));
    EXPECT_TRUE(BRITE::Includes(both, InputDevice::Gamepad));
    EXPECT_FALSE(BRITE::Includes(both, InputDevice::Mouse));
    InputDevice accumulated = InputDevice::None;
    accumulated |= InputDevice::Mouse;
    EXPECT_EQ(accumulated, InputDevice::Mouse);
}
