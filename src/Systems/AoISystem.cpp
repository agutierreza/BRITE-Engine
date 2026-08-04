#include "AoISystem.hpp"
#include <ECS/Components.hpp>
#include <Systems/Box2DBackend.hpp>
#include <Systems/PhysicsSystem.hpp>
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
                b2BodyId b2Body = Box2DBackend::GetB2Body(rb.RuntimeBody);
                if (b2Body_IsValid(b2Body) && !b2Body_IsEnabled(b2Body)) {
                    b2Body_Enable(b2Body);
                }
            }
        } else if (!inAoI && currentlyHasTag) {
            registry.remove<InAoITag>(entity);
            if (registry.all_of<RigidBodyComponent>(entity)) {
                auto& rb = registry.get<RigidBodyComponent>(entity);
                b2BodyId b2Body = Box2DBackend::GetB2Body(rb.RuntimeBody);
                if (b2Body_IsValid(b2Body) && b2Body_IsEnabled(b2Body)) {
                    b2Body_Disable(b2Body);
                }
            }
        }
    }
}

} // namespace BRITE
