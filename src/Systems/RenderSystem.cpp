#include "RenderSystem.hpp"
#include "../ECS/Components.hpp"
#include <cmath>
#include <tracy/Tracy.hpp>
namespace BRITE {

void RenderSystem::Update(entt::registry& registry, Backends::IRenderBackend* backend, Camera2D* camera) {
    ZoneScoped;
    if (!backend)
        return;

    if (camera) {
        backend->BeginMode2D(*camera);
    }

    auto view = registry.view<TransformComponent, SpriteComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& sprite = view.get<SpriteComponent>(entity);

        Rectangle dest = sprite.DestRect;
        dest.x += transform.Position.x;
        dest.y += transform.Position.y;

        // Apply scale
        dest.width *= transform.Scale.x;
        dest.height *= transform.Scale.y;

        // Extract Z-axis rotation in degrees from quaternion (assuming only Z rotation)
        float rotationDeg = 2.0f * std::atan2(transform.Rotation.z, transform.Rotation.w) * Rad2Deg;

        backend->DrawSprite(sprite.Texture, sprite.SourceRect, dest, sprite.Origin, rotationDeg, sprite.Tint);
    }

    if (camera) {
        backend->EndMode2D();
    }
}

} // namespace BRITE
