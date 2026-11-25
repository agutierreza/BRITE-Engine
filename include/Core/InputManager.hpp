#pragma once

#include "InputEvents.hpp"
#include <array>
#include <cstdint>
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "Backends/IInputBackend.hpp"

namespace BRITE {

// Which kinds of device an input came from. Flags, because an action bound to
// several devices can be held on more than one of them at once.
enum class InputDevice : uint8_t {
    None = 0,
    Keyboard = 1 << 0,
    Gamepad = 1 << 1,
    Mouse = 1 << 2,
};

constexpr InputDevice operator|(InputDevice a, InputDevice b) {
    return static_cast<InputDevice>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr InputDevice operator&(InputDevice a, InputDevice b) {
    return static_cast<InputDevice>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr InputDevice& operator|=(InputDevice& a, InputDevice b) {
    a = a | b;
    return a;
}
// True when `devices` shares any flag with `device`: for a single flag, whether
// that device is among them.
constexpr bool Includes(InputDevice devices, InputDevice device) {
    return (devices & device) != InputDevice::None;
}

class InputManager {
  public:
    static void Initialize(Backends::IInputBackend* backend);
    // The backend the manager polls, or null when it has none. Process-wide:
    // whoever set it must clear it before the backend is destroyed.
    static Backends::IInputBackend* Backend();

    // Called once per render frame
    static void PollVariable(entt::dispatcher& dispatcher);

    // Called once per fixed tick
    static void FlushFixed(entt::dispatcher& dispatcher);

    static bool IsKeyPressed(KeyCode key);
    static bool IsKeyDown(KeyCode key);
    static bool IsKeyReleased(KeyCode key);

    // A held key repeating as the operating system repeats it -- see
    // IInputBackend::IsKeyRepeated: after the platform's delay, at its rate, and
    // never for the press that began the hold, which IsKeyPressed reports.
    //
    // GetKeyRepeatCount is the number of frames polled since the previous fixed
    // tick in which the backend reported `key` repeated. It is a COUNT over those
    // frames, so a tick that follows several frames loses none of their repeats;
    // and it is zero for a tick that follows no new frame, so when one frame runs
    // several ticks only the first of them reads that frame's repeat. Why: a
    // fixed tick reads input as what happened since the last tick, so a value
    // nudged once a repeat moves as far for one second held whatever the frame
    // rate and the tick rate are, and no repeat is applied twice -- the mouse
    // wheel is counted the same way (GetMouseWheelMoveY). A backend reports a
    // repeat at most once a frame, so the count is exact while frames come
    // faster than the platform repeats.
    //
    // IsKeyRepeated is whether the count is above zero. IsKeyPressedOrRepeated
    // is the one call for a reader that acts on the press and on every repeat
    // after it, as a held Backspace erases: true on the tick the key went down
    // and on every tick it repeated. A reader that acts once per press and once
    // per repeat acts (IsKeyPressed(key) ? 1 : 0) + GetKeyRepeatCount(key) times.
    //
    // Only keys repeat: the operating system repeats nothing else. A gamepad
    // button, a mouse button or an axis held past a threshold is timed with
    // HeldRepeat (Core/HeldRepeat.hpp).
    static int GetKeyRepeatCount(KeyCode key);
    static bool IsKeyRepeated(KeyCode key);
    static bool IsKeyPressedOrRepeated(KeyCode key);

    static bool IsMouseButtonPressed(MouseButtonCode button);
    static bool IsMouseButtonDown(MouseButtonCode button);
    static bool IsMouseButtonReleased(MouseButtonCode button);

    static bool IsGamepadButtonPressed(GamepadButtonCode button);
    static bool IsGamepadButtonDown(GamepadButtonCode button);
    static bool IsGamepadButtonReleased(GamepadButtonCode button);
    static float GetGamepadAxis(GamepadAxisCode axis);
    // Whether a gamepad was available at the last frame polled before this tick.
    // False until one has been polled. Read it before trusting an axis: an absent
    // pad's axes read rest, and a pad's first reading after it is attached need
    // not be its rest -- so a reader that waits for rest before acting on an axis
    // has to start waiting again whenever the pad goes away.
    static bool IsGamepadAvailable();
    // The available gamepad's name as its backend reports it, from the same
    // frame as IsGamepadAvailable. Empty when none is available, and when the
    // backend names no gamepad. A copy, like every getter here: the next tick
    // replaces the name it was read from.
    static std::string GetGamepadName();

    static float GetMouseX();
    static float GetMouseY();

    static float GetMouseDeltaX();
    static float GetMouseDeltaY();

    // The mouse wheel's movement since the previous fixed tick, in the backend's
    // units and signs: notches, positive y turned away from the user (scrolling
    // up), positive x to the left -- see IInputBackend::GetMouseWheelMoveY. Each
    // axis on its own.
    //
    // It is the SUM over every frame polled since the previous tick, so a tick
    // that follows several frames loses none of their movement; and it is zero
    // for a tick that follows no new frame, so when one frame runs several ticks
    // only the first of them reads that frame's movement. Why: a fixed tick reads
    // input as what happened since the last tick, so a list scrolls as far for
    // one turn of the wheel whatever the frame rate and the tick rate are, and a
    // frame's turn is never applied twice. GetMouseDeltaX and GetMouseDeltaY are
    // counted the same way.
    static float GetMouseWheelMoveX();
    static float GetMouseWheelMoveY();

    // Claiming an input.
    //
    // A layer drawn over the rest of the application -- a menu, a text field, a
    // console -- reads keys, gamepad buttons and mouse buttons that the
    // application also has actions bound to, and one press must not do both.
    // ClaimKey takes `key` away from every ACTION for the rest of this fixed
    // tick: IsActionPressed, IsActionDown, ActionDownDevices, IsActionRepeated
    // and IsActionPressedOrRepeated behave as if it were up. The raw queries --
    // IsKeyPressed, IsKeyDown, IsKeyReleased, and the repeat's GetKeyRepeatCount,
    // IsKeyRepeated and IsKeyPressedOrRepeated -- still see it, which is how the
    // layer that claimed it reads it. A layer that takes a key while it is held
    // claims it on every tick, its repeats with it.
    // ClaimGamepadButton and ClaimMouseButton do the same for a gamepad button
    // and a mouse button, with IsGamepadButton* and IsMouseButton* as their raw
    // queries.
    //
    // A claim takes the one input it names and nothing else: the same action's
    // bindings on other inputs, of the same device or another, still work.
    //
    // A claim lasts until the next FlushFixed. A layer that owns inputs for as
    // long as it is open claims them on every tick it is open, BEFORE anything
    // reads the actions bound to them that tick.
    //
    // ClaimMouseWheel claims the wheel, both of its axes, for the rest of this
    // fixed tick. No action is bound to the wheel, so there is nothing for the
    // claim to hide it from: it is a word between the layers that read it. A
    // layer that scrolls on the wheel reads it only while IsMouseWheelClaimed()
    // is false, and claims it when it scrolls, so a layer under it that reads
    // later in the same tick leaves the turn alone. GetMouseWheelMoveX and
    // GetMouseWheelMoveY still report the movement after a claim -- the raw
    // reads, as IsKeyDown is for a claimed key -- which is how the layer that
    // claimed it reads it. It lasts until the next FlushFixed, as every claim.
    static void ClaimKey(KeyCode key);
    static bool IsKeyClaimed(KeyCode key);
    static void ClaimGamepadButton(GamepadButtonCode button);
    static bool IsGamepadButtonClaimed(GamepadButtonCode button);
    static void ClaimMouseButton(MouseButtonCode button);
    static bool IsMouseButtonClaimed(MouseButtonCode button);
    static void ClaimMouseWheel();
    static bool IsMouseWheelClaimed();

    // Action Mapping
    template <typename TEnum> static void BindAction(TEnum action, KeyCode key) {
        s_actionKeyBindings[static_cast<uint32_t>(action)].push_back(key);
    }

    template <typename TEnum> static void BindAction(TEnum action, GamepadButtonCode button) {
        s_actionGamepadBindings[static_cast<uint32_t>(action)].push_back(button);
    }

    template <typename TEnum> static void BindAction(TEnum action, MouseButtonCode button) {
        s_actionMouseBindings[static_cast<uint32_t>(action)].push_back(button);
    }

    // Which devices are holding this action down: each device with at least one
    // of the action's own bindings down. None while the action is up. An input
    // that is down but not bound to the action does not count.
    template <typename TEnum> static InputDevice ActionDownDevices(TEnum action) {
        uint32_t actionId = static_cast<uint32_t>(action);
        InputDevice devices = InputDevice::None;
        for (KeyCode k : s_actionKeyBindings[actionId]) {
            if (IsKeyDown(k) && !IsKeyClaimed(k)) {
                devices |= InputDevice::Keyboard;
                break;
            }
        }
        for (GamepadButtonCode b : s_actionGamepadBindings[actionId]) {
            if (IsGamepadButtonDown(b) && !IsGamepadButtonClaimed(b)) {
                devices |= InputDevice::Gamepad;
                break;
            }
        }
        for (MouseButtonCode m : s_actionMouseBindings[actionId]) {
            if (IsMouseButtonDown(m) && !IsMouseButtonClaimed(m)) {
                devices |= InputDevice::Mouse;
                break;
            }
        }
        return devices;
    }

    // One implementation of "down", so the two queries cannot disagree.
    template <typename TEnum> static bool IsActionDown(TEnum action) {
        return ActionDownDevices(action) != InputDevice::None;
    }

    template <typename TEnum> static bool IsActionPressed(TEnum action) {
        uint32_t actionId = static_cast<uint32_t>(action);
        for (KeyCode k : s_actionKeyBindings[actionId]) {
            if (IsKeyPressed(k) && !IsKeyClaimed(k))
                return true;
        }
        for (GamepadButtonCode b : s_actionGamepadBindings[actionId]) {
            if (IsGamepadButtonPressed(b) && !IsGamepadButtonClaimed(b))
                return true;
        }
        for (MouseButtonCode m : s_actionMouseBindings[actionId]) {
            if (IsMouseButtonPressed(m) && !IsMouseButtonClaimed(m))
                return true;
        }
        return false;
    }

    // Whether one of the action's KEYS repeated this tick (IsKeyRepeated) and is
    // not claimed. Only its keys: the operating system repeats keys and nothing
    // else, so an action held on a gamepad or mouse button never reads repeated
    // -- time those holds with HeldRepeat.
    template <typename TEnum> static bool IsActionRepeated(TEnum action) {
        uint32_t actionId = static_cast<uint32_t>(action);
        for (KeyCode k : s_actionKeyBindings[actionId]) {
            if (IsKeyRepeated(k) && !IsKeyClaimed(k))
                return true;
        }
        return false;
    }

    // The one call for a reader that acts on the action's press and on every
    // repeat of one of its keys -- see IsKeyPressedOrRepeated.
    template <typename TEnum> static bool IsActionPressedOrRepeated(TEnum action) {
        return IsActionPressed(action) || IsActionRepeated(action);
    }

  private:
    static void OnKeyDown(const KeyDownEvent& event);
    static void OnKeyUp(const KeyUpEvent& event);
    static void OnKeyRepeat(const KeyRepeatEvent& event);
    static void OnMouseDown(const MouseButtonDownEvent& event);
    static void OnMouseUp(const MouseButtonUpEvent& event);
    static void OnMouseMove(const MouseMoveEvent& event);
    static void OnMouseWheel(const MouseWheelEvent& event);

    static void OnGamepadButtonDown(const GamepadButtonDownEvent& event);
    static void OnGamepadButtonUp(const GamepadButtonUpEvent& event);
    static void OnGamepadAxisMove(const GamepadAxisEvent& event);
    static void OnGamepadAvailability(const GamepadAvailabilityEvent& event);

    static constexpr size_t KeyCount = static_cast<size_t>(KeyCode::Count);
    static std::array<bool, KeyCount> s_keysDown;
    static std::array<bool, KeyCount> s_keysPressedThisTick;
    static std::array<bool, KeyCount> s_keysReleasedThisTick;
    static std::array<bool, KeyCount> s_keysClaimedThisTick; // see ClaimKey
    static std::array<int, KeyCount> s_keyRepeatsThisTick; // counted since the last FlushFixed -- see GetKeyRepeatCount

    static std::unordered_map<MouseButtonCode, bool> s_buttonsDown;
    static std::unordered_map<MouseButtonCode, bool> s_buttonsPressedThisTick;
    static std::unordered_map<MouseButtonCode, bool> s_buttonsReleasedThisTick;

    static std::unordered_map<GamepadButtonCode, bool> s_gamepadButtonsDown;
    static std::unordered_map<GamepadButtonCode, bool> s_gamepadButtonsPressedThisTick;
    static std::unordered_map<GamepadButtonCode, bool> s_gamepadButtonsReleasedThisTick;
    static std::unordered_map<GamepadAxisCode, float> s_gamepadAxes;
    static bool s_gamepadAvailable;
    static std::string s_gamepadName;

    static std::unordered_map<GamepadButtonCode, bool> s_gamepadButtonsClaimedThisTick; // see ClaimGamepadButton
    static std::unordered_map<MouseButtonCode, bool> s_buttonsClaimedThisTick;          // see ClaimMouseButton

    // Action Binding Maps
    static std::unordered_map<uint32_t, std::vector<KeyCode>> s_actionKeyBindings;
    static std::unordered_map<uint32_t, std::vector<GamepadButtonCode>> s_actionGamepadBindings;
    static std::unordered_map<uint32_t, std::vector<MouseButtonCode>> s_actionMouseBindings;

    static float s_mouseX;
    static float s_mouseY;
    static float s_mouseDeltaX;
    static float s_mouseDeltaY;
    static float s_mouseWheelX; // summed since the last FlushFixed -- see GetMouseWheelMoveX
    static float s_mouseWheelY;
    static bool s_mouseWheelClaimedThisTick; // see ClaimMouseWheel

    static Backends::IInputBackend* s_backend;
};

} // namespace BRITE
