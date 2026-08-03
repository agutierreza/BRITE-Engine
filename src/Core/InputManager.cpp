#include "InputManager.hpp"
#include <raylib.h>

namespace BRITE {

// Initialize statics
std::unordered_map<KeyCode, bool> InputManager::s_keysDown;
std::unordered_map<KeyCode, bool> InputManager::s_keysPressedThisTick;
std::unordered_map<KeyCode, bool> InputManager::s_keysReleasedThisTick;

std::unordered_map<MouseButtonCode, bool> InputManager::s_buttonsDown;
std::unordered_map<MouseButtonCode, bool> InputManager::s_buttonsPressedThisTick;
std::unordered_map<MouseButtonCode, bool> InputManager::s_buttonsReleasedThisTick;

float InputManager::s_mouseX = 0.0f;
float InputManager::s_mouseY = 0.0f;
float InputManager::s_mouseDeltaX = 0.0f;
float InputManager::s_mouseDeltaY = 0.0f;

static int MapKey(KeyCode key) {
    switch (key) {
    case KeyCode::Space:
        return KEY_SPACE;
    case KeyCode::Escape:
        return KEY_ESCAPE;
    case KeyCode::Enter:
        return KEY_ENTER;
    case KeyCode::Up:
        return KEY_UP;
    case KeyCode::Down:
        return KEY_DOWN;
    case KeyCode::Left:
        return KEY_LEFT;
    case KeyCode::Right:
        return KEY_RIGHT;
    case KeyCode::W:
        return KEY_W;
    case KeyCode::A:
        return KEY_A;
    case KeyCode::S:
        return KEY_S;
    case KeyCode::D:
        return KEY_D;
    default:
        return 0;
    }
}

static int MapMouseButton(MouseButtonCode button) {
    switch (button) {
    case MouseButtonCode::Left:
        return MOUSE_BUTTON_LEFT;
    case MouseButtonCode::Right:
        return MOUSE_BUTTON_RIGHT;
    case MouseButtonCode::Middle:
        return MOUSE_BUTTON_MIDDLE;
    default:
        return 0;
    }
}

static const KeyCode AllKeys[] = {KeyCode::Space, KeyCode::Escape, KeyCode::Enter, KeyCode::Up,
                                  KeyCode::Down,  KeyCode::Left,   KeyCode::Right, KeyCode::W,
                                  KeyCode::A,     KeyCode::S,      KeyCode::D};

static const MouseButtonCode AllMouseButtons[] = {MouseButtonCode::Left, MouseButtonCode::Right,
                                                  MouseButtonCode::Middle};

void InputManager::PollVariable(entt::dispatcher& dispatcher) {
    for (KeyCode key : AllKeys) {
        int rlKey = MapKey(key);
        if (::IsKeyPressed(rlKey))
            dispatcher.enqueue<KeyDownEvent>(KeyDownEvent{key});
        if (::IsKeyReleased(rlKey))
            dispatcher.enqueue<KeyUpEvent>(KeyUpEvent{key});
    }

    for (MouseButtonCode btn : AllMouseButtons) {
        int rlBtn = MapMouseButton(btn);
        if (::IsMouseButtonPressed(rlBtn))
            dispatcher.enqueue<MouseButtonDownEvent>(MouseButtonDownEvent{btn});
        if (::IsMouseButtonReleased(rlBtn))
            dispatcher.enqueue<MouseButtonUpEvent>(MouseButtonUpEvent{btn});
    }

    Vector2 delta = ::GetMouseDelta();
    if (delta.x != 0.0f || delta.y != 0.0f) {
        dispatcher.enqueue<MouseMoveEvent>(
            MouseMoveEvent{delta.x, delta.y, (float)::GetMouseX(), (float)::GetMouseY()});
    }
}

void InputManager::FlushFixed(entt::dispatcher& dispatcher) {
    s_keysPressedThisTick.clear();
    s_keysReleasedThisTick.clear();
    s_buttonsPressedThisTick.clear();
    s_buttonsReleasedThisTick.clear();
    s_mouseDeltaX = 0.0f;
    s_mouseDeltaY = 0.0f;

    dispatcher.sink<KeyDownEvent>().connect<&InputManager::OnKeyDown>();
    dispatcher.sink<KeyUpEvent>().connect<&InputManager::OnKeyUp>();
    dispatcher.sink<MouseButtonDownEvent>().connect<&InputManager::OnMouseDown>();
    dispatcher.sink<MouseButtonUpEvent>().connect<&InputManager::OnMouseUp>();
    dispatcher.sink<MouseMoveEvent>().connect<&InputManager::OnMouseMove>();

    dispatcher.update<KeyDownEvent>();
    dispatcher.update<KeyUpEvent>();
    dispatcher.update<MouseButtonDownEvent>();
    dispatcher.update<MouseButtonUpEvent>();
    dispatcher.update<MouseMoveEvent>();

    dispatcher.sink<KeyDownEvent>().disconnect<&InputManager::OnKeyDown>();
    dispatcher.sink<KeyUpEvent>().disconnect<&InputManager::OnKeyUp>();
    dispatcher.sink<MouseButtonDownEvent>().disconnect<&InputManager::OnMouseDown>();
    dispatcher.sink<MouseButtonUpEvent>().disconnect<&InputManager::OnMouseUp>();
    dispatcher.sink<MouseMoveEvent>().disconnect<&InputManager::OnMouseMove>();
}

void InputManager::OnKeyDown(const KeyDownEvent& event) {
    if (!s_keysDown[event.key]) {
        s_keysPressedThisTick[event.key] = true;
    }
    s_keysDown[event.key] = true;
}

void InputManager::OnKeyUp(const KeyUpEvent& event) {
    s_keysReleasedThisTick[event.key] = true;
    s_keysDown[event.key] = false;
}

void InputManager::OnMouseDown(const MouseButtonDownEvent& event) {
    if (!s_buttonsDown[event.button]) {
        s_buttonsPressedThisTick[event.button] = true;
    }
    s_buttonsDown[event.button] = true;
}

void InputManager::OnMouseUp(const MouseButtonUpEvent& event) {
    s_buttonsReleasedThisTick[event.button] = true;
    s_buttonsDown[event.button] = false;
}

void InputManager::OnMouseMove(const MouseMoveEvent& event) {
    s_mouseDeltaX += event.deltaX;
    s_mouseDeltaY += event.deltaY;
    s_mouseX = event.absX;
    s_mouseY = event.absY;
}

bool InputManager::IsKeyPressed(KeyCode key) {
    return s_keysPressedThisTick[key];
}
bool InputManager::IsKeyDown(KeyCode key) {
    return s_keysDown[key];
}
bool InputManager::IsKeyReleased(KeyCode key) {
    return s_keysReleasedThisTick[key];
}
bool InputManager::IsMouseButtonPressed(MouseButtonCode button) {
    return s_buttonsPressedThisTick[button];
}
bool InputManager::IsMouseButtonDown(MouseButtonCode button) {
    return s_buttonsDown[button];
}
bool InputManager::IsMouseButtonReleased(MouseButtonCode button) {
    return s_buttonsReleasedThisTick[button];
}
float InputManager::GetMouseX() {
    return s_mouseX;
}
float InputManager::GetMouseY() {
    return s_mouseY;
}
float InputManager::GetMouseDeltaX() {
    return s_mouseDeltaX;
}
float InputManager::GetMouseDeltaY() {
    return s_mouseDeltaY;
}

} // namespace BRITE
