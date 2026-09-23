#pragma once

#include "InputEvents.hpp"
#include <array>
#include <cstdint>
#include <entt/entt.hpp>
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

    // Called once per render frame
    static void PollVariable(entt::dispatcher& dispatcher);

    // Called once per fixed tick
    static void FlushFixed(entt::dispatcher& dispatcher);

    static bool IsKeyPressed(KeyCode key);
    static bool IsKeyDown(KeyCode key);
    static bool IsKeyReleased(KeyCode key);

    static bool IsMouseButtonPressed(MouseButtonCode button);
    static bool IsMouseButtonDown(MouseButtonCode button);
    static bool IsMouseButtonReleased(MouseButtonCode button);

    static bool IsGamepadButtonPressed(GamepadButtonCode button);
    static bool IsGamepadButtonDown(GamepadButtonCode button);
    static bool IsGamepadButtonReleased(GamepadButtonCode button);
    static float GetGamepadAxis(GamepadAxisCode axis);

    static float GetMouseX();
    static float GetMouseY();

    static float GetMouseDeltaX();
    static float GetMouseDeltaY();

    // Claiming a key.
    //
    // A layer drawn over the rest of the application -- a menu, a text field, a
    // console -- reads keys that the application also has actions bound to, and
    // one press must not do both. ClaimKey takes `key` away from every ACTION for
    // the rest of this fixed tick: IsActionPressed, IsActionDown and
    // ActionDownDevices behave as if it were up. The raw key queries --
    // IsKeyPressed, IsKeyDown, IsKeyReleased -- still see it, which is how the
    // layer that claimed it reads it. Gamepad and mouse bindings of the same
    // action are untouched: only the key is claimed.
    //
    // A claim lasts until the next FlushFixed. A layer that owns keys for as long
    // as it is open claims them on every tick it is open, BEFORE anything reads
    // the actions bound to them that tick.
    static void ClaimKey(KeyCode key);
    static bool IsKeyClaimed(KeyCode key);

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
            if (IsGamepadButtonDown(b)) {
                devices |= InputDevice::Gamepad;
                break;
            }
        }
        for (MouseButtonCode m : s_actionMouseBindings[actionId]) {
            if (IsMouseButtonDown(m)) {
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
            if (IsGamepadButtonPressed(b))
                return true;
        }
        for (MouseButtonCode m : s_actionMouseBindings[actionId]) {
            if (IsMouseButtonPressed(m))
                return true;
        }
        return false;
    }

  private:
    static void OnKeyDown(const KeyDownEvent& event);
    static void OnKeyUp(const KeyUpEvent& event);
    static void OnMouseDown(const MouseButtonDownEvent& event);
    static void OnMouseUp(const MouseButtonUpEvent& event);
    static void OnMouseMove(const MouseMoveEvent& event);

    static void OnGamepadButtonDown(const GamepadButtonDownEvent& event);
    static void OnGamepadButtonUp(const GamepadButtonUpEvent& event);
    static void OnGamepadAxisMove(const GamepadAxisEvent& event);

    static constexpr size_t KeyCount = static_cast<size_t>(KeyCode::Count);
    static std::array<bool, KeyCount> s_keysDown;
    static std::array<bool, KeyCount> s_keysPressedThisTick;
    static std::array<bool, KeyCount> s_keysReleasedThisTick;
    static std::array<bool, KeyCount> s_keysClaimedThisTick; // see ClaimKey

    static std::unordered_map<MouseButtonCode, bool> s_buttonsDown;
    static std::unordered_map<MouseButtonCode, bool> s_buttonsPressedThisTick;
    static std::unordered_map<MouseButtonCode, bool> s_buttonsReleasedThisTick;

    static std::unordered_map<GamepadButtonCode, bool> s_gamepadButtonsDown;
    static std::unordered_map<GamepadButtonCode, bool> s_gamepadButtonsPressedThisTick;
    static std::unordered_map<GamepadButtonCode, bool> s_gamepadButtonsReleasedThisTick;
    static std::unordered_map<GamepadAxisCode, float> s_gamepadAxes;

    // Action Binding Maps
    static std::unordered_map<uint32_t, std::vector<KeyCode>> s_actionKeyBindings;
    static std::unordered_map<uint32_t, std::vector<GamepadButtonCode>> s_actionGamepadBindings;
    static std::unordered_map<uint32_t, std::vector<MouseButtonCode>> s_actionMouseBindings;

    static float s_mouseX;
    static float s_mouseY;
    static float s_mouseDeltaX;
    static float s_mouseDeltaY;

    static Backends::IInputBackend* s_backend;
};

} // namespace BRITE
