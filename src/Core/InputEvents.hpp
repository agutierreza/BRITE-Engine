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

} // namespace BRITE
