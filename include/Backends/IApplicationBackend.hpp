#pragma once

#include <cstdint>
#include <string>

namespace BRITE {
namespace Backends {

// How a window occupies the screen.
enum class WindowMode {
    // A window with a frame, of the size it was given.
    Windowed,
    // No frame, covering the whole monitor the window is on, at that monitor's
    // own resolution -- the size it was given is ignored while it lasts, and is
    // the size it returns to. The monitor's mode is never changed, so the
    // picture is drawn at the monitor's native resolution and nothing scales it.
    BorderlessFullscreen,
};

// What a window is created with. Each can only be chosen before the window
// exists, which is why they travel with the call that creates it -- except the
// mode, which is also switched afterwards with SetWindowMode.
struct WindowOptions {
    // Samples per pixel of the window's back buffer: multisample anti-aliasing.
    // 1 is none. A backend offers what its platform can, and may round a count
    // it cannot give to one it can.
    int MultisampleCount = 1;
    // The mode the window opens in. Opened full screen, the size it was given is
    // its windowed size: the one it takes when it leaves full screen.
    WindowMode Mode = WindowMode::Windowed;
    // Whether the person at the screen may resize the window by its frame.
    bool Resizable = false;
};

class IApplicationBackend {
  public:
    virtual ~IApplicationBackend() = default;

    virtual void Init(const std::string& title, int width, int height) = 0;
    // Create the window with options. The default ignores them and creates a
    // plain window, so that an implementation written before options existed
    // still builds; a backend that honours them overrides this.
    virtual void Init(const std::string& title, int width, int height, const WindowOptions& options) {
        (void)options;
        Init(title, width, height);
    }
    virtual void Shutdown() = 0;

    virtual bool WindowShouldClose() = 0;
    virtual void SetTargetFPS(int fps) = 0;

    // Timing
    virtual double GetTime() = 0;

    // Window properties. The size of what the window draws into, in pixels:
    // read again whenever it may have changed -- after a mode switch, a resize
    // by the person at the screen, or SetWindowSize.
    virtual int GetScreenWidth() = 0;
    virtual int GetScreenHeight() = 0;

    // The window's mode, and switching it after the window exists. Returns
    // whether the window is in `mode` afterwards. The defaults describe a
    // backend written before modes existed: always windowed, and unable to
    // leave it -- so asking it for windowed succeeds and asking it for anything
    // else is refused, never half-done.
    virtual WindowMode GetWindowMode() {
        return WindowMode::Windowed;
    }
    virtual bool SetWindowMode(WindowMode mode) {
        return mode == WindowMode::Windowed;
    }

    // Resize a windowed window. Returns whether it did; a backend that cannot,
    // the default, refuses. Only for a windowed window: what a full-screen one
    // shows is its monitor's size, so a caller keeps the size for when it
    // returns to windowed.
    virtual bool SetWindowSize(int width, int height) {
        (void)width;
        (void)height;
        return false;
    }
};

} // namespace Backends
} // namespace BRITE
