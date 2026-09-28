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
}

void RaylibApplicationBackend::Shutdown() {
    ::CloseWindow();
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

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
