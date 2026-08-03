#pragma once

#include "InputEvents.hpp"
#include <entt/entt.hpp>
#include <unordered_map>

namespace BRITE {

class InputManager {
  public:
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

    static float GetMouseX();
    static float GetMouseY();

    static float GetMouseDeltaX();
    static float GetMouseDeltaY();

  private:
    static void OnKeyDown(const KeyDownEvent& event);
    static void OnKeyUp(const KeyUpEvent& event);
    static void OnMouseDown(const MouseButtonDownEvent& event);
    static void OnMouseUp(const MouseButtonUpEvent& event);
    static void OnMouseMove(const MouseMoveEvent& event);

    static std::unordered_map<KeyCode, bool> s_keysDown;
    static std::unordered_map<KeyCode, bool> s_keysPressedThisTick;
    static std::unordered_map<KeyCode, bool> s_keysReleasedThisTick;

    static std::unordered_map<MouseButtonCode, bool> s_buttonsDown;
    static std::unordered_map<MouseButtonCode, bool> s_buttonsPressedThisTick;
    static std::unordered_map<MouseButtonCode, bool> s_buttonsReleasedThisTick;

    static float s_mouseX;
    static float s_mouseY;
    static float s_mouseDeltaX;
    static float s_mouseDeltaY;
};

} // namespace BRITE
