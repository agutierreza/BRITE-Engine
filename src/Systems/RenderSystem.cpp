#include "RenderSystem.hpp"
#include "../ECS/Components.hpp"
#include <cmath>
#include <tracy/Tracy.hpp>
namespace BRITE {

void RenderSystem::Update(entt::registry& registry, RenderPass& pass, Camera2D* camera, Camera3D* camera3d) {
    ZoneScoped;

    if (camera) {
        pass.Camera = camera;
    }
    if (camera3d) {
        pass.Camera3DPtr = camera3d;
    }

    auto view3d = registry.view<TransformComponent, Primitive3DComponent>();
    for (auto entity : view3d) {
        auto& transform = view3d.get<TransformComponent>(entity);
        auto& primitive = view3d.get<Primitive3DComponent>(entity);

        pass.Primitive3DCommands.push_back(
            {primitive.Type, transform.Position, transform.Rotation, transform.Scale, primitive.Size, primitive.Tint});
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

        pass.SpriteCommands.push_back(
            {sprite.Texture, sprite.SourceRect, dest, sprite.Origin, rotationDeg, sprite.Tint});
    }
}

} // namespace BRITE
