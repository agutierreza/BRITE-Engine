// AoISystem: which entities are inside an area of interest, as the InAoITag they carry, and the Box2D body of one that
// leaves it, which is switched off until it comes back. An entity is inside when it is within the area's half width and
// half height of its centre -- on the edge counts -- and in a world that wraps, by the shorter way round.
//
// The tag cases need no physics. The body cases make their bodies with a Box2DBackend's PreStep, as a scene's would be.

#include <ECS/Components.hpp>
#include <Systems/AoISystem.hpp>
#include <Systems/Box2DBackend.hpp>
#include <gtest/gtest.h>

namespace {

using BRITE::AoIComponent;
using BRITE::AoIConfig;
using BRITE::AoISystem;
using BRITE::InAoITag;

class AoISystemTest : public ::testing::Test {
  protected:
    // An entity at (x, y) that the area watches.
    entt::entity AddWatched(float x, float y) {
        const entt::entity entity = m_registry.create();
        m_registry.emplace<BRITE::TransformComponent>(entity, BRITE::Vector2{x, y});
        m_registry.emplace<AoIComponent>(entity);
        return entity;
    }
    void Move(entt::entity entity, float x, float y) {
        m_registry.get<BRITE::TransformComponent>(entity).Position = {x, y, 0.0f};
    }
    bool IsInside(entt::entity entity) {
        return m_registry.all_of<InAoITag>(entity);
    }
    bool InsideAfterUpdate(float x, float y, const AoIConfig& config) {
        const entt::entity entity = AddWatched(x, y);
        AoISystem::Update(m_registry, config);
        return IsInside(entity);
    }

    // An area 200 by 100 about the origin.
    static AoIConfig Area() {
        AoIConfig config;
        config.HalfWidth = 100.0f;
        config.HalfHeight = 50.0f;
        return config;
    }

    entt::registry m_registry;
};

} // namespace

// ---------------------------------------------------------------------------
// The tag
// ---------------------------------------------------------------------------

TEST_F(AoISystemTest, AnEntityInsideTheAreaIsTaggedAndOneOutsideIsNot) {
    EXPECT_TRUE(InsideAfterUpdate(0.0f, 0.0f, Area()));
    EXPECT_TRUE(InsideAfterUpdate(99.0f, -49.0f, Area()));
    EXPECT_FALSE(InsideAfterUpdate(101.0f, 0.0f, Area()));
    EXPECT_FALSE(InsideAfterUpdate(0.0f, 51.0f, Area()));
    EXPECT_FALSE(InsideAfterUpdate(-500.0f, -500.0f, Area()));
}

TEST_F(AoISystemTest, TheEdgeOfTheAreaIsInsideIt) {
    EXPECT_TRUE(InsideAfterUpdate(100.0f, 0.0f, Area()));
    EXPECT_TRUE(InsideAfterUpdate(-100.0f, 0.0f, Area()));
    EXPECT_TRUE(InsideAfterUpdate(0.0f, 50.0f, Area()));
    EXPECT_TRUE(InsideAfterUpdate(0.0f, -50.0f, Area()));
    EXPECT_TRUE(InsideAfterUpdate(100.0f, 50.0f, Area())) << "and so is a corner";
}

TEST_F(AoISystemTest, TheWidthAndTheHeightAreSeparate) {
    AoIConfig wide = Area();
    wide.HalfWidth = 1000.0f;
    wide.HalfHeight = 10.0f;

    EXPECT_TRUE(InsideAfterUpdate(900.0f, 5.0f, wide));
    EXPECT_FALSE(InsideAfterUpdate(900.0f, 15.0f, wide));
}

TEST_F(AoISystemTest, TheAreaIsAboutItsCentre) {
    AoIConfig config = Area();
    config.Center = {1000.0f, -2000.0f};

    EXPECT_TRUE(InsideAfterUpdate(1090.0f, -2040.0f, config));
    EXPECT_FALSE(InsideAfterUpdate(0.0f, 0.0f, config)) << "the origin is no longer in it";
}

TEST_F(AoISystemTest, AnEntityThatLeavesLosesItsTagAndOneThatReturnsGetsItBack) {
    const entt::entity entity = AddWatched(0.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    ASSERT_TRUE(IsInside(entity));

    Move(entity, 500.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    EXPECT_FALSE(IsInside(entity));

    Move(entity, 10.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    EXPECT_TRUE(IsInside(entity));
}

TEST_F(AoISystemTest, UpdatingAgainWithNothingMovedChangesNothing) {
    const entt::entity inside = AddWatched(0.0f, 0.0f);
    const entt::entity outside = AddWatched(500.0f, 0.0f);

    AoISystem::Update(m_registry, Area());
    AoISystem::Update(m_registry, Area());

    EXPECT_TRUE(IsInside(inside));
    EXPECT_FALSE(IsInside(outside));
}

TEST_F(AoISystemTest, AnEntityWhoseAoIIsNotActiveIsLeftAsItWas) {
    const entt::entity entity = AddWatched(0.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    ASSERT_TRUE(IsInside(entity));

    m_registry.get<AoIComponent>(entity).Active = false;
    Move(entity, 500.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    EXPECT_TRUE(IsInside(entity)) << "its tag stays, wherever it has gone";

    const entt::entity never = AddWatched(0.0f, 0.0f);
    m_registry.get<AoIComponent>(never).Active = false;
    AoISystem::Update(m_registry, Area());
    EXPECT_FALSE(IsInside(never)) << "and one never tagged is not tagged now";
}

TEST_F(AoISystemTest, AnEntityNotWatchedIsNeverTagged) {
    const entt::entity entity = m_registry.create();
    m_registry.emplace<BRITE::TransformComponent>(entity, BRITE::Vector2{0.0f, 0.0f});
    const entt::entity noTransform = m_registry.create();
    m_registry.emplace<AoIComponent>(noTransform);

    AoISystem::Update(m_registry, Area());

    EXPECT_FALSE(IsInside(entity));
    EXPECT_FALSE(IsInside(noTransform));
}

TEST_F(AoISystemTest, InAWorldThatWrapsAnEntityIsInsideByTheShorterWayRound) {
    AoIConfig config = Area();
    config.WorldWidth = 1000.0f;
    config.WorldHeight = 500.0f;

    EXPECT_TRUE(InsideAfterUpdate(950.0f, 0.0f, config)) << "50 to the left of the centre, round the back";
    EXPECT_TRUE(InsideAfterUpdate(-950.0f, 0.0f, config)) << "and 50 to the right of it, the other way";
    EXPECT_TRUE(InsideAfterUpdate(0.0f, 480.0f, config));
    EXPECT_TRUE(InsideAfterUpdate(0.0f, -480.0f, config));
    EXPECT_FALSE(InsideAfterUpdate(500.0f, 0.0f, config)) << "half the world away is as far as it gets";
    EXPECT_FALSE(InsideAfterUpdate(0.0f, 250.0f, config));
}

TEST_F(AoISystemTest, InAWorldThatDoesNotWrapTheLongWayRoundIsFar) {
    AoIConfig config = Area();
    config.WorldWidth = 0.0f;
    config.WorldHeight = 0.0f;

    EXPECT_FALSE(InsideAfterUpdate(950.0f, 0.0f, config));
    EXPECT_FALSE(InsideAfterUpdate(0.0f, 480.0f, config));
}

TEST_F(AoISystemTest, AWorldThatWrapsInOneDirectionOnlyWrapsInThatOne) {
    AoIConfig config = Area();
    config.WorldWidth = 1000.0f;

    EXPECT_TRUE(InsideAfterUpdate(950.0f, 0.0f, config));
    EXPECT_FALSE(InsideAfterUpdate(0.0f, 480.0f, config));
}

TEST_F(AoISystemTest, TheWrapIsAboutTheCentreOfTheArea) {
    AoIConfig config = Area();
    config.Center = {400.0f, 0.0f};
    config.WorldWidth = 1000.0f;

    EXPECT_TRUE(InsideAfterUpdate(-550.0f, 0.0f, config)) << "950 to the left of 400, which is 50 round the back";
    EXPECT_FALSE(InsideAfterUpdate(-100.0f, 0.0f, config));
}

// ---------------------------------------------------------------------------
// The body
// ---------------------------------------------------------------------------

namespace {

class AoIBodyTest : public AoISystemTest {
  protected:
    static constexpr float TICK = 1.0f / 60.0f;

    entt::entity AddWatchedBody(float x, float y) {
        const entt::entity entity = AddWatched(x, y);
        m_registry.emplace<BRITE::RigidBodyComponent>(entity);
        auto& box = m_registry.emplace<BRITE::BoxColliderComponent>(entity);
        box.Size = {16.0f, 16.0f};
        return entity;
    }
    void Tick() {
        m_backend.PreStep(m_registry);
        m_backend.Step(m_registry, TICK);
    }
    b2BodyId Body(entt::entity entity) {
        return BRITE::Box2DBackend::GetB2Body(m_registry.get<BRITE::RigidBodyComponent>(entity).RuntimeBody);
    }

    BRITE::Box2DBackend m_backend;
};

} // namespace

TEST_F(AoIBodyTest, ABodyThatLeavesTheAreaIsSwitchedOffAndOneThatReturnsIsSwitchedOn) {
    const entt::entity entity = AddWatchedBody(0.0f, 0.0f);
    Tick();
    AoISystem::Update(m_registry, Area());
    ASSERT_TRUE(b2Body_IsEnabled(Body(entity)));

    Move(entity, 500.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    EXPECT_FALSE(b2Body_IsEnabled(Body(entity)));

    Move(entity, 0.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    EXPECT_TRUE(b2Body_IsEnabled(Body(entity)));
}

TEST_F(AoIBodyTest, ABodySwitchedOffByLeavingTheAreaDoesNotFallWhileOneInsideDoes) {
    const entt::entity staying = AddWatchedBody(0.0f, 0.0f);
    const entt::entity leaving = AddWatchedBody(50.0f, 0.0f);
    Tick();
    AoISystem::Update(m_registry, Area());
    // Moved just before the update that reads it: a tick would put the transform back where physics has the body.
    Move(leaving, 500.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    Tick(); // the transform is the body's again, and the body is off
    const float y = m_registry.get<BRITE::TransformComponent>(leaving).Position.y;

    for (int i = 0; i < 60; ++i)
        Tick();

    EXPECT_NEAR(m_registry.get<BRITE::TransformComponent>(leaving).Position.y, y, 1e-3f) << "switched off, it stays";
    EXPECT_LT(m_registry.get<BRITE::TransformComponent>(staying).Position.y, -100.0f) << "while the other falls";
}

TEST_F(AoIBodyTest, AnEntityWithABodyNotYetMadeIsTaggedAllTheSame) {
    const entt::entity entity = AddWatchedBody(0.0f, 0.0f);

    AoISystem::Update(m_registry, Area());
    EXPECT_TRUE(IsInside(entity));
    Move(entity, 500.0f, 0.0f);
    AoISystem::Update(m_registry, Area());
    EXPECT_FALSE(IsInside(entity));

    Tick();
    EXPECT_TRUE(b2Body_IsValid(Body(entity)));
}

TEST_F(AoIBodyTest, ABodyThatEntersTheAreaIsSwitchedOnWhateverStateItWasIn) {
    const entt::entity entity = AddWatchedBody(0.0f, 0.0f);
    Tick();
    b2Body_Disable(Body(entity));
    ASSERT_FALSE(b2Body_IsEnabled(Body(entity)));

    AoISystem::Update(m_registry, Area());

    EXPECT_TRUE(IsInside(entity));
    EXPECT_TRUE(b2Body_IsEnabled(Body(entity)));
}
