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
    void Init(const std::string& title, int width, int height, const WindowOptions& options) override;
    void Shutdown() override;

    bool WindowShouldClose() override;
    void SetTargetFPS(int fps) override;

    double GetTime() override;

    int GetScreenWidth() override;
    int GetScreenHeight() override;
};

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
