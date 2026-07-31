#include "AoISystem.hpp"
#include <ECS/Components.hpp>
#include <box2d/box2d.h>
#include <cmath>

namespace BRITE {

void AoISystem::Update(entt::registry& registry, const AoIConfig& config) {
    auto view = registry.view<TransformComponent, AoIComponent>();

    for (auto entity : view) {
        auto [trans, aoi] = view.get<TransformComponent, AoIComponent>(entity);
        if (!aoi.Active)
            continue;

        float dx = trans.Position.x - config.Center.x;
        if (config.WorldWidth > 0.0f) {
            if (dx > config.WorldWidth * 0.5f)
                dx -= config.WorldWidth;
            else if (dx < -config.WorldWidth * 0.5f)
                dx += config.WorldWidth;
        }

        float dy = trans.Position.y - config.Center.y;
        if (config.WorldHeight > 0.0f) {
            if (dy > config.WorldHeight * 0.5f)
                dy -= config.WorldHeight;
            else if (dy < -config.WorldHeight * 0.5f)
                dy += config.WorldHeight;
        }

        bool inAoI = (std::abs(dx) <= config.HalfWidth) && (std::abs(dy) <= config.HalfHeight);

        bool currentlyHasTag = registry.all_of<InAoITag>(entity);

        if (inAoI && !currentlyHasTag) {
            registry.emplace<InAoITag>(entity);
            if (registry.all_of<RigidBodyComponent>(entity)) {
                auto& rb = registry.get<RigidBodyComponent>(entity);
                if (b2Body_IsValid(rb.RuntimeBody) && !b2Body_IsEnabled(rb.RuntimeBody)) {
                    b2Body_Enable(rb.RuntimeBody);
                }
            }
        } else if (!inAoI && currentlyHasTag) {
            registry.remove<InAoITag>(entity);
            if (registry.all_of<RigidBodyComponent>(entity)) {
                auto& rb = registry.get<RigidBodyComponent>(entity);
                if (b2Body_IsValid(rb.RuntimeBody) && b2Body_IsEnabled(rb.RuntimeBody)) {
                    b2Body_Disable(rb.RuntimeBody);
                }
            }
        }
    }
}

} // namespace BRITE
