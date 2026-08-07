#pragma once

#include "Backends/IRenderBackend.hpp"
#include "Math/BriteMath.hpp"
#include <entt/entt.hpp>

namespace BRITE {
class RenderSystem {
  public:
    static void Update(entt::registry& registry, Backends::IRenderBackend* backend, Camera2D* camera = nullptr);
};
} // namespace BRITE
