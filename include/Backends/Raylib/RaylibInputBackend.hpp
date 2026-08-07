#pragma once

#include "Backends/IInputBackend.hpp"

namespace BRITE {
namespace Backends {
namespace Raylib {

class RaylibInputBackend : public IInputBackend {
  public:
    void PollEvents() override;

    bool IsKeyDown(KeyCode key) override;
    bool IsKeyPressed(KeyCode key) override;
    bool IsKeyReleased(KeyCode key) override;

    bool IsMouseButtonDown(MouseButtonCode button) override;
    bool IsMouseButtonPressed(MouseButtonCode button) override;
    bool IsMouseButtonReleased(MouseButtonCode button) override;
    float GetMouseX() override;
    float GetMouseY() override;
    float GetMouseDeltaX() override;
    float GetMouseDeltaY() override;

    bool IsGamepadButtonDown(GamepadButtonCode button) override;
    bool IsGamepadButtonPressed(GamepadButtonCode button) override;
    bool IsGamepadButtonReleased(GamepadButtonCode button) override;
    float GetGamepadAxis(GamepadAxisCode axis) override;
};

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
