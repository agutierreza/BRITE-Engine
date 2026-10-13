#include "Backends/Raylib/RaylibApplicationBackend.hpp"
#include <raylib.h>

namespace BRITE {
namespace Backends {
namespace Raylib {

void RaylibApplicationBackend::Init(const std::string& title, int width, int height) {
    ::InitWindow(width, height, title.c_str());
}

void RaylibApplicationBackend::Init(const std::string& title, int width, int height, const WindowOptions& options) {
    // A hint, read when the window is created. raylib adds flags to whatever
    // the caller already set, so nothing else the caller asked for is lost.
    if (options.MultisampleCount > 1)
        ::SetConfigFlags(FLAG_MSAA_4X_HINT);
    ::InitWindow(width, height, title.c_str());

    // Applied now the window exists: raylib creates a window with whatever
    // flags the process's last window left, so each is set or cleared here.
    if (options.Resizable)
        ::SetWindowState(FLAG_WINDOW_RESIZABLE);
    else if (::IsWindowState(FLAG_WINDOW_RESIZABLE))
        ::ClearWindowState(FLAG_WINDOW_RESIZABLE);
    if (::IsWindowState(FLAG_WINDOW_UNDECORATED))
        ::ClearWindowState(FLAG_WINDOW_UNDECORATED);

    m_mode = WindowMode::Windowed;
    if (options.Mode != WindowMode::Windowed)
        SetWindowMode(options.Mode);
}

void RaylibApplicationBackend::Shutdown() {
    ::CloseWindow();
    m_mode = WindowMode::Windowed;
}

bool RaylibApplicationBackend::WindowShouldClose() {
    return ::WindowShouldClose();
}

void RaylibApplicationBackend::SetTargetFPS(int fps) {
    ::SetTargetFPS(fps);
}

double RaylibApplicationBackend::GetTime() {
    return ::GetTime();
}

int RaylibApplicationBackend::GetScreenWidth() {
    return ::GetScreenWidth();
}

int RaylibApplicationBackend::GetScreenHeight() {
    return ::GetScreenHeight();
}

WindowMode RaylibApplicationBackend::GetWindowMode() {
    return m_mode;
}

bool RaylibApplicationBackend::SetWindowMode(WindowMode mode) {
    if (!::IsWindowReady())
        return false;
    if (mode == m_mode)
        return true;

    if (mode == WindowMode::BorderlessFullscreen) {
        const int monitor = ::GetCurrentMonitor();
        const int monitorWidth = ::GetMonitorWidth(monitor);
        const int monitorHeight = ::GetMonitorHeight(monitor);
        if (monitorWidth <= 0 || monitorHeight <= 0)
            return false; // no monitor to cover: stay as it is, and say so
        const ::Vector2 windowAt = ::GetWindowPosition();
        m_windowedX = static_cast<int>(windowAt.x);
        m_windowedY = static_cast<int>(windowAt.y);
        m_windowedWidth = ::GetScreenWidth();
        m_windowedHeight = ::GetScreenHeight();

        const ::Vector2 monitorAt = ::GetMonitorPosition(monitor);
        ::SetWindowState(FLAG_WINDOW_UNDECORATED);
        ::SetWindowPosition(static_cast<int>(monitorAt.x), static_cast<int>(monitorAt.y));
        ::SetWindowSize(monitorWidth, monitorHeight);
        m_mode = WindowMode::BorderlessFullscreen;
        return true;
    }

    // Back to windowed: the frame first, so the size set afterwards is the
    // drawing area's, as it was when the window left.
    ::ClearWindowState(FLAG_WINDOW_UNDECORATED);
    ::SetWindowSize(m_windowedWidth, m_windowedHeight);
    ::SetWindowPosition(m_windowedX, m_windowedY);
    m_mode = WindowMode::Windowed;
    return true;
}

bool RaylibApplicationBackend::SetWindowSize(int width, int height) {
    if (!::IsWindowReady() || m_mode != WindowMode::Windowed || width <= 0 || height <= 0)
        return false;
    ::SetWindowSize(width, height);
    return true;
}

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
