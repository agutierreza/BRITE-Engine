#include "RenderSystem.hpp"
#include "../ECS/Components.hpp"
#include <tracy/Tracy.hpp>

namespace BRITE {

void RenderSystem::Update(entt::registry& registry, Camera2D* camera) {
    ZoneScoped;
    if (camera) {
        BeginMode2D(*camera);
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

        DrawTexturePro(sprite.Texture, sprite.SourceRect, dest, sprite.Origin, transform.Rotation, sprite.Tint);
    }

    if (camera) {
        EndMode2D();
    }
}

} // namespace BRITE
