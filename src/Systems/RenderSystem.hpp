#pragma once

#include <entt/entt.hpp>
#include <raylib.h>

namespace BRITE {
class RenderSystem {
  public:
    static void Update(entt::registry& registry, Camera2D* camera = nullptr);
};
} // namespace BRITE
