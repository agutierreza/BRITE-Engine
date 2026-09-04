#include "Backends/Raylib/RaylibInputBackend.hpp"
#include <raylib.h>

namespace BRITE {
namespace Backends {
namespace Raylib {

static int MapKey(KeyCode key) {
    switch (key) {
    // Alphanumeric letters
    case KeyCode::A:
        return KEY_A;
    case KeyCode::B:
        return KEY_B;
    case KeyCode::C:
        return KEY_C;
    case KeyCode::D:
        return KEY_D;
    case KeyCode::E:
        return KEY_E;
    case KeyCode::F:
        return KEY_F;
    case KeyCode::G:
        return KEY_G;
    case KeyCode::H:
        return KEY_H;
    case KeyCode::I:
        return KEY_I;
    case KeyCode::J:
        return KEY_J;
    case KeyCode::K:
        return KEY_K;
    case KeyCode::L:
        return KEY_L;
    case KeyCode::M:
        return KEY_M;
    case KeyCode::N:
        return KEY_N;
    case KeyCode::O:
        return KEY_O;
    case KeyCode::P:
        return KEY_P;
    case KeyCode::Q:
        return KEY_Q;
    case KeyCode::R:
        return KEY_R;
    case KeyCode::S:
        return KEY_S;
    case KeyCode::T:
        return KEY_T;
    case KeyCode::U:
        return KEY_U;
    case KeyCode::V:
        return KEY_V;
    case KeyCode::W:
        return KEY_W;
    case KeyCode::X:
        return KEY_X;
    case KeyCode::Y:
        return KEY_Y;
    case KeyCode::Z:
        return KEY_Z;

    // Top-row numbers
    case KeyCode::Num0:
        return KEY_ZERO;
    case KeyCode::Num1:
        return KEY_ONE;
    case KeyCode::Num2:
        return KEY_TWO;
    case KeyCode::Num3:
        return KEY_THREE;
    case KeyCode::Num4:
        return KEY_FOUR;
    case KeyCode::Num5:
        return KEY_FIVE;
    case KeyCode::Num6:
        return KEY_SIX;
    case KeyCode::Num7:
        return KEY_SEVEN;
    case KeyCode::Num8:
        return KEY_EIGHT;
    case KeyCode::Num9:
        return KEY_NINE;

    // Function keys
    case KeyCode::F1:
        return KEY_F1;
    case KeyCode::F2:
        return KEY_F2;
    case KeyCode::F3:
        return KEY_F3;
    case KeyCode::F4:
        return KEY_F4;
    case KeyCode::F5:
        return KEY_F5;
    case KeyCode::F6:
        return KEY_F6;
    case KeyCode::F7:
        return KEY_F7;
    case KeyCode::F8:
        return KEY_F8;
    case KeyCode::F9:
        return KEY_F9;
    case KeyCode::F10:
        return KEY_F10;
    case KeyCode::F11:
        return KEY_F11;
    case KeyCode::F12:
        return KEY_F12;

    // Modifiers
    case KeyCode::LeftShift:
        return KEY_LEFT_SHIFT;
    case KeyCode::RightShift:
        return KEY_RIGHT_SHIFT;
    case KeyCode::LeftControl:
        return KEY_LEFT_CONTROL;
    case KeyCode::RightControl:
        return KEY_RIGHT_CONTROL;
    case KeyCode::LeftAlt:
        return KEY_LEFT_ALT;
    case KeyCode::RightAlt:
        return KEY_RIGHT_ALT;
    case KeyCode::LeftSuper:
        return KEY_LEFT_SUPER;
    case KeyCode::RightSuper:
        return KEY_RIGHT_SUPER;

    // Navigation & Editing
    case KeyCode::Space:
        return KEY_SPACE;
    case KeyCode::Escape:
        return KEY_ESCAPE;
    case KeyCode::Enter:
        return KEY_ENTER;
    case KeyCode::Tab:
        return KEY_TAB;
    case KeyCode::Backspace:
        return KEY_BACKSPACE;
    case KeyCode::Insert:
        return KEY_INSERT;
    case KeyCode::Delete:
        return KEY_DELETE;
    case KeyCode::Home:
        return KEY_HOME;
    case KeyCode::End:
        return KEY_END;
    case KeyCode::PageUp:
        return KEY_PAGE_UP;
    case KeyCode::PageDown:
        return KEY_PAGE_DOWN;
    case KeyCode::Up:
        return KEY_UP;
    case KeyCode::Down:
        return KEY_DOWN;
    case KeyCode::Left:
        return KEY_LEFT;
    case KeyCode::Right:
        return KEY_RIGHT;

    // Locks & Special
    case KeyCode::CapsLock:
        return KEY_CAPS_LOCK;
    case KeyCode::ScrollLock:
        return KEY_SCROLL_LOCK;
    case KeyCode::NumLock:
        return KEY_NUM_LOCK;
    case KeyCode::PrintScreen:
        return KEY_PRINT_SCREEN;
    case KeyCode::Pause:
        return KEY_PAUSE;
    case KeyCode::Menu:
        return KEY_KB_MENU;

    // Punctuation & Symbols
    case KeyCode::Grave:
        return KEY_GRAVE;
    case KeyCode::Minus:
        return KEY_MINUS;
    case KeyCode::Equal:
        return KEY_EQUAL;
    case KeyCode::LeftBracket:
        return KEY_LEFT_BRACKET;
    case KeyCode::RightBracket:
        return KEY_RIGHT_BRACKET;
    case KeyCode::Backslash:
        return KEY_BACKSLASH;
    case KeyCode::Semicolon:
        return KEY_SEMICOLON;
    case KeyCode::Apostrophe:
        return KEY_APOSTROPHE;
    case KeyCode::Comma:
        return KEY_COMMA;
    case KeyCode::Period:
        return KEY_PERIOD;
    case KeyCode::Slash:
        return KEY_SLASH;

    // Numeric Keypad
    case KeyCode::Kp0:
        return KEY_KP_0;
    case KeyCode::Kp1:
        return KEY_KP_1;
    case KeyCode::Kp2:
        return KEY_KP_2;
    case KeyCode::Kp3:
        return KEY_KP_3;
    case KeyCode::Kp4:
        return KEY_KP_4;
    case KeyCode::Kp5:
        return KEY_KP_5;
    case KeyCode::Kp6:
        return KEY_KP_6;
    case KeyCode::Kp7:
        return KEY_KP_7;
    case KeyCode::Kp8:
        return KEY_KP_8;
    case KeyCode::Kp9:
        return KEY_KP_9;
    case KeyCode::KpDecimal:
        return KEY_KP_DECIMAL;
    case KeyCode::KpDivide:
        return KEY_KP_DIVIDE;
    case KeyCode::KpMultiply:
        return KEY_KP_MULTIPLY;
    case KeyCode::KpSubtract:
        return KEY_KP_SUBTRACT;
    case KeyCode::KpAdd:
        return KEY_KP_ADD;
    case KeyCode::KpEnter:
        return KEY_KP_ENTER;
    case KeyCode::KpEqual:
        return KEY_KP_EQUAL;

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
    // No pad means every axis is at rest -- and rest is -1.0 for a trigger, not
    // 0.0, which is what raylib itself reports for a released trigger on an
    // attached pad. Returning 0.0 here read as a half-pulled trigger to any
    // caller that could not tell an absent pad from a present one.
    if (!::IsGamepadAvailable(0))
        return GamepadAxisRestValue(axis);
    return ::GetGamepadAxisMovement(0, MapGamepadAxis(axis));
}

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
