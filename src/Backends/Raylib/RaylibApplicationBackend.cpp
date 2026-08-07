#include "Backends/Raylib/RaylibApplicationBackend.hpp"
#include <raylib.h>

namespace BRITE {
namespace Backends {
namespace Raylib {

void RaylibApplicationBackend::Init(const std::string& title, int width, int height) {
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
