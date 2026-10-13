#pragma once

#include "Backends/IApplicationBackend.hpp"
#include <string>

namespace BRITE {
namespace Backends {
namespace Raylib {

class RaylibApplicationBackend : public IApplicationBackend {
  public:
    void Init(const std::string& title, int width, int height) override;
    // raylib offers one multisampled back buffer, 4x, so any count above 1
    // asks for that. raylib keeps the request for the rest of the process:
    // once one window has asked, every later raylib window is multisampled,
    // whatever it asks, and nothing raylib offers clears it.
    //
    // raylib keeps its other window flags for the rest of the process too, so
    // the frame and Resizable are applied to the window once it exists, set or
    // cleared, rather than requested as flags an earlier window may have left.
    void Init(const std::string& title, int width, int height, const WindowOptions& options) override;
    void Shutdown() override;

    bool WindowShouldClose() override;
    void SetTargetFPS(int fps) override;

    double GetTime() override;

    int GetScreenWidth() override;
    int GetScreenHeight() override;

    // Borderless full screen is an ordinary window with its frame taken away,
    // placed over the monitor it is on and sized to that monitor's mode. It is
    // not raylib's ToggleBorderlessWindowed nor its ToggleFullscreen: both hand
    // the window to the monitor through GLFW, which makes it topmost -- so it
    // stays over every other window after the person at the screen switches
    // away -- and shows it if it was hidden. This one is never topmost, never
    // shown or focused by the switch, and the monitor's mode is never changed.
    // The windowed size and position are kept here and restored on the way back.
    WindowMode GetWindowMode() override;
    bool SetWindowMode(WindowMode mode) override;
    bool SetWindowSize(int width, int height) override;

  private:
    WindowMode m_mode = WindowMode::Windowed;
    // Where the window was, and how big, when it last left windowed mode.
    int m_windowedX = 0;
    int m_windowedY = 0;
    int m_windowedWidth = 0;
    int m_windowedHeight = 0;
};

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
