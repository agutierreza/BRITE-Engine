#pragma once

#include "InputEvents.hpp"
#include <cstdint>
#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>

#include "Backends/IInputBackend.hpp"

namespace BRITE {

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

    template <typename TEnum> static bool IsActionDown(TEnum action) {
        uint32_t actionId = static_cast<uint32_t>(action);
        for (KeyCode k : s_actionKeyBindings[actionId]) {
            if (IsKeyDown(k))
                return true;
        }
        for (GamepadButtonCode b : s_actionGamepadBindings[actionId]) {
            if (IsGamepadButtonDown(b))
                return true;
        }
        for (MouseButtonCode m : s_actionMouseBindings[actionId]) {
            if (IsMouseButtonDown(m))
                return true;
        }
        return false;
    }

    template <typename TEnum> static bool IsActionPressed(TEnum action) {
        uint32_t actionId = static_cast<uint32_t>(action);
        for (KeyCode k : s_actionKeyBindings[actionId]) {
            if (IsKeyPressed(k))
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

    static std::unordered_map<KeyCode, bool> s_keysDown;
    static std::unordered_map<KeyCode, bool> s_keysPressedThisTick;
    static std::unordered_map<KeyCode, bool> s_keysReleasedThisTick;

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
