#pragma once

#include <cstdint>
#include <string>

namespace BRITE {
namespace Backends {

class IApplicationBackend {
  public:
    virtual ~IApplicationBackend() = default;

    virtual void Init(const std::string& title, int width, int height) = 0;
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
