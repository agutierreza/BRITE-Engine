// Box2DBackend on its own: a registry, the backend's PreStep and Step, and nothing else -- no window, application or
// renderer. A world is stepped the way the framework steps it, PreStep then Step, one sixtieth of a second a tick.
//
// Positions are in pixels, 30 to the metre, so the backend's gravity of 10 m/s^2 is 300 px/s^2, downward -- toward
// negative y. A body is a small collider on an entity with a transform, and where it ends up is read from the
// transform, which is where a scene reads it; what the cases reach into Box2D for is what a scene has no way to ask: a
// body's mass, a shape's material, a push to start a body moving.

#include <ECS/Components.hpp>
#include <ECS/Events.hpp>
#include <Systems/Box2DBackend.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

using BRITE::Box2DBackend;
using BRITE::PhysicsMaterial;
using BodyType = BRITE::RigidBodyComponent::BodyType;

constexpr float TICK = 1.0f / 60.0f;
constexpr float PI = 3.14159265358979323846f;
constexpr float GRAVITY = 10.0f; // m/s^2, the backend's own

// Everything the dispatcher says, in the order it was said.
struct EventLog {
    std::vector<BRITE::PhysicsCollisionEvent> begun;
    std::vector<BRITE::PhysicsCollisionEndEvent> ended;
    std::vector<BRITE::PhysicsHitEvent> hit;
    std::vector<BRITE::PhysicsSensorBeginEvent> sensorBegun;
    std::vector<BRITE::PhysicsSensorEndEvent> sensorEnded;
    std::vector<BRITE::PhysicsJointBreakEvent> broken;

    void Connect(entt::dispatcher& dispatcher) {
        dispatcher.sink<BRITE::PhysicsCollisionEvent>().connect<&EventLog::OnBegin>(*this);
        dispatcher.sink<BRITE::PhysicsCollisionEndEvent>().connect<&EventLog::OnEnd>(*this);
        dispatcher.sink<BRITE::PhysicsHitEvent>().connect<&EventLog::OnHit>(*this);
        dispatcher.sink<BRITE::PhysicsSensorBeginEvent>().connect<&EventLog::OnSensorBegin>(*this);
        dispatcher.sink<BRITE::PhysicsSensorEndEvent>().connect<&EventLog::OnSensorEnd>(*this);
        dispatcher.sink<BRITE::PhysicsJointBreakEvent>().connect<&EventLog::OnBreak>(*this);
    }

    void OnBegin(const BRITE::PhysicsCollisionEvent& event) {
        begun.push_back(event);
    }
    void OnEnd(const BRITE::PhysicsCollisionEndEvent& event) {
        ended.push_back(event);
    }
    void OnHit(const BRITE::PhysicsHitEvent& event) {
        hit.push_back(event);
    }
    void OnSensorBegin(const BRITE::PhysicsSensorBeginEvent& event) {
        sensorBegun.push_back(event);
    }
    void OnSensorEnd(const BRITE::PhysicsSensorEndEvent& event) {
        sensorEnded.push_back(event);
    }
    void OnBreak(const BRITE::PhysicsJointBreakEvent& event) {
        broken.push_back(event);
    }
};

// Whether a pair of entities is the two given, whichever way round Box2D named them.
bool IsPair(entt::entity a, entt::entity b, entt::entity x, entt::entity y) {
    return (a == x && b == y) || (a == y && b == x);
}

class Box2DBackendTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_backend.Init(m_registry);
    }

    void Tick() {
        m_backend.PreStep(m_registry);
        m_backend.Step(m_registry, TICK);
    }
    void Run(int ticks) {
        for (int i = 0; i < ticks; ++i)
            Tick();
    }

    // The world's dispatcher, made now and listened to, as a scene that wants the events would.
    void Listen() {
        m_log.Connect(m_registry.ctx().emplace<entt::dispatcher>());
    }

    entt::entity AddBody(BodyType type, float x, float y) {
        const entt::entity entity = m_registry.create();
        m_registry.emplace<BRITE::TransformComponent>(entity, BRITE::Vector2{x, y});
        m_registry.emplace<BRITE::RigidBodyComponent>(entity).Type = type;
        return entity;
    }
    BRITE::BoxColliderComponent& AddBox(entt::entity entity, float width, float height) {
        auto& box = m_registry.emplace<BRITE::BoxColliderComponent>(entity);
        box.Size = {width, height};
        return box;
    }
    BRITE::CircleColliderComponent& AddCircle(entt::entity entity, float radius) {
        auto& circle = m_registry.emplace<BRITE::CircleColliderComponent>(entity);
        circle.Radius = radius;
        return circle;
    }
    // A static floor whose top is at y = 15.
    entt::entity AddGround() {
        const entt::entity ground = AddBody(BodyType::Static, 0.0f, 0.0f);
        AddBox(ground, 1200.0f, 30.0f);
        return ground;
    }
    // A joint from `entity` to `target`, hung on the first, with the target's body to be made by a PreStep.
    BRITE::DistanceJointComponent& AddDistanceJoint(entt::entity entity, entt::entity target, float length) {
        auto& joint = m_registry.emplace<BRITE::DistanceJointComponent>(entity);
        joint.TargetEntity = target;
        joint.Length = length;
        return joint;
    }
    // A pivot at `target`, which `entity` hangs from at the point `anchor` -- in the body's own pixels.
    BRITE::RevoluteJointComponent& AddRevoluteJoint(entt::entity entity, entt::entity target, BRITE::Vector2 anchor) {
        auto& joint = m_registry.emplace<BRITE::RevoluteJointComponent>(entity);
        joint.TargetEntity = target;
        joint.LocalAnchorA = anchor;
        return joint;
    }

    BRITE::TransformComponent& Transform(entt::entity entity) {
        return m_registry.get<BRITE::TransformComponent>(entity);
    }
    BRITE::Vector2 Position(entt::entity entity) {
        const BRITE::Vector3& position = Transform(entity).Position;
        return {position.x, position.y};
    }
    // The turn about z the transform's quaternion says, in radians.
    float Angle(entt::entity entity) {
        const BRITE::Quaternion& rotation = Transform(entity).Rotation;
        return 2.0f * std::atan2(rotation.z, rotation.w);
    }
    float Distance(entt::entity a, entt::entity b) {
        const BRITE::Vector2 pa = Position(a);
        const BRITE::Vector2 pb = Position(b);
        return std::hypot(pa.x - pb.x, pa.y - pb.y);
    }
    b2BodyId Body(entt::entity entity) {
        return Box2DBackend::GetB2Body(m_registry.get<BRITE::RigidBodyComponent>(entity).RuntimeBody);
    }

    // A body let go 20 px past the end of a spring joint of 30 px at 2 Hz, with the given damping: the nearest it
    // comes to the anchor in two seconds. It overshoots the spring's rest length by as much as it is not damped.
    float NearestStretchedSpringComes(float dampingRatio) {
        const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
        const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 250.0f);
        AddCircle(bob, 6.0f);
        BRITE::DistanceJointComponent& joint = AddDistanceJoint(bob, anchor, 30.0f);
        joint.EnableSpring = true;
        joint.Hertz = 2.0f;
        joint.DampingRatio = dampingRatio;
        float nearest = Distance(bob, anchor);
        for (int i = 0; i < 120; ++i) {
            Tick();
            nearest = std::min(nearest, Distance(bob, anchor));
        }
        return nearest;
    }

    // A body let go level with a pivot 30 px to its left, to swing, with the limits of 10 degrees either side
    // switched on or not: the most it turns, in degrees, in three seconds.
    float WidestSwingDegrees(bool limited) {
        const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
        const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
        AddBox(bob, 16.0f, 16.0f);
        BRITE::RevoluteJointComponent& joint = AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f});
        joint.EnableLimit = limited;
        joint.LowerAngle = -10.0f;
        joint.UpperAngle = 10.0f;
        float widest = 0.0f;
        for (int i = 0; i < 180; ++i) {
            Tick();
            widest = std::max(widest, std::abs(Angle(bob)));
        }
        return widest * 180.0f / PI;
    }

    entt::registry m_registry;
    Box2DBackend m_backend;
    EventLog m_log;
};

} // namespace

// ---------------------------------------------------------------------------
// Bodies
// ---------------------------------------------------------------------------

TEST_F(Box2DBackendTest, ADynamicBodyFallsAtGravityAndItsTransformFollows) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f, 16.0f);

    Run(60);

    // A second at 300 px/s^2 from rest is 150 px; stepping in sixtieths, a little over 152.
    EXPECT_NEAR(Position(body).y, 300.0f - 152.5f, 2.0f);
    EXPECT_NEAR(Position(body).x, 0.0f, 1e-3f);
    EXPECT_NEAR(Angle(body), 0.0f, 1e-3f) << "straight down turns nothing";
}

TEST_F(Box2DBackendTest, ABodyIsMadeOnceAndKeepsItsHandle) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f, 16.0f);
    const auto& rb = m_registry.get<BRITE::RigidBodyComponent>(body);
    ASSERT_EQ(rb.RuntimeBody, BRITE::NullPhysicsHandle) << "none before a PreStep";

    Tick();
    const BRITE::PhysicsHandle handle = rb.RuntimeBody;
    ASSERT_NE(handle, BRITE::NullPhysicsHandle);
    EXPECT_TRUE(b2Body_IsValid(Body(body)));

    Run(5);
    EXPECT_EQ(rb.RuntimeBody, handle);
}

TEST_F(Box2DBackendTest, TheBackendsWorldIsLiveAndAnEntitysValidBodyIsTheOneItHolds) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f, 16.0f);
    Tick();

    EXPECT_TRUE(b2World_IsValid(m_backend.GetWorld()));
    const b2BodyId held = BRITE::GetValidBody(m_registry, body);
    EXPECT_TRUE(b2Body_IsValid(held));
    EXPECT_EQ(Box2DBackend::ToHandle(held), m_registry.get<BRITE::RigidBodyComponent>(body).RuntimeBody);
}

TEST_F(Box2DBackendTest, AStaticBodyStaysWhereItIsAndItsTransformIsNeverWritten) {
    const entt::entity body = AddBody(BodyType::Static, 12.0f, -7.0f);
    AddBox(body, 16.0f, 16.0f);
    const BRITE::TransformComponent before = Transform(body);

    Run(30);

    EXPECT_EQ(Transform(body).Position.x, before.Position.x);
    EXPECT_EQ(Transform(body).Position.y, before.Position.y);
    EXPECT_EQ(Transform(body).Rotation.z, before.Rotation.z);
    EXPECT_EQ(Transform(body).Rotation.w, before.Rotation.w);
}

TEST_F(Box2DBackendTest, ADynamicBoxComesToRestOnAStaticOne) {
    AddGround();
    const entt::entity box = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddBox(box, 30.0f, 30.0f);

    Run(180);

    EXPECT_NEAR(Position(box).y, 15.0f + 15.0f, 1.5f) << "the ground's top, and half its own height";
    EXPECT_NEAR(Position(box).x, 0.0f, 0.5f);
    EXPECT_NEAR(Angle(box), 0.0f, 0.02f);
    const b2Vec2 velocity = b2Body_GetLinearVelocity(Body(box));
    EXPECT_NEAR(velocity.y, 0.0f, 0.05f) << "and it has stopped";
}

TEST_F(Box2DBackendTest, ADynamicCircleComesToRestOnAStaticBox) {
    AddGround();
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddCircle(ball, 16.0f);

    Run(180);

    EXPECT_NEAR(Position(ball).y, 15.0f + 16.0f, 1.5f) << "the ground's top, and its radius";
}

TEST_F(Box2DBackendTest, ABoxCollidersOffsetSetsWhereTheShapeSitsOnTheBody) {
    AddGround();
    const entt::entity box = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddBox(box, 30.0f, 30.0f).Offset = {0.0f, 20.0f};

    Run(180);

    // The shape rests on the ground 20 px above the body's origin, so the origin is 20 px lower than it would be.
    EXPECT_NEAR(Position(box).y, 15.0f + 15.0f - 20.0f, 1.5f);
}

TEST_F(Box2DBackendTest, ACircleCollidersOffsetSetsWhereTheShapeSitsOnTheBody) {
    AddGround();
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddCircle(ball, 16.0f).Offset = {0.0f, 20.0f};

    Run(180);

    EXPECT_NEAR(Position(ball).y, 15.0f + 16.0f - 20.0f, 1.5f);
}

TEST_F(Box2DBackendTest, AKinematicBodyIsWhereItsTransformPutsItAndGravityLeavesItAlone) {
    const entt::entity body = AddBody(BodyType::Kinematic, 0.0f, 0.0f);
    AddBox(body, 16.0f, 16.0f);
    Tick();

    Transform(body).Position = {90.0f, 45.0f, 0.0f};
    Run(30);

    EXPECT_NEAR(Position(body).x, 90.0f, 0.05f);
    EXPECT_NEAR(Position(body).y, 45.0f, 0.05f) << "a dynamic body would have fallen";
}

TEST_F(Box2DBackendTest, AGravityScaleScalesTheFall) {
    const entt::entity falling = AddBody(BodyType::Dynamic, 0.0f, 1000.0f);
    AddCircle(falling, 8.0f);
    const entt::entity hanging = AddBody(BodyType::Dynamic, 200.0f, 1000.0f);
    AddCircle(hanging, 8.0f);
    m_registry.get<BRITE::RigidBodyComponent>(hanging).GravityScale = 0.0f;
    const entt::entity diving = AddBody(BodyType::Dynamic, 400.0f, 1000.0f);
    AddCircle(diving, 8.0f);
    m_registry.get<BRITE::RigidBodyComponent>(diving).GravityScale = 2.0f;

    Run(30);

    const float fallen = 1000.0f - Position(falling).y;
    EXPECT_NEAR(fallen, 38.75f, 1.5f) << "half a second at 300 px/s^2";
    EXPECT_NEAR(1000.0f - Position(hanging).y, 0.0f, 1e-3f);
    EXPECT_NEAR(1000.0f - Position(diving).y, 2.0f * fallen, 1.5f);
}

TEST_F(Box2DBackendTest, ARotationIsReadFromTheTransformAndTheTurnIsWrittenBack) {
    const entt::entity body = m_registry.create();
    m_registry.emplace<BRITE::TransformComponent>(body, BRITE::Vector2{0.0f, 0.0f}, 90.0f);
    auto& rb = m_registry.emplace<BRITE::RigidBodyComponent>(body);
    rb.GravityScale = 0.0f;
    AddBox(body, 16.0f, 16.0f);

    Tick();
    EXPECT_NEAR(Angle(body), PI / 2.0f, 0.01f) << "the transform's 90 degrees is the body's";

    b2Body_SetAngularVelocity(Body(body), 1.0f);
    Run(60);

    EXPECT_NEAR(Angle(body), PI / 2.0f + 1.0f, 0.03f) << "and a radian a second more for a second";
    const BRITE::Quaternion& rotation = Transform(body).Rotation;
    EXPECT_EQ(rotation.x, 0.0f);
    EXPECT_EQ(rotation.y, 0.0f);
    EXPECT_NEAR(rotation.z * rotation.z + rotation.w * rotation.w, 1.0f, 1e-4f) << "still a unit quaternion about z";
}

TEST_F(Box2DBackendTest, ABodyWithFixedRotationDoesNotTurn) {
    const entt::entity free = AddBody(BodyType::Dynamic, 0.0f, 0.0f);
    m_registry.get<BRITE::RigidBodyComponent>(free).GravityScale = 0.0f;
    AddBox(free, 16.0f, 16.0f);
    const entt::entity fixed = AddBody(BodyType::Dynamic, 200.0f, 0.0f);
    m_registry.get<BRITE::RigidBodyComponent>(fixed).GravityScale = 0.0f;
    m_registry.get<BRITE::RigidBodyComponent>(fixed).FixedRotation = true;
    AddBox(fixed, 16.0f, 16.0f);
    Tick();

    // A knock to the side of each, off its centre, which is a torque; the same to both.
    for (const entt::entity body : {free, fixed}) {
        const b2Vec2 centre = b2Body_GetPosition(Body(body));
        b2Body_ApplyLinearImpulse(Body(body), {0.0f, 0.5f}, {centre.x + 0.2f, centre.y}, true);
    }
    Run(30);

    EXPECT_GT(std::abs(Angle(free)), 0.5f) << "the knock turns a free body";
    EXPECT_NEAR(Angle(fixed), 0.0f, 1e-4f) << "and does not turn one with its rotation fixed";
    EXPECT_GT(Position(fixed).y, 10.0f) << "though it still moves it";
}

TEST_F(Box2DBackendTest, TheZOfATransformIsLeftAlone) {
    const entt::entity body = m_registry.create();
    m_registry.emplace<BRITE::TransformComponent>(body, BRITE::Vector3{0.0f, 300.0f, 7.0f});
    m_registry.emplace<BRITE::RigidBodyComponent>(body);
    AddBox(body, 16.0f, 16.0f);

    Run(10);

    EXPECT_EQ(Transform(body).Position.z, 7.0f);
    EXPECT_LT(Transform(body).Position.y, 300.0f);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

TEST_F(Box2DBackendTest, TwoBodiesThatMeetAreOneCollisionEventNamingBoth) {
    Listen();
    const entt::entity ground = AddGround();
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddCircle(ball, 16.0f);

    Run(90);

    ASSERT_EQ(m_log.begun.size(), 1u);
    EXPECT_TRUE(IsPair(m_log.begun[0].EntityA, m_log.begun[0].EntityB, ball, ground));
    EXPECT_TRUE(m_log.ended.empty());
}

TEST_F(Box2DBackendTest, TwoBodiesThatPartAreOneCollisionEndEventNamingBoth) {
    Listen();
    const entt::entity ground = AddGround();
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddCircle(ball, 16.0f);
    Run(90);
    ASSERT_TRUE(m_log.ended.empty()) << "still resting on the ground";

    b2Body_SetLinearVelocity(Body(ball), {0.0f, 5.0f});
    Run(10);

    ASSERT_EQ(m_log.ended.size(), 1u);
    EXPECT_TRUE(IsPair(m_log.ended[0].EntityA, m_log.ended[0].EntityB, ball, ground));
}

// Box2D 3.0 reports a hit only for a shape that asks, and only when the impulse a contact ends its step with is above
// nothing -- which, after the solver relaxes a hard stop, it often is not: of bodies landing at the same speed, some
// are reported and some are not. So what is fixed here is that every collider asks, and that a hit Box2D does report
// arrives whole; not which landings Box2D chooses to report.

TEST_F(Box2DBackendTest, EveryColliderAsksBox2DToReportItsHits) {
    const entt::entity box = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddBox(box, 30.0f, 30.0f);
    const entt::entity ball = AddBody(BodyType::Dynamic, 100.0f, 100.0f);
    AddCircle(ball, 16.0f);
    Tick();

    const b2ShapeId boxShape = Box2DBackend::GetB2Shape(m_registry.get<BRITE::BoxColliderComponent>(box).RuntimeShape);
    const b2ShapeId ballShape =
        Box2DBackend::GetB2Shape(m_registry.get<BRITE::CircleColliderComponent>(ball).RuntimeShape);
    EXPECT_TRUE(b2Shape_AreHitEventsEnabled(boxShape));
    EXPECT_TRUE(b2Shape_AreHitEventsEnabled(ballShape));
}

TEST_F(Box2DBackendTest, ABodyThatLandsHardCanBeAHitEventNamingBothAndTheSpeedAndNormalOfIt) {
    Listen();
    const entt::entity ground = AddGround();
    // A row of bodies of different sizes let drop together, so that Box2D reports some of them.
    std::vector<entt::entity> bodies;
    float x = -500.0f;
    for (const float size : {20.0f, 29.0f, 30.0f, 32.0f, 40.0f, 60.0f}) {
        const entt::entity box = AddBody(BodyType::Dynamic, x, 100.0f);
        AddBox(box, size, size);
        bodies.push_back(box);
        const entt::entity ball = AddBody(BodyType::Dynamic, x + 90.0f, 100.0f);
        AddCircle(ball, size / 2.0f);
        bodies.push_back(ball);
        x += 180.0f;
    }

    Run(120);

    ASSERT_FALSE(m_log.hit.empty()) << "a dozen bodies landing at 6 m/s, and Box2D reports none of them";
    for (const BRITE::PhysicsHitEvent& hit : m_log.hit) {
        const bool onGround = hit.EntityA == ground || hit.EntityB == ground;
        const entt::entity other = hit.EntityA == ground ? hit.EntityB : hit.EntityA;
        EXPECT_TRUE(onGround);
        EXPECT_NE(std::find(bodies.begin(), bodies.end(), other), bodies.end());
        // Each falls 100 px less its own half height to the ground's top at 15: 5.5 to 6.9 m/s.
        EXPECT_GT(hit.ApproachSpeed, 5.0f);
        EXPECT_LT(hit.ApproachSpeed, 7.5f);
        EXPECT_NEAR(std::abs(hit.NormalY), 1.0f, 0.01f) << "straight up or down, on level ground";
        EXPECT_NEAR(hit.NormalX, 0.0f, 0.01f);
    }
}

TEST_F(Box2DBackendTest, ABodySetDownGentlyIsNoHit) {
    Listen();
    AddGround();
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 15.0f + 16.0f + 0.5f);
    AddCircle(ball, 16.0f);

    Run(60);

    EXPECT_TRUE(m_log.hit.empty()) << "under a metre a second is a touch, which is not a hit";
    EXPECT_NEAR(Position(ball).y, 15.0f + 16.0f, 1.0f);
}

TEST_F(Box2DBackendTest, ACollisionWithNoDispatcherInTheRegistryMakesOne) {
    AddGround();
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddCircle(ball, 16.0f);
    Run(10);
    ASSERT_EQ(m_registry.ctx().find<entt::dispatcher>(), nullptr) << "nothing has touched yet, so nothing is made";

    Run(80);

    EXPECT_NE(m_registry.ctx().find<entt::dispatcher>(), nullptr);
}

TEST_F(Box2DBackendTest, ASensorSendsSensorEventsToWhatPassesThroughItAndBlocksNothing) {
    Listen();
    const entt::entity zone = AddBody(BodyType::Static, 0.0f, 0.0f);
    AddBox(zone, 100.0f, 100.0f).IsSensor = true;
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 200.0f);
    AddCircle(ball, 8.0f);

    Run(120);

    ASSERT_EQ(m_log.sensorBegun.size(), 1u);
    EXPECT_EQ(m_log.sensorBegun[0].SensorEntity, zone);
    EXPECT_EQ(m_log.sensorBegun[0].VisitorEntity, ball);
    ASSERT_EQ(m_log.sensorEnded.size(), 1u);
    EXPECT_EQ(m_log.sensorEnded[0].SensorEntity, zone);
    EXPECT_EQ(m_log.sensorEnded[0].VisitorEntity, ball);
    EXPECT_LT(Position(ball).y, -58.0f) << "fell clean through";
    EXPECT_TRUE(m_log.begun.empty()) << "a sensor is not a collision";
}

TEST_F(Box2DBackendTest, ASensorOverlapWithNoDispatcherInTheRegistryMakesOne) {
    const entt::entity zone = AddBody(BodyType::Static, 0.0f, 0.0f);
    AddBox(zone, 100.0f, 100.0f).IsSensor = true;
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 200.0f);
    AddCircle(ball, 8.0f);

    Run(120);

    EXPECT_NE(m_registry.ctx().find<entt::dispatcher>(), nullptr);
}

// ---------------------------------------------------------------------------
// Joints
// ---------------------------------------------------------------------------

TEST_F(Box2DBackendTest, ADistanceJointHoldsTwoBodiesAtItsLengthAsOneSwingsFromTheOther) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddCircle(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f);

    float worstLength = 0.0f;
    float lowest = 300.0f;
    for (int i = 0; i < 180; ++i) {
        Tick();
        worstLength = std::max(worstLength, std::abs(Distance(bob, anchor) - 30.0f));
        lowest = std::min(lowest, Position(bob).y);
    }

    EXPECT_LT(worstLength, 1.5f);
    EXPECT_LT(lowest, 275.0f) << "it swung down under its own weight";
}

TEST_F(Box2DBackendTest, ADistanceJointWithASpringStretchesUnderTheWeightItHolds) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 270.0f);
    AddCircle(bob, 6.0f);
    BRITE::DistanceJointComponent& joint = AddDistanceJoint(bob, anchor, 30.0f);
    joint.EnableSpring = true;
    joint.Hertz = 1.0f;
    joint.DampingRatio = 0.7f;

    Run(360);

    // At 1 Hz the spring sags g / (2 pi f)^2 = 0.25 m, or 7.6 px; a rigid joint holds 30.
    EXPECT_GT(Distance(bob, anchor), 33.0f);
    EXPECT_LT(Distance(bob, anchor), 45.0f);
}

TEST_F(Box2DBackendTest, ALightlyDampedSpringJointSwingsWellPastItsRestLength) {
    // Its rest length under the weight it holds is about 32 px.
    EXPECT_LT(NearestStretchedSpringComes(0.05f), 25.0f);
}

TEST_F(Box2DBackendTest, ACriticallyDampedSpringJointSettlesWithoutSwingingPastItsRestLength) {
    EXPECT_GT(NearestStretchedSpringComes(1.0f), 30.0f);
}

TEST_F(Box2DBackendTest, ARevoluteJointPinsABodyToAPointItSwingsAbout) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddBox(bob, 16.0f, 16.0f);
    AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f});

    float worstLength = 0.0f;
    float lowest = 300.0f;
    for (int i = 0; i < 180; ++i) {
        Tick();
        worstLength = std::max(worstLength, std::abs(Distance(bob, anchor) - 30.0f));
        lowest = std::min(lowest, Position(bob).y);
    }

    EXPECT_LT(worstLength, 1.5f) << "the body's origin stays 30 px from the pivot it hangs from";
    EXPECT_LT(lowest, 275.0f);
}

TEST_F(Box2DBackendTest, ARevoluteJointWithNoLimitsLetsTheBodySwingWide) {
    EXPECT_GT(WidestSwingDegrees(false), 45.0f);
}

TEST_F(Box2DBackendTest, ARevoluteJointsLimitsHoldTheAngleTheyAreSetTo) {
    EXPECT_LT(WidestSwingDegrees(true), 12.0f) << "limits of 10 degrees either side";
}

TEST_F(Box2DBackendTest, AJointToNothingIsNeverMade) {
    const entt::entity noBody = m_registry.create(); // a target that is no body at all
    const entt::entity gone = m_registry.create();
    m_registry.destroy(gone); // a target that is no entity any more
    const entt::entity bareBody = m_registry.create();
    m_registry.emplace<BRITE::RigidBodyComponent>(bareBody); // a body the world never makes: it has no transform
    const entt::entity nothing = entt::null;

    for (const entt::entity target : {noBody, gone, bareBody, nothing}) {
        const entt::entity holder = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
        AddBox(holder, 16.0f, 16.0f);
        AddDistanceJoint(holder, target, 30.0f);
        AddRevoluteJoint(holder, target, {0.0f, 0.0f});
    }

    Run(10);

    for (const entt::entity holder : m_registry.view<BRITE::DistanceJointComponent>()) {
        EXPECT_EQ(m_registry.get<BRITE::DistanceJointComponent>(holder).RuntimeJoint, BRITE::NullPhysicsHandle);
        EXPECT_EQ(m_registry.get<BRITE::RevoluteJointComponent>(holder).RuntimeJoint, BRITE::NullPhysicsHandle);
        EXPECT_EQ(m_backend.GetJointForce(m_registry, holder), 0.0f);
    }
}

TEST_F(Box2DBackendTest, ADistanceJointPastItsBreakForceBreaksAndSaysSoAndTheBodyFallsFree) {
    Listen();
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddCircle(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f).BreakForce = 0.001f;

    Run(60);

    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJointComponent>(bob)) << "the joint went with its break";
    ASSERT_EQ(m_log.broken.size(), 1u);
    EXPECT_EQ(m_log.broken[0].EntityA, bob);
    EXPECT_EQ(m_log.broken[0].EntityB, anchor);
    EXPECT_GT(m_log.broken[0].FinalForceMagnitude, 0.001f);
    EXPECT_GT(Distance(bob, anchor), 100.0f) << "no longer held to the 30 px of a pendulum";
}

TEST_F(Box2DBackendTest, ARevoluteJointPastItsBreakForceBreaksAndSaysSo) {
    Listen();
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddBox(bob, 16.0f, 16.0f);
    AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f}).BreakForce = 0.001f;

    Run(60);

    EXPECT_FALSE(m_registry.all_of<BRITE::RevoluteJointComponent>(bob));
    ASSERT_EQ(m_log.broken.size(), 1u);
    EXPECT_EQ(m_log.broken[0].EntityA, bob);
    EXPECT_EQ(m_log.broken[0].EntityB, anchor);
    EXPECT_GT(Distance(bob, anchor), 100.0f);
}

TEST_F(Box2DBackendTest, AJointBreaksJustTheSameWithNobodyListening) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddCircle(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f).BreakForce = 0.001f;

    Run(60);

    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJointComponent>(bob));
    EXPECT_EQ(m_registry.ctx().find<entt::dispatcher>(), nullptr) << "a break makes no dispatcher; only contacts do";
}

TEST_F(Box2DBackendTest, AJointWithNoBreakForceOrOneAboveItsLoadHoldsForGood) {
    Listen();
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity unbreakable = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddCircle(unbreakable, 6.0f);
    AddDistanceJoint(unbreakable, anchor, 30.0f); // BreakForce 0 is no limit
    const entt::entity strong = AddBody(BodyType::Dynamic, -30.0f, 300.0f);
    AddCircle(strong, 6.0f);
    AddDistanceJoint(strong, anchor, 30.0f).BreakForce = 100000.0f;

    Run(240);

    EXPECT_TRUE(m_registry.all_of<BRITE::DistanceJointComponent>(unbreakable));
    EXPECT_TRUE(m_registry.all_of<BRITE::DistanceJointComponent>(strong));
    EXPECT_TRUE(m_log.broken.empty());
    EXPECT_LT(Distance(unbreakable, anchor), 31.5f);
}

TEST_F(Box2DBackendTest, AJointForceIsTheWeightAHangingBodyPullsOnItsDistanceJoint) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 270.0f);
    AddCircle(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f);

    Run(120);

    const float weight = b2Body_GetMass(Body(bob)) * GRAVITY;
    EXPECT_NEAR(m_backend.GetJointForce(m_registry, bob), weight, 0.1f * weight);
}

TEST_F(Box2DBackendTest, AJointForceIsTheWeightAHangingBodyPullsOnItsRevoluteJoint) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 270.0f);
    AddBox(bob, 16.0f, 16.0f);
    AddRevoluteJoint(bob, anchor, {0.0f, 30.0f});

    Run(120);

    const float weight = b2Body_GetMass(Body(bob)) * GRAVITY;
    EXPECT_NEAR(m_backend.GetJointForce(m_registry, bob), weight, 0.1f * weight);
}

TEST_F(Box2DBackendTest, AnEntityWithNoJointFeelsNoJointForce) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f, 16.0f);
    Run(5);

    EXPECT_EQ(m_backend.GetJointForce(m_registry, body), 0.0f);
}

TEST_F(Box2DBackendTest, ADestroyedDistanceJointIsGoneFromTheEntityAndTheBodyIsFree) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddCircle(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f);
    Run(10);
    ASSERT_GT(m_backend.GetJointForce(m_registry, bob), 0.0f);

    m_backend.DestroyJoint(m_registry, bob);

    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJointComponent>(bob));
    EXPECT_EQ(m_backend.GetJointForce(m_registry, bob), 0.0f);
    Run(60);
    EXPECT_GT(Distance(bob, anchor), 100.0f);
}

TEST_F(Box2DBackendTest, ADestroyedRevoluteJointIsGoneFromTheEntityAndTheBodyIsFree) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddBox(bob, 16.0f, 16.0f);
    AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f});
    Run(10);
    ASSERT_GT(m_backend.GetJointForce(m_registry, bob), 0.0f);

    m_backend.DestroyJoint(m_registry, bob);

    EXPECT_FALSE(m_registry.all_of<BRITE::RevoluteJointComponent>(bob));
    EXPECT_EQ(m_backend.GetJointForce(m_registry, bob), 0.0f);
    Run(60);
    EXPECT_GT(Distance(bob, anchor), 100.0f);
}

TEST_F(Box2DBackendTest, DestroyingJointsTakesEveryJointOfTheEntityAndAJointNotYetMadeToo) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity both = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddCircle(both, 6.0f);
    AddDistanceJoint(both, anchor, 30.0f);
    AddRevoluteJoint(both, anchor, {-30.0f, 0.0f});
    const entt::entity bare = AddBody(BodyType::Dynamic, 0.0f, 0.0f);
    Run(5);
    const entt::entity unmade = AddBody(BodyType::Dynamic, 0.0f, 0.0f);
    AddDistanceJoint(unmade, anchor, 30.0f); // and no PreStep after it, so no joint behind it
    ASSERT_EQ(m_registry.get<BRITE::DistanceJointComponent>(unmade).RuntimeJoint, BRITE::NullPhysicsHandle);

    m_backend.DestroyJoint(m_registry, both);
    m_backend.DestroyJoint(m_registry, bare); // nothing to take: no harm done
    m_backend.DestroyJoint(m_registry, unmade);

    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJointComponent>(both));
    EXPECT_FALSE(m_registry.all_of<BRITE::RevoluteJointComponent>(both));
    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJointComponent>(unmade));
    EXPECT_TRUE(m_registry.valid(bare));
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

TEST_F(Box2DBackendTest, AMaterialSetReachesTheShapesOfABodyAlreadyMade) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f, 16.0f);
    AddCircle(body, 8.0f);
    Tick();

    const auto material = std::make_shared<PhysicsMaterial>(PhysicsMaterial{3.0f, 0.9f, 0.5f});
    m_backend.SetMaterial(m_registry, body, material);

    const auto& box = m_registry.get<BRITE::BoxColliderComponent>(body);
    const auto& circle = m_registry.get<BRITE::CircleColliderComponent>(body);
    EXPECT_EQ(box.Material, material);
    EXPECT_EQ(circle.Material, material);
    for (const BRITE::PhysicsHandle handle : {box.RuntimeShape, circle.RuntimeShape}) {
        const b2ShapeId shape = Box2DBackend::GetB2Shape(handle);
        ASSERT_TRUE(b2Shape_IsValid(shape));
        EXPECT_EQ(b2Shape_GetDensity(shape), 3.0f);
        EXPECT_EQ(b2Shape_GetFriction(shape), 0.9f);
        EXPECT_EQ(b2Shape_GetRestitution(shape), 0.5f);
    }
}

TEST_F(Box2DBackendTest, AMaterialSetBeforeTheShapeIsMadeIsTheMaterialItIsMadeWith) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f, 16.0f);
    AddCircle(body, 8.0f);
    const auto material = std::make_shared<PhysicsMaterial>(PhysicsMaterial{3.0f, 0.9f, 0.5f});

    m_backend.SetMaterial(m_registry, body, material);
    EXPECT_EQ(m_registry.get<BRITE::BoxColliderComponent>(body).Material, material);
    EXPECT_EQ(m_registry.get<BRITE::CircleColliderComponent>(body).Material, material);
    Tick();

    const b2ShapeId box = Box2DBackend::GetB2Shape(m_registry.get<BRITE::BoxColliderComponent>(body).RuntimeShape);
    const b2ShapeId circle =
        Box2DBackend::GetB2Shape(m_registry.get<BRITE::CircleColliderComponent>(body).RuntimeShape);
    EXPECT_EQ(b2Shape_GetFriction(box), 0.9f);
    EXPECT_EQ(b2Shape_GetRestitution(circle), 0.5f);
}

TEST_F(Box2DBackendTest, AMaterialSetOnABodyWithNoColliderChangesNothing) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    Tick();

    m_backend.SetMaterial(m_registry, body, std::make_shared<PhysicsMaterial>());

    EXPECT_FALSE(m_registry.all_of<BRITE::BoxColliderComponent>(body));
    EXPECT_FALSE(m_registry.all_of<BRITE::CircleColliderComponent>(body));
}

TEST_F(Box2DBackendTest, ABouncierMaterialSetAfterTheBodyIsMadeIsABouncierBody) {
    AddGround();
    const entt::entity dead = AddBody(BodyType::Dynamic, -200.0f, 100.0f);
    AddCircle(dead, 16.0f);
    const entt::entity lively = AddBody(BodyType::Dynamic, 200.0f, 100.0f);
    AddCircle(lively, 16.0f);
    Tick();
    m_backend.SetMaterial(m_registry, lively, std::make_shared<PhysicsMaterial>(PhysicsMaterial{1.0f, 0.3f, 0.9f}));

    float deadPeak = 0.0f;
    float livelyPeak = 0.0f;
    for (int i = 0; i < 90; ++i) {
        Tick();
        if (i > 45) { // after the first landing, at about tick 41
            deadPeak = std::max(deadPeak, Position(dead).y);
            livelyPeak = std::max(livelyPeak, Position(lively).y);
        }
    }

    EXPECT_LT(deadPeak, 15.0f + 16.0f + 3.0f) << "it stays down";
    EXPECT_GT(livelyPeak, 15.0f + 16.0f + 25.0f) << "and the other comes back up";
}
