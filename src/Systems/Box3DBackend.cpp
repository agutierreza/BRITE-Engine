#include "Box3DBackend.hpp"
#include <ECS/Components.hpp>
#include <ECS/Events.hpp>
#include <box3d/collision.h>
#include <box3d/math_functions.h>
#include <raymath.h>
#include <tracy/Tracy.hpp>

namespace BRITE {

constexpr float PPM = 30.0f;

Box3DBackend::Box3DBackend() {
    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity.x = 0.0f;
    worldDef.gravity.y = -10.0f;
    worldDef.gravity.z = 0.0f;
    m_worldId = b3CreateWorld(&worldDef);
}

Box3DBackend::~Box3DBackend() {
    if (b3World_IsValid(m_worldId)) {
        b3DestroyWorld(m_worldId);
    }
}

void Box3DBackend::Init(entt::registry& registry) {
    // Nothing needed here for now
}

void Box3DBackend::PreStep(entt::registry& registry) {
    ZoneScoped;

    // PRE-STEP
    {
        auto view = registry.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rb = view.get<RigidBodyComponent>(entity);

            if (!b3Body_IsValid(GetB3Body(rb.RuntimeBody))) {
                // Create the body
                b3BodyDef bodyDef = b3DefaultBodyDef();

                switch (rb.Type) {
                case RigidBodyComponent::BodyType::Static:
                    bodyDef.type = b3_staticBody;
                    break;
                case RigidBodyComponent::BodyType::Kinematic:
                    bodyDef.type = b3_kinematicBody;
                    break;
                case RigidBodyComponent::BodyType::Dynamic:
                    bodyDef.type = b3_dynamicBody;
                    break;
                }

                bodyDef.position = {transform.Position.x / PPM, transform.Position.y / PPM, transform.Position.z / PPM};
                bodyDef.rotation.v.x = transform.Rotation.x;
                bodyDef.rotation.v.y = transform.Rotation.y;
                bodyDef.rotation.v.z = transform.Rotation.z;
                bodyDef.rotation.s = transform.Rotation.w;

                bodyDef.motionLocks.angularX = rb.FixedRotation;
                bodyDef.motionLocks.angularY = rb.FixedRotation;
                bodyDef.motionLocks.angularZ = rb.FixedRotation;

                bodyDef.gravityScale = rb.GravityScale;
                // Store entity in user data for contact listeners later
                bodyDef.userData = (void*)(uintptr_t)entity;

                rb.RuntimeBody = ToHandle(b3CreateBody(m_worldId, &bodyDef));

                // Check for BoxCollider3D
                if (registry.all_of<BoxCollider3DComponent>(entity)) {
                    auto& box = registry.get<BoxCollider3DComponent>(entity);
                    b3ShapeDef shapeDef = b3DefaultShapeDef();
                    shapeDef.density = box.Material->Density;
                    shapeDef.baseMaterial.friction = box.Material->Friction;
                    shapeDef.baseMaterial.restitution = box.Material->Restitution;
                    shapeDef.isSensor = box.IsSensor;

                    b3Vec3 offset = {box.Offset.x / PPM, box.Offset.y / PPM, box.Offset.z / PPM};
                    b3BoxHull hull = b3MakeOffsetBoxHull(box.Size.x / 2.0f / PPM, box.Size.y / 2.0f / PPM,
                                                         box.Size.z / 2.0f / PPM, offset);
                    box.RuntimeShape = ToHandle(b3CreateHullShape(GetB3Body(rb.RuntimeBody), &shapeDef, &hull.base));
                }

                // Check for SphereCollider
                if (registry.all_of<SphereColliderComponent>(entity)) {
                    auto& sphereC = registry.get<SphereColliderComponent>(entity);
                    b3ShapeDef shapeDef = b3DefaultShapeDef();
                    shapeDef.density = sphereC.Material->Density;
                    shapeDef.baseMaterial.friction = sphereC.Material->Friction;
                    shapeDef.baseMaterial.restitution = sphereC.Material->Restitution;
                    shapeDef.isSensor = sphereC.IsSensor;

                    b3Sphere sphere = {{sphereC.Offset.x / PPM, sphereC.Offset.y / PPM, sphereC.Offset.z / PPM},
                                       sphereC.Radius / PPM};
                    sphereC.RuntimeShape = ToHandle(b3CreateSphereShape(GetB3Body(rb.RuntimeBody), &shapeDef, &sphere));
                }
            } else if (rb.Type == RigidBodyComponent::BodyType::Kinematic) {
                // If kinematic, sync Transform -> Box3D before step
                b3Pos pos = {transform.Position.x / PPM, transform.Position.y / PPM, transform.Position.z / PPM};
                b3Quat rot = {{transform.Rotation.x, transform.Rotation.y, transform.Rotation.z}, transform.Rotation.w};
                b3Body_SetTransform(GetB3Body(rb.RuntimeBody), pos, rot);
            }

            // Check for DistanceJoint3DComponent
            if (registry.all_of<DistanceJoint3DComponent>(entity)) {
                auto& joint = registry.get<DistanceJoint3DComponent>(entity);
                if (!b3Joint_IsValid(GetB3Joint(joint.RuntimeJoint)) && registry.valid(joint.TargetEntity)) {
                    if (registry.all_of<RigidBodyComponent>(joint.TargetEntity)) {
                        auto& targetRb = registry.get<RigidBodyComponent>(joint.TargetEntity);
                        if (b3Body_IsValid(GetB3Body(targetRb.RuntimeBody))) {
                            b3DistanceJointDef jointDef = b3DefaultDistanceJointDef();
                            jointDef.base.bodyIdA = GetB3Body(rb.RuntimeBody);
                            jointDef.base.bodyIdB = GetB3Body(targetRb.RuntimeBody);
                            jointDef.base.localFrameA = b3Transform_identity;
                            jointDef.base.localFrameB = b3Transform_identity;
                            jointDef.length = joint.Length / PPM;
                            jointDef.base.collideConnected = joint.CollideConnected;
                            if (joint.EnableSpring) {
                                jointDef.enableSpring = true;
                                jointDef.hertz = joint.Hertz;
                                // Box3D might not have dampingRatio directly in b3DistanceJointDef? Let's check... if
                                // it fails, I'll fix it. I think it does. Wait, let me just assume it might have
                                // dampingRatio or skip it. It usually has `lowerSpringForce` and `upperSpringForce`
                                // instead of dampingRatio for distance joints in v3? Wait, earlier I saw
                                // `lowerSpringForce`, `upperSpringForce`, `hertz` but no dampingRatio? Actually let me
                                // just check the output from before. output was `lowerSpringForce`, `upperSpringForce`,
                                // `hertz`. No `dampingRatio`. Let me skip damping ratio.
                            }
                            joint.RuntimeJoint = ToHandle(b3CreateDistanceJoint(m_worldId, &jointDef));
                        }
                    }
                }
            }

            // Check for RevoluteJoint3DComponent
            if (registry.all_of<RevoluteJoint3DComponent>(entity)) {
                auto& joint = registry.get<RevoluteJoint3DComponent>(entity);
                if (!b3Joint_IsValid(GetB3Joint(joint.RuntimeJoint)) && registry.valid(joint.TargetEntity)) {
                    if (registry.all_of<RigidBodyComponent>(joint.TargetEntity)) {
                        auto& targetRb = registry.get<RigidBodyComponent>(joint.TargetEntity);
                        if (b3Body_IsValid(GetB3Body(targetRb.RuntimeBody))) {
                            b3RevoluteJointDef jointDef = b3DefaultRevoluteJointDef();
                            jointDef.base.bodyIdA = GetB3Body(rb.RuntimeBody);
                            jointDef.base.bodyIdB = GetB3Body(targetRb.RuntimeBody);
                            jointDef.base.localFrameA = b3Transform_identity;
                            jointDef.base.localFrameA.p = {joint.LocalAnchorA.x / PPM, joint.LocalAnchorA.y / PPM,
                                                           joint.LocalAnchorA.z / PPM};
                            jointDef.base.localFrameB = b3Transform_identity;
                            jointDef.base.localFrameB.p = {joint.LocalAnchorB.x / PPM, joint.LocalAnchorB.y / PPM,
                                                           joint.LocalAnchorB.z / PPM};

                            jointDef.targetAngle = joint.ReferenceAngle * (PI / 180.0f);
                            jointDef.enableLimit = joint.EnableLimit;
                            jointDef.lowerAngle = joint.LowerAngle * (PI / 180.0f);
                            jointDef.upperAngle = joint.UpperAngle * (PI / 180.0f);
                            jointDef.base.collideConnected = joint.CollideConnected;
                            joint.RuntimeJoint = ToHandle(b3CreateRevoluteJoint(m_worldId, &jointDef));
                        }
                    }
                }
            }
        }
    }
}

void Box3DBackend::Step(entt::registry& registry, float dt) {
    ZoneScoped;

    // STEP
    b3World_Step(m_worldId, dt, 4);

    // POST-STEP
    {
        auto view = registry.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rb = view.get<RigidBodyComponent>(entity);

            if (b3Body_IsValid(GetB3Body(rb.RuntimeBody)) && rb.Type != RigidBodyComponent::BodyType::Static) {
                b3Pos position = b3Body_GetPosition(GetB3Body(rb.RuntimeBody));
                b3Quat rotation = b3Body_GetRotation(GetB3Body(rb.RuntimeBody));

                transform.Position.x = position.x * PPM;
                transform.Position.y = position.y * PPM;
                transform.Position.z = position.z * PPM;
                transform.Rotation.x = rotation.v.x;
                transform.Rotation.y = rotation.v.y;
                transform.Rotation.z = rotation.v.z;
                transform.Rotation.w = rotation.s;
            }
        }

        // Contact events can be implemented here similar to Box2D if required
        // For simplicity, we skip full event mapping for the initial 3D backend implementation

        auto checkJointBreak3D = [this, &registry](auto entity, auto& joint) {
            if (joint.BreakForce > 0.0f && b3Joint_IsValid(GetB3Joint(joint.RuntimeJoint))) {
                b3Vec3 force = b3Joint_GetConstraintForce(GetB3Joint(joint.RuntimeJoint));
                float magnitude = b3Length(force);
                if (magnitude > joint.BreakForce) {
                    b3DestroyJoint(GetB3Joint(joint.RuntimeJoint), true);
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

        auto distanceJointView = registry.view<DistanceJoint3DComponent>();
        for (auto entity : distanceJointView) {
            auto& joint = distanceJointView.get<DistanceJoint3DComponent>(entity);
            if (checkJointBreak3D(entity, joint)) {
                registry.remove<DistanceJoint3DComponent>(entity);
            }
        }

        auto revoluteJointView = registry.view<RevoluteJoint3DComponent>();
        for (auto entity : revoluteJointView) {
            auto& joint = revoluteJointView.get<RevoluteJoint3DComponent>(entity);
            if (checkJointBreak3D(entity, joint)) {
                registry.remove<RevoluteJoint3DComponent>(entity);
            }
        }
    }
}

void Box3DBackend::SetMaterial(entt::registry& registry, entt::entity entity,
                               std::shared_ptr<PhysicsMaterial> material) {
    if (registry.all_of<BoxCollider3DComponent>(entity)) {
        auto& box = registry.get<BoxCollider3DComponent>(entity);
        box.Material = material;
        if (b3Shape_IsValid(GetB3Shape(box.RuntimeShape))) {
            b3Shape_SetDensity(GetB3Shape(box.RuntimeShape), material->Density, true);
            b3Shape_SetFriction(GetB3Shape(box.RuntimeShape), material->Friction);
            b3Shape_SetRestitution(GetB3Shape(box.RuntimeShape), material->Restitution);
        }
    }
    if (registry.all_of<SphereColliderComponent>(entity)) {
        auto& sphereC = registry.get<SphereColliderComponent>(entity);
        sphereC.Material = material;
        if (b3Shape_IsValid(GetB3Shape(sphereC.RuntimeShape))) {
            b3Shape_SetDensity(GetB3Shape(sphereC.RuntimeShape), material->Density, true);
            b3Shape_SetFriction(GetB3Shape(sphereC.RuntimeShape), material->Friction);
            b3Shape_SetRestitution(GetB3Shape(sphereC.RuntimeShape), material->Restitution);
        }
    }
}

float Box3DBackend::GetJointForce(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<DistanceJoint3DComponent>(entity)) {
        auto& joint = registry.get<DistanceJoint3DComponent>(entity);
        if (b3Joint_IsValid(GetB3Joint(joint.RuntimeJoint))) {
            b3Vec3 force = b3Joint_GetConstraintForce(GetB3Joint(joint.RuntimeJoint));
            return b3Length(force);
        }
    }
    if (registry.all_of<RevoluteJoint3DComponent>(entity)) {
        auto& joint = registry.get<RevoluteJoint3DComponent>(entity);
        if (b3Joint_IsValid(GetB3Joint(joint.RuntimeJoint))) {
            b3Vec3 force = b3Joint_GetConstraintForce(GetB3Joint(joint.RuntimeJoint));
            return b3Length(force);
        }
    }
    return 0.0f;
}

void Box3DBackend::DestroyJoint(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<DistanceJoint3DComponent>(entity)) {
        auto& joint = registry.get<DistanceJoint3DComponent>(entity);
        if (b3Joint_IsValid(GetB3Joint(joint.RuntimeJoint))) {
            b3DestroyJoint(GetB3Joint(joint.RuntimeJoint), true);
        }
        registry.remove<DistanceJoint3DComponent>(entity);
    }
    if (registry.all_of<RevoluteJoint3DComponent>(entity)) {
        auto& joint = registry.get<RevoluteJoint3DComponent>(entity);
        if (b3Joint_IsValid(GetB3Joint(joint.RuntimeJoint))) {
            b3DestroyJoint(GetB3Joint(joint.RuntimeJoint), true);
        }
        registry.remove<RevoluteJoint3DComponent>(entity);
    }
}

} // namespace BRITE
