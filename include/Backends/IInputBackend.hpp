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
    // Whether the platform repeated `key` since the backend last polled it: the
    // operating system's key repeat, sent while a key is held, after its own
    // delay and at its own rate (both the user's settings, not the
    // application's). Never for the press that began the hold -- IsKeyPressed
    // reports that one. At least once: a poll in which the platform repeated a
    // key several times reports true once, as IsKeyPressed reports an edge.
    // Not pure, so a backend whose platform does not repeat keys need not
    // answer; one that does not override it reports no repeats, which is what
    // it has.
    virtual bool IsKeyRepeated(KeyCode /*key*/) {
        return false;
    }

    // Mouse
    virtual bool IsMouseButtonDown(MouseButtonCode button) = 0;
    virtual bool IsMouseButtonPressed(MouseButtonCode button) = 0;
    virtual bool IsMouseButtonReleased(MouseButtonCode button) = 0;
    virtual float GetMouseX() = 0;
    virtual float GetMouseY() = 0;
    virtual float GetMouseDeltaX() = 0;
    virtual float GetMouseDeltaY() = 0;
    // The mouse wheel's movement since the backend last polled its platform, in
    // notches: 1.0 for one notch of a notched wheel, fractions from a smooth
    // wheel or a touchpad. Positive y is the wheel turned away from the user
    // (scrolling up); positive x is the wheel pushed or tilted to the left. Both
    // point toward the top left of a page, which is GLFW's convention on every
    // platform it supports (it inverts Windows' horizontal wheel to match macOS
    // and X11). Each axis is reported on its own: a wheel with no horizontal
    // axis reports none on x.
    // Not pure, so a backend without a wheel need not answer; one that does not
    // override them reports no movement, which is what it has.
    virtual float GetMouseWheelMoveX() {
        return 0.0f;
    }
    virtual float GetMouseWheelMoveY() {
        return 0.0f;
    }

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
