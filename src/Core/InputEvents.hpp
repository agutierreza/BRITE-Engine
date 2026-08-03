#pragma once

namespace BRITE {

enum class KeyCode {
    Space,
    Escape,
    Enter,
    Up,
    Down,
    Left,
    Right,
    W,
    A,
    S,
    D,
    // Add more as needed
};

enum class MouseButtonCode { Left, Right, Middle };

enum class GamepadButtonCode {
    Unknown,
    LeftFaceUp,
    LeftFaceRight,
    LeftFaceDown,
    LeftFaceLeft,
    RightFaceUp,
    RightFaceRight,
    RightFaceDown,
    RightFaceLeft,
    LeftTrigger1,
    LeftTrigger2,
    RightTrigger1,
    RightTrigger2,
    MiddleLeft,
    Middle,
    MiddleRight,
    LeftThumb,
    RightThumb
};

enum class GamepadAxisCode { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger };

struct KeyDownEvent {
    KeyCode key;
};

struct KeyUpEvent {
    KeyCode key;
};

struct MouseButtonDownEvent {
    MouseButtonCode button;
};

struct MouseButtonUpEvent {
    MouseButtonCode button;
};

struct MouseMoveEvent {
    float deltaX;
    float deltaY;
    float absX;
    float absY;
};

struct GamepadButtonDownEvent {
    GamepadButtonCode button;
};

struct GamepadButtonUpEvent {
    GamepadButtonCode button;
};

struct GamepadAxisEvent {
    GamepadAxisCode axis;
    float value;
};

} // namespace BRITE
