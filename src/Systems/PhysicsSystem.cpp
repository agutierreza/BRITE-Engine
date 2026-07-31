#include "PhysicsSystem.hpp"
#include "../ECS/Components.hpp"
#include "../ECS/Events.hpp"
#include <tracy/Tracy.hpp>

namespace BRITE {

constexpr float PPM = 30.0f;

void PhysicsSystem::PreStep(entt::registry& registry, b2WorldId worldId) {
    ZoneScoped;
    auto view = registry.view<TransformComponent, RigidBodyComponent>();

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& rb = view.get<RigidBodyComponent>(entity);

        if (!b2Body_IsValid(rb.RuntimeBody)) {
            // Create the body
            b2BodyDef bodyDef = b2DefaultBodyDef();

            switch (rb.Type) {
            case RigidBodyComponent::BodyType::Static:
                bodyDef.type = b2_staticBody;
                break;
            case RigidBodyComponent::BodyType::Kinematic:
                bodyDef.type = b2_kinematicBody;
                break;
            case RigidBodyComponent::BodyType::Dynamic:
                bodyDef.type = b2_dynamicBody;
                break;
            }

            bodyDef.position = {transform.Position.x / PPM, transform.Position.y / PPM};
            bodyDef.rotation = b2MakeRot(transform.Rotation * (PI / 180.0f));
            bodyDef.fixedRotation = rb.FixedRotation;
            bodyDef.gravityScale = rb.GravityScale;
            // Store entity in user data for contact listeners later
            bodyDef.userData = (void*)(uintptr_t)entity;

            rb.RuntimeBody = b2CreateBody(worldId, &bodyDef);

            // Check for BoxCollider
            if (registry.all_of<BoxColliderComponent>(entity)) {
                auto& box = registry.get<BoxColliderComponent>(entity);
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = box.Density;
                shapeDef.friction = box.Friction;
                shapeDef.restitution = box.Restitution;

                b2Polygon poly = b2MakeOffsetBox(box.Size.x / 2.0f / PPM, box.Size.y / 2.0f / PPM,
                                                 {box.Offset.x / PPM, box.Offset.y / PPM}, 0.0f);
                box.RuntimeShape = b2CreatePolygonShape(rb.RuntimeBody, &shapeDef, &poly);
            }

            // Check for CircleCollider
            if (registry.all_of<CircleColliderComponent>(entity)) {
                auto& circle = registry.get<CircleColliderComponent>(entity);
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = circle.Density;
                shapeDef.friction = circle.Friction;
                shapeDef.restitution = circle.Restitution;

                b2Circle circ = {{circle.Offset.x / PPM, circle.Offset.y / PPM}, circle.Radius / PPM};
                circle.RuntimeShape = b2CreateCircleShape(rb.RuntimeBody, &shapeDef, &circ);
            }
        } else if (rb.Type == RigidBodyComponent::BodyType::Kinematic) {
            // If kinematic, sync Transform -> Box2D before step
            b2Vec2 pos = {transform.Position.x / PPM, transform.Position.y / PPM};
            b2Rot rot = b2MakeRot(transform.Rotation * (PI / 180.0f));
            b2Body_SetTransform(rb.RuntimeBody, pos, rot);
        }
    }
}

void PhysicsSystem::PostStep(entt::registry& registry, b2WorldId worldId) {
    ZoneScoped;
    auto view = registry.view<TransformComponent, RigidBodyComponent>();

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& rb = view.get<RigidBodyComponent>(entity);

        if (b2Body_IsValid(rb.RuntimeBody) && rb.Type != RigidBodyComponent::BodyType::Static) {
            b2Vec2 position = b2Body_GetPosition(rb.RuntimeBody);
            b2Rot rotation = b2Body_GetRotation(rb.RuntimeBody);
            float angle = b2Rot_GetAngle(rotation);

            transform.Position.x = position.x * PPM;
            transform.Position.y = position.y * PPM;

            // Box2D uses radians, Raylib uses degrees for drawing.
            transform.Rotation = angle * (180.0f / PI);
        }
    }

    b2ContactEvents contactEvents = b2World_GetContactEvents(worldId);
    if (contactEvents.beginCount > 0 || contactEvents.endCount > 0 || contactEvents.hitCount > 0) {
        entt::dispatcher* dispatcher = registry.ctx().find<entt::dispatcher>();
        if (!dispatcher) {
            dispatcher = &registry.ctx().emplace<entt::dispatcher>();
        }

        for (int i = 0; i < contactEvents.beginCount; ++i) {
            b2ContactBeginTouchEvent* event = contactEvents.beginEvents + i;
            b2BodyId bodyA = b2Shape_GetBody(event->shapeIdA);
            b2BodyId bodyB = b2Shape_GetBody(event->shapeIdB);
            entt::entity entityA = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyA);
            entt::entity entityB = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyB);
            dispatcher->trigger<PhysicsCollisionEvent>(PhysicsCollisionEvent{entityA, entityB});
        }

        for (int i = 0; i < contactEvents.endCount; ++i) {
            b2ContactEndTouchEvent* event = contactEvents.endEvents + i;
            b2BodyId bodyA = b2Shape_GetBody(event->shapeIdA);
            b2BodyId bodyB = b2Shape_GetBody(event->shapeIdB);
            entt::entity entityA = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyA);
            entt::entity entityB = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyB);
            dispatcher->trigger<PhysicsCollisionEndEvent>(PhysicsCollisionEndEvent{entityA, entityB});
        }

        for (int i = 0; i < contactEvents.hitCount; ++i) {
            b2ContactHitEvent* event = contactEvents.hitEvents + i;
            b2BodyId bodyA = b2Shape_GetBody(event->shapeIdA);
            b2BodyId bodyB = b2Shape_GetBody(event->shapeIdB);
            entt::entity entityA = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyA);
            entt::entity entityB = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyB);
            dispatcher->trigger<PhysicsHitEvent>(
                PhysicsHitEvent{entityA, entityB, event->approachSpeed, event->normal.x, event->normal.y});
        }
    }
}

} // namespace BRITE
