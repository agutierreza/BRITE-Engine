#include "PhysicsSystem.hpp"
#include "../ECS/Components.hpp"
#include <tracy/Tracy.hpp>

namespace BRITE {

void PhysicsSystem::Update(entt::registry& registry) {
    ZoneScoped;
    auto view = registry.view<TransformComponent, PhysicsBodyComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& physics = view.get<PhysicsBodyComponent>(entity);

        if (physics.Body) {
            b2Vec2 position = physics.Body->GetPosition();
            float angle = physics.Body->GetAngle();

            // Assuming a 1:1 ratio for physics-to-pixels currently, 
            // though typically a scalar (e.g. 30 pixels = 1 meter) is used.
            transform.Position.x = position.x;
            transform.Position.y = position.y;
            
            // Box2D uses radians, Raylib uses degrees for drawing.
            transform.Rotation = angle * (180.0f / PI);
        }
    }
}

} // namespace BRITE
