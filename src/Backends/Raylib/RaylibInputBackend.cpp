#include "Backends/Raylib/RaylibInputBackend.hpp"
#include <raylib.h>

namespace BRITE {
namespace Backends {
namespace Raylib {

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

static int MapGamepadButton(GamepadButtonCode button) {
    switch (button) {
    case GamepadButtonCode::Unknown:
        return GAMEPAD_BUTTON_UNKNOWN;
    case GamepadButtonCode::LeftFaceUp:
        return GAMEPAD_BUTTON_LEFT_FACE_UP;
    case GamepadButtonCode::LeftFaceRight:
        return GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
    case GamepadButtonCode::LeftFaceDown:
        return GAMEPAD_BUTTON_LEFT_FACE_DOWN;
    case GamepadButtonCode::LeftFaceLeft:
        return GAMEPAD_BUTTON_LEFT_FACE_LEFT;
    case GamepadButtonCode::RightFaceUp:
        return GAMEPAD_BUTTON_RIGHT_FACE_UP;
    case GamepadButtonCode::RightFaceRight:
        return GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
    case GamepadButtonCode::RightFaceDown:
        return GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
    case GamepadButtonCode::RightFaceLeft:
        return GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
    case GamepadButtonCode::LeftTrigger1:
        return GAMEPAD_BUTTON_LEFT_TRIGGER_1;
    case GamepadButtonCode::LeftTrigger2:
        return GAMEPAD_BUTTON_LEFT_TRIGGER_2;
    case GamepadButtonCode::RightTrigger1:
        return GAMEPAD_BUTTON_RIGHT_TRIGGER_1;
    case GamepadButtonCode::RightTrigger2:
        return GAMEPAD_BUTTON_RIGHT_TRIGGER_2;
    case GamepadButtonCode::MiddleLeft:
        return GAMEPAD_BUTTON_MIDDLE_LEFT;
    case GamepadButtonCode::Middle:
        return GAMEPAD_BUTTON_MIDDLE;
    case GamepadButtonCode::MiddleRight:
        return GAMEPAD_BUTTON_MIDDLE_RIGHT;
    case GamepadButtonCode::LeftThumb:
        return GAMEPAD_BUTTON_LEFT_THUMB;
    case GamepadButtonCode::RightThumb:
        return GAMEPAD_BUTTON_RIGHT_THUMB;
    default:
        return GAMEPAD_BUTTON_UNKNOWN;
    }
}

static int MapGamepadAxis(GamepadAxisCode axis) {
    switch (axis) {
    case GamepadAxisCode::LeftX:
        return GAMEPAD_AXIS_LEFT_X;
    case GamepadAxisCode::LeftY:
        return GAMEPAD_AXIS_LEFT_Y;
    case GamepadAxisCode::RightX:
        return GAMEPAD_AXIS_RIGHT_X;
    case GamepadAxisCode::RightY:
        return GAMEPAD_AXIS_RIGHT_Y;
    case GamepadAxisCode::LeftTrigger:
        return GAMEPAD_AXIS_LEFT_TRIGGER;
    case GamepadAxisCode::RightTrigger:
        return GAMEPAD_AXIS_RIGHT_TRIGGER;
    default:
        return GAMEPAD_AXIS_LEFT_X;
    }
}

void RaylibInputBackend::PollEvents() {
    // raylib polls internally in EndDrawing/BeginDrawing or PollInputEvents.
    // We don't necessarily need to call anything here for standard raylib.
}

bool RaylibInputBackend::IsKeyDown(KeyCode key) {
    return ::IsKeyDown(MapKey(key));
}

bool RaylibInputBackend::IsKeyPressed(KeyCode key) {
    return ::IsKeyPressed(MapKey(key));
}

bool RaylibInputBackend::IsKeyReleased(KeyCode key) {
    return ::IsKeyReleased(MapKey(key));
}

bool RaylibInputBackend::IsMouseButtonDown(MouseButtonCode button) {
    return ::IsMouseButtonDown(MapMouseButton(button));
}

bool RaylibInputBackend::IsMouseButtonPressed(MouseButtonCode button) {
    return ::IsMouseButtonPressed(MapMouseButton(button));
}

bool RaylibInputBackend::IsMouseButtonReleased(MouseButtonCode button) {
    return ::IsMouseButtonReleased(MapMouseButton(button));
}

float RaylibInputBackend::GetMouseX() {
    return (float)::GetMouseX();
}

float RaylibInputBackend::GetMouseY() {
    return (float)::GetMouseY();
}

float RaylibInputBackend::GetMouseDeltaX() {
    return ::GetMouseDelta().x;
}

float RaylibInputBackend::GetMouseDeltaY() {
    return ::GetMouseDelta().y;
}

bool RaylibInputBackend::IsGamepadButtonDown(GamepadButtonCode button) {
    if (!::IsGamepadAvailable(0))
        return false;
    return ::IsGamepadButtonDown(0, MapGamepadButton(button));
}

bool RaylibInputBackend::IsGamepadButtonPressed(GamepadButtonCode button) {
    if (!::IsGamepadAvailable(0))
        return false;
    return ::IsGamepadButtonPressed(0, MapGamepadButton(button));
}

bool RaylibInputBackend::IsGamepadButtonReleased(GamepadButtonCode button) {
    if (!::IsGamepadAvailable(0))
        return false;
    return ::IsGamepadButtonReleased(0, MapGamepadButton(button));
}

float RaylibInputBackend::GetGamepadAxis(GamepadAxisCode axis) {
    if (!::IsGamepadAvailable(0))
        return 0.0f;
    return ::GetGamepadAxisMovement(0, MapGamepadAxis(axis));
}

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
