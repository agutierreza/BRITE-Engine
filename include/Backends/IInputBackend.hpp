#pragma once

#include "Core/InputEvents.hpp"

#include <string>

namespace BRITE {
namespace Backends {

class IInputBackend {
  public:
    virtual ~IInputBackend() = default;

    virtual void PollEvents() = 0;

    // Keyboard
    virtual bool IsKeyDown(KeyCode key) = 0;
    virtual bool IsKeyPressed(KeyCode key) = 0;
    virtual bool IsKeyReleased(KeyCode key) = 0;

    // Mouse
    virtual bool IsMouseButtonDown(MouseButtonCode button) = 0;
    virtual bool IsMouseButtonPressed(MouseButtonCode button) = 0;
    virtual bool IsMouseButtonReleased(MouseButtonCode button) = 0;
    virtual float GetMouseX() = 0;
    virtual float GetMouseY() = 0;
    virtual float GetMouseDeltaX() = 0;
    virtual float GetMouseDeltaY() = 0;

    // Gamepad
    virtual bool IsGamepadButtonDown(GamepadButtonCode button) = 0;
    virtual bool IsGamepadButtonPressed(GamepadButtonCode button) = 0;
    virtual bool IsGamepadButtonReleased(GamepadButtonCode button) = 0;
    // Sticks over [-1, 1] resting at 0; triggers over [-1, 1] resting at -1.
    // With no gamepad attached, report GamepadAxisRestValue(axis), never 0.0.
    virtual float GetGamepadAxis(GamepadAxisCode axis) = 0;
    // Whether a gamepad is attached and being read. An axis cannot say so: an
    // absent pad reports rest, and so does an attached one nobody is touching.
    // Not pure, so a backend without gamepad support need not answer; one that
    // does not override it reports no gamepad, which is what it has.
    virtual bool IsGamepadAvailable() {
        return false;
    }
    // The attached gamepad's name as the platform reports it, empty when no
    // gamepad is attached. Not pure, for the same reason as IsGamepadAvailable:
    // a backend that does not override it names no gamepad.
    virtual std::string GetGamepadName() {
        return {};
    }
};

} // namespace Backends
} // namespace BRITE
