#pragma once

#include "Backends/IRenderBackend.hpp"
#include "Math/BriteMath.hpp"
#include <entt/entt.hpp>

namespace BRITE {
class RenderSystem {
  public:
    static void Update(entt::registry& registry, RenderPass& pass, Camera2D* camera = nullptr);
};
} // namespace BRITE
