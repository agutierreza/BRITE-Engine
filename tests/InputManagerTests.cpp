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
    Claimable,
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
    bool padAvailable = false;
    bool IsGamepadAvailable() override {
        return padAvailable;
    }
    std::string padName;
    std::string GetGamepadName() override {
        return padName;
    }
    // The wheel's movement this frame, as a backend reports it once a frame.
    float wheelX = 0.0f, wheelY = 0.0f;
    float GetMouseWheelMoveX() override {
        return wheelX;
    }
    float GetMouseWheelMoveY() override {
        return wheelY;
    }

    void Clear() {
        keysPressed.clear();
        keysReleased.clear();
        padPressed.clear();
        padReleased.clear();
        mousePressed.clear();
        mouseReleased.clear();
        wheelX = 0.0f;
        wheelY = 0.0f;
    }
};

/// Answers only what IInputBackend makes pure, so every default it has is the
/// one reported.
class SilentInputBackend : public BRITE::Backends::IInputBackend {
  public:
    void PollEvents() override {}
    bool IsKeyDown(KeyCode) override {
        return false;
    }
    bool IsKeyPressed(KeyCode) override {
        return false;
    }
    bool IsKeyReleased(KeyCode) override {
        return false;
    }
    bool IsMouseButtonDown(MouseButtonCode) override {
        return false;
    }
    bool IsMouseButtonPressed(MouseButtonCode) override {
        return false;
    }
    bool IsMouseButtonReleased(MouseButtonCode) override {
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
    bool IsGamepadButtonDown(GamepadButtonCode) override {
        return false;
    }
    bool IsGamepadButtonPressed(GamepadButtonCode) override {
        return false;
    }
    bool IsGamepadButtonReleased(GamepadButtonCode) override {
        return false;
    }
    float GetGamepadAxis(BRITE::GamepadAxisCode axis) override {
        return BRITE::GamepadAxisRestValue(axis);
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
        m_backend.keysReleased = {KeyCode::G, KeyCode::H, KeyCode::J, KeyCode::K, KeyCode::L};
        m_backend.padReleased = {GamepadButtonCode::RightFaceDown, GamepadButtonCode::RightFaceUp,
                                 GamepadButtonCode::LeftFaceDown};
        m_backend.mouseReleased = {MouseButtonCode::Right};
        m_backend.padAvailable = false;
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

// -----------------------------------------------------------------------------
// ClaimKey: a layer over the application takes a key away from its actions.
// -----------------------------------------------------------------------------

TEST_F(InputManagerDevices, AClaimedKeyIsHiddenFromEveryActionAndNotFromTheLayerThatClaimedIt) {
    // L is pressed. A layer claims it: the action bound to L reads as neither
    // pressed nor down, and no device is holding it -- while the raw key query,
    // which is how the layer reads it, still says pressed and down.
    // MUTATIONS: not asking IsKeyClaimed in IsActionPressed -- red on the first
    // expectation; not asking it in ActionDownDevices -- red on the second and
    // third; claiming the raw query too -- red on the last two.
    InputManager::BindAction(Action::Claimable, KeyCode::L);
    m_backend.keysPressed = {KeyCode::L};
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    InputManager::ClaimKey(KeyCode::L);

    EXPECT_FALSE(InputManager::IsActionPressed(Action::Claimable));
    EXPECT_FALSE(InputManager::IsActionDown(Action::Claimable));
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Claimable), InputDevice::None);
    EXPECT_TRUE(InputManager::IsKeyPressed(KeyCode::L));
    EXPECT_TRUE(InputManager::IsKeyDown(KeyCode::L));
    m_backend.Clear();
}

TEST_F(InputManagerDevices, AClaimIsTheKeysAloneAndTheSameActionOnAnotherDeviceStillWorks) {
    // The action is on L and on a pad button; L is claimed and the pad button is
    // pressed. The pad still presses the action and the pad is the only device
    // holding it. Only the key was claimed.
    // MUTATION: a claim that hid the whole action would read not-pressed; red.
    InputManager::BindAction(Action::Claimable, GamepadButtonCode::LeftFaceDown);
    m_backend.keysPressed = {KeyCode::L};
    m_backend.padPressed = {GamepadButtonCode::LeftFaceDown};
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    InputManager::ClaimKey(KeyCode::L);

    EXPECT_TRUE(InputManager::IsActionPressed(Action::Claimable));
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Claimable), InputDevice::Gamepad);
    m_backend.Clear();
}

TEST_F(InputManagerDevices, AClaimLastsUntilTheNextFixedTick) {
    // Claimed on one tick, L is still held on the next; nobody claims it again,
    // and the action is down once more.
    // MUTATION: not clearing the claims in FlushFixed leaves L claimed for ever;
    // red.
    InputManager::BindAction(Action::Claimable, KeyCode::L);
    m_backend.keysPressed = {KeyCode::L};
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    InputManager::ClaimKey(KeyCode::L);
    ASSERT_FALSE(InputManager::IsActionDown(Action::Claimable));

    Tick(); // the next fixed tick; L is still down
    EXPECT_FALSE(InputManager::IsKeyClaimed(KeyCode::L));
    EXPECT_TRUE(InputManager::IsActionDown(Action::Claimable));
}

TEST_F(InputManagerDevices, AClaimedGamepadButtonIsHiddenFromEveryActionAndNotFromTheLayerThatClaimedIt) {
    // The pad's lower face button is pressed and a layer claims it: the action
    // bound to it reads as neither pressed nor down and no device holds it,
    // while the raw button queries, which the layer reads, still say pressed and
    // down. A layer drawn over the application can take a pad button exactly as
    // it can take a key.
    // MUTATIONS: not asking IsGamepadButtonClaimed in IsActionPressed -- red on
    // the first expectation; not asking it in ActionDownDevices -- red on the
    // second and third; a claim that records nothing -- red on the first three.
    InputManager::BindAction(Action::Claimable, GamepadButtonCode::RightFaceDown);
    m_backend.padPressed = {GamepadButtonCode::RightFaceDown};
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    InputManager::ClaimGamepadButton(GamepadButtonCode::RightFaceDown);

    EXPECT_FALSE(InputManager::IsActionPressed(Action::Claimable));
    EXPECT_FALSE(InputManager::IsActionDown(Action::Claimable));
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Claimable), InputDevice::None);
    EXPECT_TRUE(InputManager::IsGamepadButtonPressed(GamepadButtonCode::RightFaceDown));
    EXPECT_TRUE(InputManager::IsGamepadButtonDown(GamepadButtonCode::RightFaceDown));
    m_backend.Clear();
}

TEST_F(InputManagerDevices, AClaimedMouseButtonIsHiddenFromEveryActionAndNotFromTheLayerThatClaimedIt) {
    // The same for the mouse: the right button is pressed and claimed.
    // MUTATIONS: not asking IsMouseButtonClaimed in IsActionPressed -- red on the
    // first expectation; not asking it in ActionDownDevices -- red on the second
    // and third.
    InputManager::BindAction(Action::Claimable, MouseButtonCode::Right);
    m_backend.mousePressed = {MouseButtonCode::Right};
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    InputManager::ClaimMouseButton(MouseButtonCode::Right);

    EXPECT_FALSE(InputManager::IsActionPressed(Action::Claimable));
    EXPECT_FALSE(InputManager::IsActionDown(Action::Claimable));
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Claimable), InputDevice::None);
    EXPECT_TRUE(InputManager::IsMouseButtonPressed(MouseButtonCode::Right));
    EXPECT_TRUE(InputManager::IsMouseButtonDown(MouseButtonCode::Right));
    m_backend.Clear();
}

TEST_F(InputManagerDevices, AClaimedButtonIsThatButtonAloneAndTheActionsOtherButtonStillWorks) {
    // The action is on two pad buttons; the lower is claimed and the upper is
    // pressed as well. The upper still presses the action, and the pad is the
    // device holding it: a claim names one input, not a device.
    // MUTATION: a claim that hid every pad binding of the action reads
    // not-pressed; red.
    InputManager::BindAction(Action::Claimable, GamepadButtonCode::RightFaceDown);
    InputManager::BindAction(Action::Claimable, GamepadButtonCode::RightFaceUp);
    m_backend.padPressed = {GamepadButtonCode::RightFaceDown, GamepadButtonCode::RightFaceUp};
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    InputManager::ClaimGamepadButton(GamepadButtonCode::RightFaceDown);

    EXPECT_TRUE(InputManager::IsActionPressed(Action::Claimable));
    EXPECT_EQ(InputManager::ActionDownDevices(Action::Claimable), InputDevice::Gamepad);
    m_backend.Clear();
}

TEST_F(InputManagerDevices, AButtonsClaimLastsUntilTheNextFixedTick) {
    // Claimed on one tick, both buttons are still held on the next; nobody
    // claims them again, and they are free once more.
    // MUTATION: not clearing a device's claims in FlushFixed leaves its button
    // claimed for ever; red on that device's expectation.
    m_backend.padPressed = {GamepadButtonCode::RightFaceDown};
    m_backend.mousePressed = {MouseButtonCode::Right};
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    InputManager::ClaimGamepadButton(GamepadButtonCode::RightFaceDown);
    InputManager::ClaimMouseButton(MouseButtonCode::Right);
    ASSERT_TRUE(InputManager::IsGamepadButtonClaimed(GamepadButtonCode::RightFaceDown));
    ASSERT_TRUE(InputManager::IsMouseButtonClaimed(MouseButtonCode::Right));

    Tick(); // the next fixed tick
    EXPECT_FALSE(InputManager::IsGamepadButtonClaimed(GamepadButtonCode::RightFaceDown));
    EXPECT_FALSE(InputManager::IsMouseButtonClaimed(MouseButtonCode::Right));
}

// Whether a gamepad is available. An axis cannot answer it: an absent pad reads
// rest, and so does an attached one nobody is touching.

TEST_F(InputManagerDevices, AGamepadIsAvailableFromTheTickAfterTheBackendSaysSoUntilItSaysOtherwise) {
    // The backend reports a pad, then stops reporting it. InputManager says what
    // the last frame polled before the tick said -- as it does for an axis -- and
    // no more: before anything is polled, and after the pad goes, false.
    // MUTATIONS: not applying the event in FlushFixed -- it stays false; red. Not
    // queueing it in PollVariable -- the same; red. Latching true once seen -- the
    // pad never goes away; red.
    EXPECT_FALSE(InputManager::IsGamepadAvailable()) << "nothing has been polled";
    m_backend.padAvailable = true;
    InputManager::PollVariable(m_dispatcher);
    EXPECT_FALSE(InputManager::IsGamepadAvailable()) << "a frame's poll takes effect at the tick, not before";
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_TRUE(InputManager::IsGamepadAvailable());
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_TRUE(InputManager::IsGamepadAvailable()) << "a tick with no frame polled keeps what the last one said";

    m_backend.padAvailable = false;
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FALSE(InputManager::IsGamepadAvailable());
}

TEST_F(InputManagerDevices, AGamepadsNameComesFromTheSameFrameAsItsAvailability) {
    // A pad is plugged in, a different one replaces it, then it goes while the
    // backend still holds a stale name. The name a tick reads is the last polled
    // frame's, and only while that frame had a pad.
    // MUTATIONS: not applying the name in FlushFixed -- empty after the first
    // tick; red. Reading the backend's name whatever its availability -- the stale
    // name survives the pad; red. Reading it straight from the backend instead of
    // the tick's event -- the new name before the tick; red.
    EXPECT_TRUE(InputManager::GetGamepadName().empty()) << "nothing has been polled";
    m_backend.padAvailable = true;
    m_backend.padName = "Pad One";
    InputManager::PollVariable(m_dispatcher);
    EXPECT_TRUE(InputManager::GetGamepadName().empty()) << "a frame's poll takes effect at the tick, not before";
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_EQ(InputManager::GetGamepadName(), "Pad One");

    m_backend.padName = "Pad Two";
    InputManager::PollVariable(m_dispatcher);
    EXPECT_EQ(InputManager::GetGamepadName(), "Pad One") << "the tick has not run yet";
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_EQ(InputManager::GetGamepadName(), "Pad Two");

    m_backend.padAvailable = false; // the backend still says "Pad Two"
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_TRUE(InputManager::GetGamepadName().empty()) << "no pad, no name";
}

TEST_F(InputManagerDevices, AGamepadsNameReadBeforeATickKeepsItsValueAfterIt) {
    // A caller holds the name it read across the next tick, which replaces the
    // name the manager keeps. `const auto&` binds what the getter returns: a
    // copy, kept alive by the binding, or -- if the getter handed out the
    // manager's own string -- that string, which the tick then rewrites.
    // MUTATION: GetGamepadName returning `const std::string&` again -- the held
    // name reads "Pad Two" after the tick; red.
    m_backend.padAvailable = true;
    m_backend.padName = "Pad One";
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    const auto& held = InputManager::GetGamepadName();
    ASSERT_EQ(held, "Pad One");

    m_backend.padName = "Pad Two";
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_EQ(InputManager::GetGamepadName(), "Pad Two");
    EXPECT_EQ(held, "Pad One") << "the name a caller holds is its own, not the manager's";
}

TEST_F(InputManagerDevices, TheLastFramePolledBeforeATickDecidesWhetherAGamepadIsAvailable) {
    // Two frames drawn in one tick: the pad was there for the first and gone by
    // the second. The tick reads the second.
    // MUTATION: applying the first queued event and dropping the rest -- true; red.
    m_backend.padAvailable = true;
    InputManager::PollVariable(m_dispatcher);
    m_backend.padAvailable = false;
    InputManager::PollVariable(m_dispatcher);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FALSE(InputManager::IsGamepadAvailable());
}

TEST(GamepadAvailability, ABackendThatDoesNotSayReportsNoGamepad) {
    // IsGamepadAvailable is not pure, so a backend written before it -- or one
    // with no gamepad support -- still builds, and reports what it has: none.
    // MUTATION: a default of true -- red.
    SilentInputBackend silent;
    EXPECT_FALSE(silent.IsGamepadAvailable());
    // GetGamepadName is not pure either, and names no gamepad by default.
    // MUTATION: a default returning any text -- red.
    EXPECT_TRUE(silent.GetGamepadName().empty());
}

// The mouse wheel: each frame's movement polled with the other inputs and summed
// by the fixed tick, and a claim the layers that read it pass between them.
// Inputs arrive as in a running application: the backend reports a frame's
// movement, PollVariable queues it, FlushFixed applies it.
namespace {
class MouseWheel : public InputManagerDevices {
  protected:
    // One render frame whose wheel moved (x, y).
    void Frame(float x, float y) {
        m_backend.wheelX = x;
        m_backend.wheelY = y;
        InputManager::PollVariable(m_dispatcher);
        m_backend.Clear();
    }
};
} // namespace

TEST_F(MouseWheel, TheMovementOfEveryFramePolledBeforeATickIsSummed) {
    // Three frames drawn before one tick, the wheel turned 1, then 2, then half
    // a notch: the tick reads 1 + 2 + 0.5 = 3.5 notches -- none of them lost.
    // MUTATION: the tick keeping the last frame's movement (= for +=) -- 0.5, red;
    // keeping only the first frame's -- 1, red.
    Frame(0.0f, 1.0f);
    Frame(0.0f, 2.0f);
    Frame(0.0f, 0.5f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), 3.5f);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveX(), 0.0f);
}

TEST_F(MouseWheel, ASecondTickInTheSameFrameReadsNoMovement) {
    // One frame turned the wheel 3 notches, and that frame ran two ticks. The
    // first reads 3; the second follows no new frame and reads 0 -- the turn is
    // not applied twice.
    // MUTATION: FlushFixed not clearing the sum -- the second tick reads 3, red.
    Frame(0.0f, 3.0f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), 3.0f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), 0.0f);
}

TEST_F(MouseWheel, AFrameWithNoMovementReadsNone) {
    // A tick after a frame that turned it 2 notches reads 2; the tick after the
    // next frame, which did not turn it, reads 0, not the 2 of before.
    // MUTATION: FlushFixed not clearing the sum -- 2, red.
    Frame(-2.0f, 2.0f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveX(), -2.0f);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), 2.0f);
    Frame(0.0f, 0.0f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveX(), 0.0f);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), 0.0f);
}

TEST_F(MouseWheel, TheTwoAxesAreIndependent) {
    // A frame moving only x reads on x alone, one moving only y on y alone, and
    // one moving both reads each on its own axis: 1.5 on x then 0; 0 then -2 on
    // y; and (0.25, -0.75) as (0.25, -0.75).
    // MUTATION: queuing a frame only when BOTH axes moved (&& for ||) -- the
    // first two ticks read 0, red; a frame's y added into x -- the third reads
    // x = 0.25 - 0.75 = -0.5, red.
    Frame(1.5f, 0.0f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveX(), 1.5f);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), 0.0f);
    Frame(0.0f, -2.0f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveX(), 0.0f);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), -2.0f);
    Frame(0.25f, -0.75f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveX(), 0.25f);
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), -0.75f);
}

TEST_F(MouseWheel, AClaimIsSeenByASecondReaderAndTheRawReadsStillReportTheMovement) {
    // Two layers read the wheel in one tick, the upper first. Nothing has
    // claimed it when the upper reads, so it scrolls and claims; the lower then
    // sees the claim and leaves the turn alone. The movement itself is still
    // there to read, which is how the upper reads it after claiming.
    // MUTATION: ClaimMouseWheel doing nothing -- the second reader sees no claim,
    // red; a claim zeroing the movement -- the raw read is 0, red.
    Frame(0.0f, 1.0f);
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FALSE(InputManager::IsMouseWheelClaimed()) << "nothing has claimed it yet";
    InputManager::ClaimMouseWheel(); // the upper layer scrolled on it
    EXPECT_TRUE(InputManager::IsMouseWheelClaimed()) << "the lower layer must see the claim";
    EXPECT_FLOAT_EQ(InputManager::GetMouseWheelMoveY(), 1.0f) << "the raw read still reports the turn";
}

TEST_F(MouseWheel, AClaimLastsUntilTheNextFixedTick) {
    // MUTATION: FlushFixed not clearing the claim -- still claimed, red.
    InputManager::ClaimMouseWheel();
    InputManager::FlushFixed(m_dispatcher);
    EXPECT_FALSE(InputManager::IsMouseWheelClaimed());
}

TEST(MouseWheelDefault, ABackendWithoutAWheelReportsNone) {
    // The wheel's reads are not pure, so a backend written before them -- or one
    // for a platform with no wheel -- still builds, and reports what it has: no
    // movement, on either axis, to the manager too.
    // MUTATION: a default of 1.0 -- red.
    SilentInputBackend silent;
    EXPECT_FLOAT_EQ(silent.GetMouseWheelMoveX(), 0.0f);
    EXPECT_FLOAT_EQ(silent.GetMouseWheelMoveY(), 0.0f);
    entt::dispatcher dispatcher;
    InputManager::Initialize(&silent);
    InputManager::PollVariable(dispatcher);
    InputManager::FlushFixed(dispatcher);
    const float x = InputManager::GetMouseWheelMoveX();
    const float y = InputManager::GetMouseWheelMoveY();
    InputManager::Initialize(nullptr);
    EXPECT_FLOAT_EQ(x, 0.0f);
    EXPECT_FLOAT_EQ(y, 0.0f);
}
