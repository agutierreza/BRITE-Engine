#include "PhysicsSystem.hpp"
#include "../ECS/Components.hpp"
#include "../ECS/Events.hpp"
#include <box2d/math_functions.h>
#include <tracy/Tracy.hpp>

namespace BRITE {

constexpr float PPM = 30.0f;

void PhysicsSystem::PreStep(entt::registry& registry, b2WorldId worldId) {
    ZoneScoped;
    auto view = registry.view<TransformComponent, RigidBodyComponent>();

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& rb = view.get<RigidBodyComponent>(entity);

        if (!b2Body_IsValid(GetB2Body(rb.RuntimeBody))) {
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
            bodyDef.rotation = b2MakeRot(QuaternionToEuler(transform.Rotation).z);
            bodyDef.fixedRotation = rb.FixedRotation;
            bodyDef.gravityScale = rb.GravityScale;
            // Store entity in user data for contact listeners later
            bodyDef.userData = (void*)(uintptr_t)entity;

            rb.RuntimeBody = ToHandle(b2CreateBody(worldId, &bodyDef));

            // Check for BoxCollider
            if (registry.all_of<BoxColliderComponent>(entity)) {
                auto& box = registry.get<BoxColliderComponent>(entity);
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = box.Material->Density;
                shapeDef.friction = box.Material->Friction;
                shapeDef.restitution = box.Material->Restitution;
                shapeDef.isSensor = box.IsSensor;

                b2Polygon poly = b2MakeOffsetBox(box.Size.x / 2.0f / PPM, box.Size.y / 2.0f / PPM,
                                                 {box.Offset.x / PPM, box.Offset.y / PPM}, 0.0f);
                box.RuntimeShape = ToHandle(b2CreatePolygonShape(GetB2Body(rb.RuntimeBody), &shapeDef, &poly));
            }

            // Check for CircleCollider
            if (registry.all_of<CircleColliderComponent>(entity)) {
                auto& circle = registry.get<CircleColliderComponent>(entity);
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = circle.Material->Density;
                shapeDef.friction = circle.Material->Friction;
                shapeDef.restitution = circle.Material->Restitution;
                shapeDef.isSensor = circle.IsSensor;

                b2Circle circ = {{circle.Offset.x / PPM, circle.Offset.y / PPM}, circle.Radius / PPM};
                circle.RuntimeShape = ToHandle(b2CreateCircleShape(GetB2Body(rb.RuntimeBody), &shapeDef, &circ));
            }
        } else if (rb.Type == RigidBodyComponent::BodyType::Kinematic) {
            // If kinematic, sync Transform -> Box2D before step
            b2Vec2 pos = {transform.Position.x / PPM, transform.Position.y / PPM};
            b2Rot rot = b2MakeRot(QuaternionToEuler(transform.Rotation).z);
            b2Body_SetTransform(GetB2Body(rb.RuntimeBody), pos, rot);
        }

        // Check for DistanceJointComponent
        if (registry.all_of<DistanceJointComponent>(entity)) {
            auto& joint = registry.get<DistanceJointComponent>(entity);
            if (!b2Joint_IsValid(GetB2Joint(joint.RuntimeJoint)) && registry.valid(joint.TargetEntity)) {
                if (registry.all_of<RigidBodyComponent>(joint.TargetEntity)) {
                    auto& targetRb = registry.get<RigidBodyComponent>(joint.TargetEntity);
                    if (b2Body_IsValid(GetB2Body(targetRb.RuntimeBody))) {
                        b2DistanceJointDef jointDef = b2DefaultDistanceJointDef();
                        jointDef.bodyIdA = GetB2Body(rb.RuntimeBody);
                        jointDef.bodyIdB = GetB2Body(targetRb.RuntimeBody);
                        jointDef.localAnchorA = {0.0f, 0.0f};
                        jointDef.localAnchorB = {0.0f, 0.0f};
                        jointDef.length = joint.Length / PPM;
                        jointDef.collideConnected = joint.CollideConnected;
                        if (joint.EnableSpring) {
                            jointDef.enableSpring = true;
                            jointDef.hertz = joint.Hertz;
                            jointDef.dampingRatio = joint.DampingRatio;
                        }
                        joint.RuntimeJoint = ToHandle(b2CreateDistanceJoint(worldId, &jointDef));
                    }
                }
            }
        }

        // Check for RevoluteJointComponent
        if (registry.all_of<RevoluteJointComponent>(entity)) {
            auto& joint = registry.get<RevoluteJointComponent>(entity);
            if (!b2Joint_IsValid(GetB2Joint(joint.RuntimeJoint)) && registry.valid(joint.TargetEntity)) {
                if (registry.all_of<RigidBodyComponent>(joint.TargetEntity)) {
                    auto& targetRb = registry.get<RigidBodyComponent>(joint.TargetEntity);
                    if (b2Body_IsValid(GetB2Body(targetRb.RuntimeBody))) {
                        b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
                        jointDef.bodyIdA = GetB2Body(rb.RuntimeBody);
                        jointDef.bodyIdB = GetB2Body(targetRb.RuntimeBody);
                        jointDef.localAnchorA = {joint.LocalAnchorA.x / PPM, joint.LocalAnchorA.y / PPM};
                        jointDef.localAnchorB = {joint.LocalAnchorB.x / PPM, joint.LocalAnchorB.y / PPM};
                        jointDef.referenceAngle = joint.ReferenceAngle * (PI / 180.0f);
                        jointDef.enableLimit = joint.EnableLimit;
                        jointDef.lowerAngle = joint.LowerAngle * (PI / 180.0f);
                        jointDef.upperAngle = joint.UpperAngle * (PI / 180.0f);
                        jointDef.collideConnected = joint.CollideConnected;
                        joint.RuntimeJoint = ToHandle(b2CreateRevoluteJoint(worldId, &jointDef));
                    }
                }
            }
        }
    }
}

void PhysicsSystem::PostStep(entt::registry& registry, b2WorldId worldId) {
    ZoneScoped;
    auto view = registry.view<TransformComponent, RigidBodyComponent>();

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& rb = view.get<RigidBodyComponent>(entity);

        if (b2Body_IsValid(GetB2Body(rb.RuntimeBody)) && rb.Type != RigidBodyComponent::BodyType::Static) {
            b2Vec2 position = b2Body_GetPosition(GetB2Body(rb.RuntimeBody));
            b2Rot rotation = b2Body_GetRotation(GetB2Body(rb.RuntimeBody));
            float angle = b2Rot_GetAngle(rotation);

            transform.Position.x = position.x * PPM;
            transform.Position.y = position.y * PPM;

            // Box2D uses radians, map to Z-axis quaternion rotation
            transform.Rotation = QuaternionFromAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, angle);
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

    b2SensorEvents sensorEvents = b2World_GetSensorEvents(worldId);
    if (sensorEvents.beginCount > 0 || sensorEvents.endCount > 0) {
        entt::dispatcher* dispatcher = registry.ctx().find<entt::dispatcher>();
        if (!dispatcher) {
            dispatcher = &registry.ctx().emplace<entt::dispatcher>();
        }

        for (int i = 0; i < sensorEvents.beginCount; ++i) {
            b2SensorBeginTouchEvent* event = sensorEvents.beginEvents + i;
            b2BodyId bodyA = b2Shape_GetBody(event->sensorShapeId);
            b2BodyId bodyB = b2Shape_GetBody(event->visitorShapeId);
            entt::entity entityA = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyA);
            entt::entity entityB = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyB);
            dispatcher->trigger<PhysicsSensorBeginEvent>(PhysicsSensorBeginEvent{entityA, entityB});
        }

        for (int i = 0; i < sensorEvents.endCount; ++i) {
            b2SensorEndTouchEvent* event = sensorEvents.endEvents + i;
            b2BodyId bodyA = b2Shape_GetBody(event->sensorShapeId);
            b2BodyId bodyB = b2Shape_GetBody(event->visitorShapeId);
            entt::entity entityA = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyA);
            entt::entity entityB = (entt::entity)(uintptr_t)b2Body_GetUserData(bodyB);
            dispatcher->trigger<PhysicsSensorEndEvent>(PhysicsSensorEndEvent{entityA, entityB});
        }
    }

    auto checkJointBreak = [&registry](auto entity, auto& joint) {
        if (joint.BreakForce > 0.0f && b2Joint_IsValid(GetB2Joint(joint.RuntimeJoint))) {
            b2Vec2 force = b2Joint_GetConstraintForce(GetB2Joint(joint.RuntimeJoint));
            float magnitude = b2Length(force);
            if (magnitude > joint.BreakForce) {
                b2DestroyJoint(GetB2Joint(joint.RuntimeJoint));
                joint.RuntimeJoint = NullPhysicsHandle;

                entt::dispatcher* dispatcher = registry.ctx().find<entt::dispatcher>();
                if (dispatcher) {
                    dispatcher->trigger<PhysicsJointBreakEvent>(
                        PhysicsJointBreakEvent{entity, joint.TargetEntity, magnitude});
                }
                return true;
            }
        }
        return false;
    };

    auto distanceJointView = registry.view<DistanceJointComponent>();
    for (auto entity : distanceJointView) {
        auto& joint = distanceJointView.get<DistanceJointComponent>(entity);
        if (checkJointBreak(entity, joint)) {
            registry.remove<DistanceJointComponent>(entity);
        }
    }

    auto revoluteJointView = registry.view<RevoluteJointComponent>();
    for (auto entity : revoluteJointView) {
        auto& joint = revoluteJointView.get<RevoluteJointComponent>(entity);
        if (checkJointBreak(entity, joint)) {
            registry.remove<RevoluteJointComponent>(entity);
        }
    }
}

void PhysicsSystem::SetMaterial(entt::registry& registry, entt::entity entity,
                                std::shared_ptr<PhysicsMaterial> material) {
    if (registry.all_of<BoxColliderComponent>(entity)) {
        auto& box = registry.get<BoxColliderComponent>(entity);
        box.Material = material;
        if (b2Shape_IsValid(GetB2Shape(box.RuntimeShape))) {
            b2Shape_SetDensity(GetB2Shape(box.RuntimeShape), material->Density);
            b2Shape_SetFriction(GetB2Shape(box.RuntimeShape), material->Friction);
            b2Shape_SetRestitution(GetB2Shape(box.RuntimeShape), material->Restitution);
        }
    }
    if (registry.all_of<CircleColliderComponent>(entity)) {
        auto& circle = registry.get<CircleColliderComponent>(entity);
        circle.Material = material;
        if (b2Shape_IsValid(GetB2Shape(circle.RuntimeShape))) {
            b2Shape_SetDensity(GetB2Shape(circle.RuntimeShape), material->Density);
            b2Shape_SetFriction(GetB2Shape(circle.RuntimeShape), material->Friction);
            b2Shape_SetRestitution(GetB2Shape(circle.RuntimeShape), material->Restitution);
        }
    }
}

float PhysicsSystem::GetJointForce(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<DistanceJointComponent>(entity)) {
        auto& joint = registry.get<DistanceJointComponent>(entity);
        if (b2Joint_IsValid(GetB2Joint(joint.RuntimeJoint))) {
            b2Vec2 force = b2Joint_GetConstraintForce(GetB2Joint(joint.RuntimeJoint));
            return b2Length(force);
        }
    }
    if (registry.all_of<RevoluteJointComponent>(entity)) {
        auto& joint = registry.get<RevoluteJointComponent>(entity);
        if (b2Joint_IsValid(GetB2Joint(joint.RuntimeJoint))) {
            b2Vec2 force = b2Joint_GetConstraintForce(GetB2Joint(joint.RuntimeJoint));
            return b2Length(force);
        }
    }
    return 0.0f;
}

void PhysicsSystem::DestroyJoint(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<DistanceJointComponent>(entity)) {
        auto& joint = registry.get<DistanceJointComponent>(entity);
        if (b2Joint_IsValid(GetB2Joint(joint.RuntimeJoint))) {
            b2DestroyJoint(GetB2Joint(joint.RuntimeJoint));
        }
        registry.remove<DistanceJointComponent>(entity);
    }
    if (registry.all_of<RevoluteJointComponent>(entity)) {
        auto& joint = registry.get<RevoluteJointComponent>(entity);
        if (b2Joint_IsValid(GetB2Joint(joint.RuntimeJoint))) {
            b2DestroyJoint(GetB2Joint(joint.RuntimeJoint));
        }
        registry.remove<RevoluteJointComponent>(entity);
    }
}

} // namespace BRITE
