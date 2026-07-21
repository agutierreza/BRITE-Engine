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

enum class MouseButtonCode {
    Left,
    Right,
    Middle
};

class InputManager {
public:
    static void Update(); // Call once per frame before systems

    static bool IsKeyPressed(KeyCode key);
    static bool IsKeyDown(KeyCode key);
    static bool IsKeyReleased(KeyCode key);

    static bool IsMouseButtonPressed(MouseButtonCode button);
    static bool IsMouseButtonDown(MouseButtonCode button);
    static bool IsMouseButtonReleased(MouseButtonCode button);
    
    // Abstracting mouse position
    static float GetMouseX();
    static float GetMouseY();
};

} // namespace BRITE
