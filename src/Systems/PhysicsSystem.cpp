#include "PhysicsSystem.hpp"
#include "Box2DBackend.hpp"
#include "Box3DBackend.hpp"

namespace BRITE {

PhysicsSystem::PhysicsSystem() {}

PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::Init(entt::registry& registry, brite::framework::PhysicsEngineType backendType) {
    if (backendType == brite::framework::PhysicsEngineType::Box2D) {
        m_backend = std::make_unique<Box2DBackend>();
    } else {
        m_backend = std::make_unique<Box3DBackend>();
    }

    if (m_backend) {
        m_backend->Init(registry);
    }
}

void PhysicsSystem::PreStep(entt::registry& registry) {
    if (m_backend) {
        m_backend->PreStep(registry);
    }
}

void PhysicsSystem::Step(entt::registry& registry, float dt) {
    if (m_backend) {
        m_backend->Step(registry, dt);
    }
}

void PhysicsSystem::SetMaterial(entt::registry& registry, entt::entity entity,
                                std::shared_ptr<PhysicsMaterial> material) {
    if (m_backend) {
        m_backend->SetMaterial(registry, entity, material);
    }
}

float PhysicsSystem::GetJointForce(entt::registry& registry, entt::entity entity) {
    if (m_backend) {
        return m_backend->GetJointForce(registry, entity);
    }
    return 0.0f;
}

void PhysicsSystem::DestroyJoint(entt::registry& registry, entt::entity entity) {
    if (m_backend) {
        m_backend->DestroyJoint(registry, entity);
    }
}

} // namespace BRITE
