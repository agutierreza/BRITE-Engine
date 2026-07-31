#include "InputManager.hpp"
#include <raylib.h>

namespace BRITE {

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

void InputManager::Update() {
    // If we need to capture state for fixed timestep, we would
    // accumulate events here. For now, Raylib's state is queried directly.
}

bool InputManager::IsKeyPressed(KeyCode key) {
    return ::IsKeyPressed(MapKey(key));
}

bool InputManager::IsKeyDown(KeyCode key) {
    return ::IsKeyDown(MapKey(key));
}

bool InputManager::IsKeyReleased(KeyCode key) {
    return ::IsKeyReleased(MapKey(key));
}

bool InputManager::IsMouseButtonPressed(MouseButtonCode button) {
    return ::IsMouseButtonPressed(MapMouseButton(button));
}

bool InputManager::IsMouseButtonDown(MouseButtonCode button) {
    return ::IsMouseButtonDown(MapMouseButton(button));
}

bool InputManager::IsMouseButtonReleased(MouseButtonCode button) {
    return ::IsMouseButtonReleased(MapMouseButton(button));
}

float InputManager::GetMouseX() {
    return (float)::GetMouseX();
}

float InputManager::GetMouseY() {
    return (float)::GetMouseY();
}

} // namespace BRITE
