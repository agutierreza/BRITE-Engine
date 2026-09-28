#pragma once

#include <cstdint>
#include <string>

namespace BRITE {
namespace Backends {

// What a window is created with. Each can only be chosen before the window
// exists, which is why they travel with the call that creates it.
struct WindowOptions {
    // Samples per pixel of the window's back buffer: multisample anti-aliasing.
    // 1 is none. A backend offers what its platform can, and may round a count
    // it cannot give to one it can.
    int MultisampleCount = 1;
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

    // Window properties
    virtual int GetScreenWidth() = 0;
    virtual int GetScreenHeight() = 0;
};

} // namespace Backends
} // namespace BRITE
