#include "InputManager.hpp"

namespace BRITE {

// Initialize statics
std::unordered_map<KeyCode, bool> InputManager::s_keysDown;
std::unordered_map<KeyCode, bool> InputManager::s_keysPressedThisTick;
std::unordered_map<KeyCode, bool> InputManager::s_keysReleasedThisTick;

std::unordered_map<MouseButtonCode, bool> InputManager::s_buttonsDown;
std::unordered_map<MouseButtonCode, bool> InputManager::s_buttonsPressedThisTick;
std::unordered_map<MouseButtonCode, bool> InputManager::s_buttonsReleasedThisTick;

std::unordered_map<GamepadButtonCode, bool> InputManager::s_gamepadButtonsDown;
std::unordered_map<GamepadButtonCode, bool> InputManager::s_gamepadButtonsPressedThisTick;
std::unordered_map<GamepadButtonCode, bool> InputManager::s_gamepadButtonsReleasedThisTick;
std::unordered_map<GamepadAxisCode, float> InputManager::s_gamepadAxes;

std::unordered_map<uint32_t, std::vector<KeyCode>> InputManager::s_actionKeyBindings;
std::unordered_map<uint32_t, std::vector<GamepadButtonCode>> InputManager::s_actionGamepadBindings;
std::unordered_map<uint32_t, std::vector<MouseButtonCode>> InputManager::s_actionMouseBindings;

float InputManager::s_mouseX = 0.0f;
float InputManager::s_mouseY = 0.0f;
float InputManager::s_mouseDeltaX = 0.0f;
float InputManager::s_mouseDeltaY = 0.0f;
Backends::IInputBackend* InputManager::s_backend = nullptr;

void InputManager::Initialize(Backends::IInputBackend* backend) {
    s_backend = backend;
}

static const KeyCode AllKeys[] = {KeyCode::Space, KeyCode::Escape, KeyCode::Enter, KeyCode::Up,
                                  KeyCode::Down,  KeyCode::Left,   KeyCode::Right, KeyCode::W,
                                  KeyCode::A,     KeyCode::S,      KeyCode::D};

static const MouseButtonCode AllMouseButtons[] = {MouseButtonCode::Left, MouseButtonCode::Right,
                                                  MouseButtonCode::Middle};

static const GamepadButtonCode AllGamepadButtons[] = {
    GamepadButtonCode::LeftFaceUp,    GamepadButtonCode::LeftFaceRight, GamepadButtonCode::LeftFaceDown,
    GamepadButtonCode::LeftFaceLeft,  GamepadButtonCode::RightFaceUp,   GamepadButtonCode::RightFaceRight,
    GamepadButtonCode::RightFaceDown, GamepadButtonCode::RightFaceLeft, GamepadButtonCode::LeftTrigger1,
    GamepadButtonCode::LeftTrigger2,  GamepadButtonCode::RightTrigger1, GamepadButtonCode::RightTrigger2,
    GamepadButtonCode::MiddleLeft,    GamepadButtonCode::Middle,        GamepadButtonCode::MiddleRight,
    GamepadButtonCode::LeftThumb,     GamepadButtonCode::RightThumb};

static const GamepadAxisCode AllGamepadAxes[] = {GamepadAxisCode::LeftX,       GamepadAxisCode::LeftY,
                                                 GamepadAxisCode::RightX,      GamepadAxisCode::RightY,
                                                 GamepadAxisCode::LeftTrigger, GamepadAxisCode::RightTrigger};

void InputManager::PollVariable(entt::dispatcher& dispatcher) {
    if (!s_backend)
        return;

    for (KeyCode key : AllKeys) {
        if (s_backend->IsKeyPressed(key))
            dispatcher.enqueue<KeyDownEvent>(KeyDownEvent{key});
        if (s_backend->IsKeyReleased(key))
            dispatcher.enqueue<KeyUpEvent>(KeyUpEvent{key});
    }

    for (MouseButtonCode btn : AllMouseButtons) {
        if (s_backend->IsMouseButtonPressed(btn))
            dispatcher.enqueue<MouseButtonDownEvent>(MouseButtonDownEvent{btn});
        if (s_backend->IsMouseButtonReleased(btn))
            dispatcher.enqueue<MouseButtonUpEvent>(MouseButtonUpEvent{btn});
    }

    float dx = s_backend->GetMouseDeltaX();
    float dy = s_backend->GetMouseDeltaY();
    if (dx != 0.0f || dy != 0.0f) {
        dispatcher.enqueue<MouseMoveEvent>(MouseMoveEvent{dx, dy, s_backend->GetMouseX(), s_backend->GetMouseY()});
    }

    for (GamepadButtonCode btn : AllGamepadButtons) {
        if (s_backend->IsGamepadButtonPressed(btn))
            dispatcher.enqueue<GamepadButtonDownEvent>(GamepadButtonDownEvent{btn});
        if (s_backend->IsGamepadButtonReleased(btn))
            dispatcher.enqueue<GamepadButtonUpEvent>(GamepadButtonUpEvent{btn});
    }

    for (GamepadAxisCode axis : AllGamepadAxes) {
        float value = s_backend->GetGamepadAxis(axis);
        dispatcher.enqueue<GamepadAxisEvent>(GamepadAxisEvent{axis, value});
    }
}

void InputManager::FlushFixed(entt::dispatcher& dispatcher) {
    s_keysPressedThisTick.clear();
    s_keysReleasedThisTick.clear();
    s_buttonsPressedThisTick.clear();
    s_buttonsReleasedThisTick.clear();
    s_gamepadButtonsPressedThisTick.clear();
    s_gamepadButtonsReleasedThisTick.clear();
    s_mouseDeltaX = 0.0f;
    s_mouseDeltaY = 0.0f;

    dispatcher.sink<KeyDownEvent>().connect<&InputManager::OnKeyDown>();
    dispatcher.sink<KeyUpEvent>().connect<&InputManager::OnKeyUp>();
    dispatcher.sink<MouseButtonDownEvent>().connect<&InputManager::OnMouseDown>();
    dispatcher.sink<MouseButtonUpEvent>().connect<&InputManager::OnMouseUp>();
    dispatcher.sink<MouseMoveEvent>().connect<&InputManager::OnMouseMove>();
    dispatcher.sink<GamepadButtonDownEvent>().connect<&InputManager::OnGamepadButtonDown>();
    dispatcher.sink<GamepadButtonUpEvent>().connect<&InputManager::OnGamepadButtonUp>();
    dispatcher.sink<GamepadAxisEvent>().connect<&InputManager::OnGamepadAxisMove>();

    dispatcher.update<KeyDownEvent>();
    dispatcher.update<KeyUpEvent>();
    dispatcher.update<MouseButtonDownEvent>();
    dispatcher.update<MouseButtonUpEvent>();
    dispatcher.update<MouseMoveEvent>();
    dispatcher.update<GamepadButtonDownEvent>();
    dispatcher.update<GamepadButtonUpEvent>();
    dispatcher.update<GamepadAxisEvent>();

    dispatcher.sink<KeyDownEvent>().disconnect<&InputManager::OnKeyDown>();
    dispatcher.sink<KeyUpEvent>().disconnect<&InputManager::OnKeyUp>();
    dispatcher.sink<MouseButtonDownEvent>().disconnect<&InputManager::OnMouseDown>();
    dispatcher.sink<MouseButtonUpEvent>().disconnect<&InputManager::OnMouseUp>();
    dispatcher.sink<MouseMoveEvent>().disconnect<&InputManager::OnMouseMove>();
    dispatcher.sink<GamepadButtonDownEvent>().disconnect<&InputManager::OnGamepadButtonDown>();
    dispatcher.sink<GamepadButtonUpEvent>().disconnect<&InputManager::OnGamepadButtonUp>();
    dispatcher.sink<GamepadAxisEvent>().disconnect<&InputManager::OnGamepadAxisMove>();
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

void InputManager::OnGamepadButtonDown(const GamepadButtonDownEvent& event) {
    if (!s_gamepadButtonsDown[event.button]) {
        s_gamepadButtonsPressedThisTick[event.button] = true;
    }
    s_gamepadButtonsDown[event.button] = true;
}

void InputManager::OnGamepadButtonUp(const GamepadButtonUpEvent& event) {
    s_gamepadButtonsReleasedThisTick[event.button] = true;
    s_gamepadButtonsDown[event.button] = false;
}

void InputManager::OnGamepadAxisMove(const GamepadAxisEvent& event) {
    s_gamepadAxes[event.axis] = event.value;
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

bool InputManager::IsGamepadButtonPressed(GamepadButtonCode button) {
    return s_gamepadButtonsPressedThisTick[button];
}
bool InputManager::IsGamepadButtonDown(GamepadButtonCode button) {
    return s_gamepadButtonsDown[button];
}
bool InputManager::IsGamepadButtonReleased(GamepadButtonCode button) {
    return s_gamepadButtonsReleasedThisTick[button];
}
float InputManager::GetGamepadAxis(GamepadAxisCode axis) {
    return s_gamepadAxes[axis];
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
