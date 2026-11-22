// PhysicsSystem on its own: what it passes to the backend it was given, and what it does with none. A backend that
// only records what it is asked stands in for Box2D and Box3D, whose own cases are in their own files.

#include <ECS/PhysicsMaterial.hpp>
#include <Systems/IPhysicsBackend.hpp>
#include <Systems/PhysicsSystem.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace {

struct Calls {
    int materials = 0;
    int destroyed = 0;
    entt::entity materialOn{entt::null};
    entt::entity destroyedOn{entt::null};
    std::shared_ptr<BRITE::PhysicsMaterial> material;
};

class RecordingPhysics : public BRITE::IPhysicsBackend {
  public:
    RecordingPhysics(Calls& calls, float jointForce) : m_calls(calls), m_jointForce(jointForce) {}

    void Init(entt::registry&) override {}
    void PreStep(entt::registry&) override {}
    void Step(entt::registry&, float) override {}
    void SetMaterial(entt::registry&, entt::entity entity, std::shared_ptr<BRITE::PhysicsMaterial> material) override {
        ++m_calls.materials;
        m_calls.materialOn = entity;
        m_calls.material = std::move(material);
    }
    float GetJointForce(entt::registry&, entt::entity) override {
        return m_jointForce;
    }
    void DestroyJoint(entt::registry&, entt::entity entity) override {
        ++m_calls.destroyed;
        m_calls.destroyedOn = entity;
    }

  private:
    Calls& m_calls;
    float m_jointForce;
};

} // namespace

TEST(PhysicsSystem, AMaterialSetIsTheBackendsToSetOnTheEntityItWasSetOn) {
    entt::registry registry;
    Calls calls;
    BRITE::PhysicsSystem physics;
    physics.Init(registry, std::make_unique<RecordingPhysics>(calls, 0.0f));

    const entt::entity entity = registry.create();
    const auto material = std::make_shared<BRITE::PhysicsMaterial>(BRITE::PhysicsMaterial{2.0f, 0.5f, 0.25f});
    physics.SetMaterial(registry, entity, material);

    EXPECT_EQ(calls.materials, 1);
    EXPECT_EQ(calls.materialOn, entity);
    EXPECT_EQ(calls.material, material) << "the very material, not a copy";
}

TEST(PhysicsSystem, AJointForceIsWhateverTheBackendReads) {
    entt::registry registry;
    Calls calls;
    BRITE::PhysicsSystem physics;
    physics.Init(registry, std::make_unique<RecordingPhysics>(calls, 4.5f));

    EXPECT_EQ(physics.GetJointForce(registry, registry.create()), 4.5f);
}

TEST(PhysicsSystem, ADestroyedJointIsTheBackendsToDestroyOnTheEntityItWasAskedOf) {
    entt::registry registry;
    Calls calls;
    BRITE::PhysicsSystem physics;
    physics.Init(registry, std::make_unique<RecordingPhysics>(calls, 0.0f));

    const entt::entity entity = registry.create();
    physics.DestroyJoint(registry, entity);

    EXPECT_EQ(calls.destroyed, 1);
    EXPECT_EQ(calls.destroyedOn, entity);
}

TEST(PhysicsSystem, WithNoBackendEveryCallDoesNothingAndAJointForceReadsZero) {
    entt::registry registry;
    BRITE::PhysicsSystem physics;
    const entt::entity entity = registry.create();

    EXPECT_EQ(physics.GetBackend(), nullptr);
    physics.PreStep(registry);
    physics.Step(registry, 1.0f / 60.0f);
    physics.SetMaterial(registry, entity, std::make_shared<BRITE::PhysicsMaterial>());
    physics.DestroyJoint(registry, entity);
    EXPECT_EQ(physics.GetJointForce(registry, entity), 0.0f);
}

TEST(PhysicsSystem, ABackendGivenIsTheOneItKeepsAndOneGivenAfterReplacesIt) {
    entt::registry registry;
    Calls first;
    Calls second;
    BRITE::PhysicsSystem physics;

    physics.Init(registry, std::make_unique<RecordingPhysics>(first, 1.0f));
    BRITE::IPhysicsBackend* firstBackend = physics.GetBackend();
    ASSERT_NE(firstBackend, nullptr);
    EXPECT_EQ(physics.GetJointForce(registry, registry.create()), 1.0f);

    physics.Init(registry, std::make_unique<RecordingPhysics>(second, 2.0f));
    EXPECT_EQ(physics.GetJointForce(registry, registry.create()), 2.0f) << "the later backend answers";
    physics.DestroyJoint(registry, registry.create());
    EXPECT_EQ(first.destroyed, 0);
    EXPECT_EQ(second.destroyed, 1);

    physics.Init(registry, nullptr);
    EXPECT_EQ(physics.GetBackend(), nullptr) << "and none given takes the backend away";
    EXPECT_EQ(physics.GetJointForce(registry, registry.create()), 0.0f);
}
