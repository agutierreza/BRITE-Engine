#pragma once

namespace BRITE {

enum class KeyCode {
    // Alphanumeric letters
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    // Top-row numbers
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,

    // Function keys
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    // Modifiers
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
    LeftSuper,
    RightSuper,

    // Navigation & Editing
    Space,
    Escape,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Up,
    Down,
    Left,
    Right,

    // Locks & Special
    CapsLock,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,
    Menu,

    // Punctuation & Symbols
    Grave,
    Minus,
    Equal,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Comma,
    Period,
    Slash,

    // Numeric Keypad
    Kp0,
    Kp1,
    Kp2,
    Kp3,
    Kp4,
    Kp5,
    Kp6,
    Kp7,
    Kp8,
    Kp9,
    KpDecimal,
    KpDivide,
    KpMultiply,
    KpSubtract,
    KpAdd,
    KpEnter,
    KpEqual,

    // Sentinel (Total count of unique sequential keys)
    Count,

    // Compatibility aliases
    One = Num1,
    Two = Num2
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

/// The value an axis reports when nothing is touching it.
///
/// Not zero for every axis: the sticks rest at 0.0, but the analog triggers
/// report their pressure over [-1, 1] with -1.0 fully released, which is the
/// convention of every backend this engine has and of the hardware underneath.
/// Both the manager (before its first poll) and a backend with no gamepad
/// attached report THIS rather than 0.0, so a caller reading a trigger never
/// sees a phantom half-pull from a pad that does not exist.
constexpr float GamepadAxisRestValue(GamepadAxisCode axis) {
    return (axis == GamepadAxisCode::LeftTrigger || axis == GamepadAxisCode::RightTrigger) ? -1.0f : 0.0f;
}

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

// Whether a gamepad was available when the frame was polled. Queued every frame,
// as the axes are, so the state a fixed tick reads is the last frame's before it.
struct GamepadAvailabilityEvent {
    bool available;
};

} // namespace BRITE
