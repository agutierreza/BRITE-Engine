#pragma once

#include <entt/entt.hpp>
#include <raylib.h>

namespace BRITE {

struct AoIConfig {
    Vector2 Center = {0.0f, 0.0f};
    float HalfWidth = 1000.0f;
    float HalfHeight = 1000.0f;

    // Optional wrap-around boundaries for toroidal worlds (0 means standard
    // non-wrapping Euclidean world)
    float WorldWidth = 0.0f;
    float WorldHeight = 0.0f;
};

class AoISystem {
  public:
    static void Update(entt::registry& registry, const AoIConfig& config);
};

} // namespace BRITE
