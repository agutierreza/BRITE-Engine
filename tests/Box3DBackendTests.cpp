// Box3DBackend on its own: a registry, the backend's PreStep and Step, and nothing else -- no window, application or
// renderer. A world is stepped the way the framework steps it, PreStep then Step, one sixtieth of a second a tick.
//
// Positions are in pixels, 30 to the metre, so the backend's gravity of 10 m/s^2 is 300 px/s^2, downward -- toward
// negative y. A body's place and turn are read from its entity's transform, which is where a scene reads them; what
// the cases reach into Box3D for is what a scene has no way to ask: a body's mass, a shape's material, a push to start
// a body moving.
//
// The backend sends no contact or sensor events yet, so there are none here; what it sends when a joint breaks is
// covered.

#include <ECS/Components.hpp>
#include <ECS/Events.hpp>
#include <Systems/Box3DBackend.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

using BRITE::Box3DBackend;
using BRITE::PhysicsMaterial;
using BodyType = BRITE::RigidBodyComponent::BodyType;

constexpr float TICK = 1.0f / 60.0f;
constexpr float PI = 3.14159265358979323846f;
constexpr float GRAVITY = 10.0f; // m/s^2, the backend's own

class Box3DBackendTest : public ::testing::Test {
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
        m_registry.ctx()
            .emplace<entt::dispatcher>()
            .sink<BRITE::PhysicsJointBreakEvent>()
            .connect<&Box3DBackendTest::OnBreak>(*this);
    }
    void OnBreak(const BRITE::PhysicsJointBreakEvent& event) {
        m_broken.push_back(event);
    }

    entt::entity AddBody(BodyType type, float x, float y, float z = 0.0f) {
        const entt::entity entity = m_registry.create();
        m_registry.emplace<BRITE::TransformComponent>(entity, BRITE::Vector3{x, y, z});
        m_registry.emplace<BRITE::RigidBodyComponent>(entity).Type = type;
        return entity;
    }
    BRITE::BoxCollider3DComponent& AddBox(entt::entity entity, float size) {
        auto& box = m_registry.emplace<BRITE::BoxCollider3DComponent>(entity);
        box.Size = {size, size, size};
        return box;
    }
    BRITE::SphereColliderComponent& AddSphere(entt::entity entity, float radius) {
        auto& sphere = m_registry.emplace<BRITE::SphereColliderComponent>(entity);
        sphere.Radius = radius;
        return sphere;
    }
    // A static floor whose top is at y = 15.
    entt::entity AddGround() {
        const entt::entity ground = AddBody(BodyType::Static, 0.0f, 0.0f);
        auto& box = m_registry.emplace<BRITE::BoxCollider3DComponent>(ground);
        box.Size = {1200.0f, 30.0f, 1200.0f};
        return ground;
    }
    BRITE::DistanceJoint3DComponent& AddDistanceJoint(entt::entity entity, entt::entity target, float length) {
        auto& joint = m_registry.emplace<BRITE::DistanceJoint3DComponent>(entity);
        joint.TargetEntity = target;
        joint.Length = length;
        return joint;
    }
    // A pivot at `target`, which `entity` hangs from at the point `anchor` -- in the body's own pixels.
    BRITE::RevoluteJoint3DComponent& AddRevoluteJoint(entt::entity entity, entt::entity target, BRITE::Vector3 anchor) {
        auto& joint = m_registry.emplace<BRITE::RevoluteJoint3DComponent>(entity);
        joint.TargetEntity = target;
        joint.LocalAnchorA = anchor;
        return joint;
    }

    BRITE::TransformComponent& Transform(entt::entity entity) {
        return m_registry.get<BRITE::TransformComponent>(entity);
    }
    BRITE::Vector3 Position(entt::entity entity) {
        return Transform(entity).Position;
    }
    // The turn about z the transform's quaternion says, in radians.
    float AngleAboutZ(entt::entity entity) {
        const BRITE::Quaternion& rotation = Transform(entity).Rotation;
        return 2.0f * std::atan2(rotation.z, rotation.w);
    }
    float Distance(entt::entity a, entt::entity b) {
        const BRITE::Vector3 pa = Position(a);
        const BRITE::Vector3 pb = Position(b);
        return std::sqrt((pa.x - pb.x) * (pa.x - pb.x) + (pa.y - pb.y) * (pa.y - pb.y) + (pa.z - pb.z) * (pa.z - pb.z));
    }
    b3BodyId Body(entt::entity entity) {
        return Box3DBackend::GetB3Body(m_registry.get<BRITE::RigidBodyComponent>(entity).RuntimeBody);
    }

    // What a body let go on a hinge did in three seconds: how low it got, how far it strayed from the 30 px it hangs
    // from its pivot, and how far to either side of the pivot's plane, along x.
    struct Swung {
        float lowest = 300.0f;
        float worstLength = 0.0f;
        float widestAcross = 0.0f;
    };
    Swung LetGoForThreeSeconds(entt::entity bob, entt::entity pivot) {
        Swung swung;
        for (int i = 0; i < 180; ++i) {
            Tick();
            swung.lowest = std::min(swung.lowest, Position(bob).y);
            swung.worstLength = std::max(swung.worstLength, std::abs(Distance(bob, pivot) - 30.0f));
            swung.widestAcross = std::max(swung.widestAcross, std::abs(Position(bob).x));
        }
        return swung;
    }
    // A body hanging off a pivot 30 px away along z -- the pivot at (0, 300, 0), the body at (0, 300, 30) -- which a
    // hinge about z cannot let fall, since the body is on the hinge's axis, and a hinge about x can.
    entt::entity AddPivot() {
        return AddBody(BodyType::Static, 0.0f, 300.0f);
    }
    entt::entity AddBodyHangingOffThePivotAlongZ() {
        const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 300.0f, 30.0f);
        AddBox(bob, 16.0f);
        return bob;
    }

    // A body let go 20 px past the end of a spring joint of 30 px at 2 Hz, with the given damping: the nearest it
    // comes to the anchor in two seconds. It overshoots the spring's rest length by as much as it is not damped.
    float NearestStretchedSpringComes(float dampingRatio) {
        const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
        const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 250.0f);
        AddSphere(bob, 6.0f);
        BRITE::DistanceJoint3DComponent& joint = AddDistanceJoint(bob, anchor, 30.0f);
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
    // switched on or not: the most it turns about the hinge, in degrees, in three seconds.
    float WidestSwingDegrees(bool limited) {
        const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
        const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
        AddBox(bob, 16.0f);
        BRITE::RevoluteJoint3DComponent& joint = AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f, 0.0f});
        joint.EnableLimit = limited;
        joint.LowerAngle = -10.0f;
        joint.UpperAngle = 10.0f;
        float widest = 0.0f;
        for (int i = 0; i < 180; ++i) {
            Tick();
            widest = std::max(widest, std::abs(AngleAboutZ(bob)));
        }
        return widest * 180.0f / PI;
    }

    entt::registry m_registry;
    Box3DBackend m_backend;
    std::vector<BRITE::PhysicsJointBreakEvent> m_broken;
};

} // namespace

// ---------------------------------------------------------------------------
// Bodies
// ---------------------------------------------------------------------------

TEST_F(Box3DBackendTest, ADynamicBodyFallsAtGravityAndItsTransformFollows) {
    const entt::entity body = AddBody(BodyType::Dynamic, 5.0f, 300.0f, -8.0f);
    AddBox(body, 16.0f);

    Run(60);

    // A second at 300 px/s^2 from rest is 150 px; stepping in sixtieths, a little over 152.
    EXPECT_NEAR(Position(body).y, 300.0f - 152.5f, 2.0f);
    EXPECT_NEAR(Position(body).x, 5.0f, 1e-3f) << "straight down moves it no other way";
    EXPECT_NEAR(Position(body).z, -8.0f, 1e-3f);
}

TEST_F(Box3DBackendTest, ABodyIsMadeOnceAndKeepsItsHandle) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f);
    const auto& rb = m_registry.get<BRITE::RigidBodyComponent>(body);
    ASSERT_EQ(rb.RuntimeBody, BRITE::NullPhysicsHandle) << "none before a PreStep";

    Tick();
    const BRITE::PhysicsHandle handle = rb.RuntimeBody;
    ASSERT_NE(handle, BRITE::NullPhysicsHandle);
    EXPECT_TRUE(b3Body_IsValid(Body(body)));

    Run(5);
    EXPECT_EQ(rb.RuntimeBody, handle);
}

TEST_F(Box3DBackendTest, TheBackendsWorldIsLiveAndAnEntitysValidBodyIsTheOneItHolds) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f);
    Tick();

    EXPECT_TRUE(b3World_IsValid(m_backend.GetWorld()));
    const b3BodyId held = BRITE::GetValidB3Body(m_registry, body);
    EXPECT_TRUE(b3Body_IsValid(held));
    EXPECT_EQ(Box3DBackend::ToHandle(held), m_registry.get<BRITE::RigidBodyComponent>(body).RuntimeBody);
}

TEST_F(Box3DBackendTest, AStaticBodyStaysWhereItIsAndItsTransformIsNeverWritten) {
    const entt::entity body = AddBody(BodyType::Static, 12.0f, -7.0f, 3.0f);
    AddBox(body, 16.0f);
    const BRITE::TransformComponent before = Transform(body);

    Run(30);

    EXPECT_EQ(Position(body).x, before.Position.x);
    EXPECT_EQ(Position(body).y, before.Position.y);
    EXPECT_EQ(Position(body).z, before.Position.z);
    EXPECT_EQ(Transform(body).Rotation.w, before.Rotation.w);
}

TEST_F(Box3DBackendTest, ADynamicBoxComesToRestOnAStaticOne) {
    AddGround();
    const entt::entity box = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddBox(box, 30.0f);

    Run(180);

    EXPECT_NEAR(Position(box).y, 15.0f + 15.0f, 1.5f) << "the ground's top, and half its own height";
    EXPECT_NEAR(Position(box).x, 0.0f, 0.5f);
    EXPECT_NEAR(Position(box).z, 0.0f, 0.5f);
    EXPECT_NEAR(b3Body_GetLinearVelocity(Body(box)).y, 0.0f, 0.05f) << "and it has stopped";
}

TEST_F(Box3DBackendTest, ADynamicSphereComesToRestOnAStaticBox) {
    AddGround();
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddSphere(ball, 16.0f);

    Run(180);

    EXPECT_NEAR(Position(ball).y, 15.0f + 16.0f, 1.5f) << "the ground's top, and its radius";
}

TEST_F(Box3DBackendTest, ABoxCollidersOffsetSetsWhereTheShapeSitsOnTheBody) {
    AddGround();
    const entt::entity box = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddBox(box, 30.0f).Offset = {0.0f, 20.0f, 0.0f};

    Run(180);

    // The shape rests on the ground 20 px above the body's origin, so the origin is 20 px lower than it would be.
    EXPECT_NEAR(Position(box).y, 15.0f + 15.0f - 20.0f, 1.5f);
}

TEST_F(Box3DBackendTest, ASphereCollidersOffsetSetsWhereTheShapeSitsOnTheBody) {
    AddGround();
    const entt::entity ball = AddBody(BodyType::Dynamic, 0.0f, 100.0f);
    AddSphere(ball, 16.0f).Offset = {0.0f, 20.0f, 0.0f};

    Run(180);

    EXPECT_NEAR(Position(ball).y, 15.0f + 16.0f - 20.0f, 1.5f);
}

TEST_F(Box3DBackendTest, AKinematicBodyIsWhereItsTransformPutsItAndGravityLeavesItAlone) {
    const entt::entity body = AddBody(BodyType::Kinematic, 0.0f, 0.0f);
    AddBox(body, 16.0f);
    Tick();

    Transform(body).Position = {90.0f, 45.0f, -20.0f};
    Run(30);

    EXPECT_NEAR(Position(body).x, 90.0f, 0.05f);
    EXPECT_NEAR(Position(body).y, 45.0f, 0.05f) << "a dynamic body would have fallen";
    EXPECT_NEAR(Position(body).z, -20.0f, 0.05f);
}

TEST_F(Box3DBackendTest, AGravityScaleScalesTheFall) {
    const entt::entity falling = AddBody(BodyType::Dynamic, 0.0f, 1000.0f);
    AddSphere(falling, 8.0f);
    const entt::entity hanging = AddBody(BodyType::Dynamic, 200.0f, 1000.0f);
    AddSphere(hanging, 8.0f);
    m_registry.get<BRITE::RigidBodyComponent>(hanging).GravityScale = 0.0f;
    const entt::entity diving = AddBody(BodyType::Dynamic, 400.0f, 1000.0f);
    AddSphere(diving, 8.0f);
    m_registry.get<BRITE::RigidBodyComponent>(diving).GravityScale = 2.0f;

    Run(30);

    const float fallen = 1000.0f - Position(falling).y;
    EXPECT_NEAR(fallen, 38.75f, 1.5f) << "half a second at 300 px/s^2";
    EXPECT_NEAR(1000.0f - Position(hanging).y, 0.0f, 1e-3f);
    EXPECT_NEAR(1000.0f - Position(diving).y, 2.0f * fallen, 1.5f);
}

TEST_F(Box3DBackendTest, ARotationIsReadFromTheTransformAndTheTurnIsWrittenBack) {
    // A quarter turn about y to start with: the quaternion of 90 degrees about y.
    const float half = PI / 4.0f;
    const entt::entity body = m_registry.create();
    m_registry.emplace<BRITE::TransformComponent>(body, BRITE::Vector3{0.0f, 0.0f, 0.0f},
                                                  BRITE::Quaternion{0.0f, std::sin(half), 0.0f, std::cos(half)});
    m_registry.emplace<BRITE::RigidBodyComponent>(body).GravityScale = 0.0f;
    AddSphere(body, 8.0f);

    Tick();
    EXPECT_NEAR(std::abs(Transform(body).Rotation.y), std::sin(half), 1e-3f)
        << "the transform's quarter turn is the body's";
    EXPECT_NEAR(std::abs(Transform(body).Rotation.w), std::cos(half), 1e-3f);

    b3Body_SetAngularVelocity(Body(body), {0.0f, 1.0f, 0.0f});
    Run(60);

    // A radian a second more about y for a second: 90 degrees and a radian, as a quaternion's half-angle.
    const float turned = PI / 2.0f + 1.0f;
    EXPECT_NEAR(std::abs(Transform(body).Rotation.y), std::sin(turned / 2.0f), 0.03f);
    EXPECT_NEAR(std::abs(Transform(body).Rotation.w), std::abs(std::cos(turned / 2.0f)), 0.03f);
    const BRITE::Quaternion& rotation = Transform(body).Rotation;
    EXPECT_NEAR(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w,
                1.0f, 1e-3f)
        << "still a unit quaternion";
}

TEST_F(Box3DBackendTest, ABodyWithFixedRotationDoesNotTurn) {
    const entt::entity free = AddBody(BodyType::Dynamic, 0.0f, 0.0f);
    m_registry.get<BRITE::RigidBodyComponent>(free).GravityScale = 0.0f;
    AddSphere(free, 8.0f);
    const entt::entity fixed = AddBody(BodyType::Dynamic, 200.0f, 0.0f);
    m_registry.get<BRITE::RigidBodyComponent>(fixed).GravityScale = 0.0f;
    m_registry.get<BRITE::RigidBodyComponent>(fixed).FixedRotation = true;
    AddSphere(fixed, 8.0f);
    Tick();

    b3Body_SetAngularVelocity(Body(free), {0.0f, 0.0f, 2.0f});
    b3Body_SetAngularVelocity(Body(fixed), {0.0f, 0.0f, 2.0f});
    Run(30);

    EXPECT_NEAR(AngleAboutZ(free), 1.0f, 0.03f) << "the same push turns a free body";
    EXPECT_NEAR(AngleAboutZ(fixed), 0.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Joints
// ---------------------------------------------------------------------------

TEST_F(Box3DBackendTest, ADistanceJointHoldsTwoBodiesAtItsLengthAsOneSwingsFromTheOther) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddSphere(bob, 6.0f);
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

TEST_F(Box3DBackendTest, ADistanceJointWithASpringStretchesUnderTheWeightItHolds) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 270.0f);
    AddSphere(bob, 6.0f);
    BRITE::DistanceJoint3DComponent& joint = AddDistanceJoint(bob, anchor, 30.0f);
    joint.EnableSpring = true;
    joint.Hertz = 1.0f;

    Run(360);

    // At 1 Hz the spring sags g / (2 pi f)^2 = 0.25 m, or 7.6 px; a rigid joint holds 30.
    EXPECT_GT(Distance(bob, anchor), 33.0f);
    EXPECT_LT(Distance(bob, anchor), 60.0f);
}

TEST_F(Box3DBackendTest, ALightlyDampedSpringJointSwingsWellPastItsRestLength) {
    // Its rest length under the weight it holds is about 32 px.
    EXPECT_LT(NearestStretchedSpringComes(0.05f), 25.0f);
}

TEST_F(Box3DBackendTest, ACriticallyDampedSpringJointSettlesWithoutSwingingPastItsRestLength) {
    EXPECT_GT(NearestStretchedSpringComes(1.0f), 30.0f);
}

TEST_F(Box3DBackendTest, ARevoluteJointHingesAboutItsAxis) {
    const entt::entity pivot = AddPivot();
    const entt::entity bob = AddBodyHangingOffThePivotAlongZ();
    AddRevoluteJoint(bob, pivot, {0.0f, 0.0f, -30.0f}).Axis = {3.0f, 0.0f, 0.0f}; // not a unit vector, and no matter

    const Swung swung = LetGoForThreeSeconds(bob, pivot);

    EXPECT_LT(swung.lowest, 275.0f) << "it swings down about x";
    EXPECT_LT(swung.worstLength, 1.5f);
    EXPECT_LT(swung.widestAcross, 1.0f) << "and stays in the plane the hinge allows";
}

TEST_F(Box3DBackendTest, ARevoluteJointWithTheDefaultAxisHingesAboutZAndHoldsABodyOnThatAxis) {
    const entt::entity pivot = AddPivot();
    const entt::entity bob = AddBodyHangingOffThePivotAlongZ();
    AddRevoluteJoint(bob, pivot, {0.0f, 0.0f, -30.0f});

    const Swung swung = LetGoForThreeSeconds(bob, pivot);

    EXPECT_GT(swung.lowest, 298.0f) << "a hinge about z cannot swing a body that hangs along z";
}

TEST_F(Box3DBackendTest, ARevoluteJointsAxisIsInTheSpaceOfTheBodyItIsOn) {
    // The same body, turned a quarter about y: its z is now the world's x, and the pivot 30 px off along the world's
    // -z is 30 px off along its own x. A default hinge, about its z, is a hinge about the world's x.
    const float half = PI / 4.0f;
    const entt::entity pivot = AddPivot();
    const entt::entity bob = m_registry.create();
    m_registry.emplace<BRITE::TransformComponent>(bob, BRITE::Vector3{0.0f, 300.0f, 30.0f},
                                                  BRITE::Quaternion{0.0f, std::sin(half), 0.0f, std::cos(half)});
    m_registry.emplace<BRITE::RigidBodyComponent>(bob);
    AddBox(bob, 16.0f);
    AddRevoluteJoint(bob, pivot, {30.0f, 0.0f, 0.0f});

    const Swung swung = LetGoForThreeSeconds(bob, pivot);

    EXPECT_LT(swung.lowest, 275.0f);
    EXPECT_LT(swung.worstLength, 1.5f);
    EXPECT_LT(swung.widestAcross, 1.0f);
}

TEST_F(Box3DBackendTest, ARevoluteJointWithNoAxisHingesAboutZ) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddBox(bob, 16.0f);
    AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f, 0.0f}).Axis = {0.0f, 0.0f, 0.0f};

    float lowest = 300.0f;
    float worstLength = 0.0f;
    for (int i = 0; i < 180; ++i) {
        Tick();
        lowest = std::min(lowest, Position(bob).y);
        worstLength = std::max(worstLength, std::abs(Distance(bob, anchor) - 30.0f));
    }

    EXPECT_LT(lowest, 275.0f) << "it swings, as with the default axis";
    EXPECT_LT(worstLength, 1.5f);
}

TEST_F(Box3DBackendTest, ARevoluteJointPinsABodyToAPointItSwingsAbout) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddBox(bob, 16.0f);
    AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f, 0.0f});

    float worstLength = 0.0f;
    float lowest = 300.0f;
    for (int i = 0; i < 180; ++i) {
        Tick();
        worstLength = std::max(worstLength, std::abs(Distance(bob, anchor) - 30.0f));
        lowest = std::min(lowest, Position(bob).y);
    }

    EXPECT_LT(worstLength, 1.5f) << "the body's origin stays 30 px from the pivot it hangs from";
    EXPECT_LT(lowest, 275.0f);
    EXPECT_NEAR(Position(bob).z, 0.0f, 1.0f) << "and it swings in the plane of the hinge";
}

TEST_F(Box3DBackendTest, ARevoluteJointWithNoLimitsLetsTheBodySwingWide) {
    EXPECT_GT(WidestSwingDegrees(false), 45.0f);
}

TEST_F(Box3DBackendTest, ARevoluteJointsLimitsHoldTheAngleTheyAreSetTo) {
    EXPECT_LT(WidestSwingDegrees(true), 12.0f) << "limits of 10 degrees either side";
}

TEST_F(Box3DBackendTest, AJointToNothingIsNeverMade) {
    const entt::entity noBody = m_registry.create(); // a target that is no body at all
    const entt::entity gone = m_registry.create();
    m_registry.destroy(gone); // a target that is no entity any more
    const entt::entity bareBody = m_registry.create();
    m_registry.emplace<BRITE::RigidBodyComponent>(bareBody); // a body the world never makes: it has no transform
    const entt::entity nothing = entt::null;

    for (const entt::entity target : {noBody, gone, bareBody, nothing}) {
        const entt::entity holder = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
        AddBox(holder, 16.0f);
        AddDistanceJoint(holder, target, 30.0f);
        AddRevoluteJoint(holder, target, {0.0f, 0.0f, 0.0f});
    }

    Run(10);

    for (const entt::entity holder : m_registry.view<BRITE::DistanceJoint3DComponent>()) {
        EXPECT_EQ(m_registry.get<BRITE::DistanceJoint3DComponent>(holder).RuntimeJoint, BRITE::NullPhysicsHandle);
        EXPECT_EQ(m_registry.get<BRITE::RevoluteJoint3DComponent>(holder).RuntimeJoint, BRITE::NullPhysicsHandle);
        EXPECT_EQ(m_backend.GetJointForce(m_registry, holder), 0.0f);
    }
}

TEST_F(Box3DBackendTest, ADistanceJointPastItsBreakForceBreaksAndSaysSoAndTheBodyFallsFree) {
    Listen();
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddSphere(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f).BreakForce = 0.001f;

    Run(60);

    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJoint3DComponent>(bob)) << "the joint went with its break";
    ASSERT_EQ(m_broken.size(), 1u);
    EXPECT_EQ(m_broken[0].EntityA, bob);
    EXPECT_EQ(m_broken[0].EntityB, anchor);
    EXPECT_GT(m_broken[0].FinalForceMagnitude, 0.001f);
    EXPECT_GT(Distance(bob, anchor), 100.0f) << "no longer held to the 30 px of a pendulum";
}

TEST_F(Box3DBackendTest, ARevoluteJointPastItsBreakForceBreaksAndSaysSo) {
    Listen();
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddBox(bob, 16.0f);
    AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f, 0.0f}).BreakForce = 0.001f;

    Run(60);

    EXPECT_FALSE(m_registry.all_of<BRITE::RevoluteJoint3DComponent>(bob));
    ASSERT_EQ(m_broken.size(), 1u);
    EXPECT_EQ(m_broken[0].EntityA, bob);
    EXPECT_EQ(m_broken[0].EntityB, anchor);
    EXPECT_GT(Distance(bob, anchor), 100.0f);
}

TEST_F(Box3DBackendTest, AJointBreaksJustTheSameWithNobodyListening) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddSphere(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f).BreakForce = 0.001f;

    Run(60);

    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJoint3DComponent>(bob));
    EXPECT_EQ(m_registry.ctx().find<entt::dispatcher>(), nullptr) << "a break makes no dispatcher";
}

TEST_F(Box3DBackendTest, AJointWithNoBreakForceOrOneAboveItsLoadHoldsForGood) {
    Listen();
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity unbreakable = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddSphere(unbreakable, 6.0f);
    AddDistanceJoint(unbreakable, anchor, 30.0f); // BreakForce 0 is no limit
    const entt::entity strong = AddBody(BodyType::Dynamic, -30.0f, 300.0f);
    AddSphere(strong, 6.0f);
    AddDistanceJoint(strong, anchor, 30.0f).BreakForce = 100000.0f;

    Run(240);

    EXPECT_TRUE(m_registry.all_of<BRITE::DistanceJoint3DComponent>(unbreakable));
    EXPECT_TRUE(m_registry.all_of<BRITE::DistanceJoint3DComponent>(strong));
    EXPECT_TRUE(m_broken.empty());
    EXPECT_LT(Distance(unbreakable, anchor), 31.5f);
}

TEST_F(Box3DBackendTest, AJointForceIsTheWeightAHangingBodyPullsOnItsDistanceJoint) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 270.0f);
    AddSphere(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f);

    Run(120);

    const float weight = b3Body_GetMass(Body(bob)) * GRAVITY;
    EXPECT_NEAR(m_backend.GetJointForce(m_registry, bob), weight, 0.1f * weight);
}

TEST_F(Box3DBackendTest, AJointForceIsTheWeightAHangingBodyPullsOnItsRevoluteJoint) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 0.0f, 270.0f);
    AddBox(bob, 16.0f);
    AddRevoluteJoint(bob, anchor, {0.0f, 30.0f, 0.0f});

    Run(120);

    const float weight = b3Body_GetMass(Body(bob)) * GRAVITY;
    EXPECT_NEAR(m_backend.GetJointForce(m_registry, bob), weight, 0.1f * weight);
}

TEST_F(Box3DBackendTest, AnEntityWithNoJointFeelsNoJointForce) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f);
    Run(5);

    EXPECT_EQ(m_backend.GetJointForce(m_registry, body), 0.0f);
}

TEST_F(Box3DBackendTest, ADestroyedDistanceJointIsGoneFromTheEntityAndTheBodyIsFree) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddSphere(bob, 6.0f);
    AddDistanceJoint(bob, anchor, 30.0f);
    Run(10);
    ASSERT_GT(m_backend.GetJointForce(m_registry, bob), 0.0f);

    m_backend.DestroyJoint(m_registry, bob);

    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJoint3DComponent>(bob));
    EXPECT_EQ(m_backend.GetJointForce(m_registry, bob), 0.0f);
    Run(60);
    EXPECT_GT(Distance(bob, anchor), 100.0f);
}

TEST_F(Box3DBackendTest, ADestroyedRevoluteJointIsGoneFromTheEntityAndTheBodyIsFree) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity bob = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddBox(bob, 16.0f);
    AddRevoluteJoint(bob, anchor, {-30.0f, 0.0f, 0.0f});
    Run(10);
    ASSERT_GT(m_backend.GetJointForce(m_registry, bob), 0.0f);

    m_backend.DestroyJoint(m_registry, bob);

    EXPECT_FALSE(m_registry.all_of<BRITE::RevoluteJoint3DComponent>(bob));
    EXPECT_EQ(m_backend.GetJointForce(m_registry, bob), 0.0f);
    Run(60);
    EXPECT_GT(Distance(bob, anchor), 100.0f);
}

TEST_F(Box3DBackendTest, DestroyingJointsTakesEveryJointOfTheEntityAndAJointNotYetMadeToo) {
    const entt::entity anchor = AddBody(BodyType::Static, 0.0f, 300.0f);
    const entt::entity both = AddBody(BodyType::Dynamic, 30.0f, 300.0f);
    AddSphere(both, 6.0f);
    AddDistanceJoint(both, anchor, 30.0f);
    AddRevoluteJoint(both, anchor, {-30.0f, 0.0f, 0.0f});
    const entt::entity bare = AddBody(BodyType::Dynamic, 0.0f, 0.0f);
    Run(5);
    const entt::entity unmade = AddBody(BodyType::Dynamic, 0.0f, 0.0f);
    AddDistanceJoint(unmade, anchor, 30.0f); // and no PreStep after it, so no joint behind it
    ASSERT_EQ(m_registry.get<BRITE::DistanceJoint3DComponent>(unmade).RuntimeJoint, BRITE::NullPhysicsHandle);

    m_backend.DestroyJoint(m_registry, both);
    m_backend.DestroyJoint(m_registry, bare); // nothing to take: no harm done
    m_backend.DestroyJoint(m_registry, unmade);

    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJoint3DComponent>(both));
    EXPECT_FALSE(m_registry.all_of<BRITE::RevoluteJoint3DComponent>(both));
    EXPECT_FALSE(m_registry.all_of<BRITE::DistanceJoint3DComponent>(unmade));
    EXPECT_TRUE(m_registry.valid(bare));
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

TEST_F(Box3DBackendTest, AMaterialSetReachesTheShapesOfABodyAlreadyMade) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f);
    AddSphere(body, 8.0f);
    Tick();

    const auto material = std::make_shared<PhysicsMaterial>(PhysicsMaterial{3.0f, 0.9f, 0.5f});
    m_backend.SetMaterial(m_registry, body, material);

    const auto& box = m_registry.get<BRITE::BoxCollider3DComponent>(body);
    const auto& sphere = m_registry.get<BRITE::SphereColliderComponent>(body);
    EXPECT_EQ(box.Material, material);
    EXPECT_EQ(sphere.Material, material);
    for (const BRITE::PhysicsHandle handle : {box.RuntimeShape, sphere.RuntimeShape}) {
        const b3ShapeId shape = Box3DBackend::GetB3Shape(handle);
        ASSERT_TRUE(b3Shape_IsValid(shape));
        EXPECT_EQ(b3Shape_GetDensity(shape), 3.0f);
        EXPECT_EQ(b3Shape_GetFriction(shape), 0.9f);
        EXPECT_EQ(b3Shape_GetRestitution(shape), 0.5f);
    }
}

TEST_F(Box3DBackendTest, AMaterialSetBeforeTheShapeIsMadeIsTheMaterialItIsMadeWith) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    AddBox(body, 16.0f);
    AddSphere(body, 8.0f);
    const auto material = std::make_shared<PhysicsMaterial>(PhysicsMaterial{3.0f, 0.9f, 0.5f});

    m_backend.SetMaterial(m_registry, body, material);
    EXPECT_EQ(m_registry.get<BRITE::BoxCollider3DComponent>(body).Material, material);
    EXPECT_EQ(m_registry.get<BRITE::SphereColliderComponent>(body).Material, material);
    Tick();

    const b3ShapeId box = Box3DBackend::GetB3Shape(m_registry.get<BRITE::BoxCollider3DComponent>(body).RuntimeShape);
    const b3ShapeId sphere =
        Box3DBackend::GetB3Shape(m_registry.get<BRITE::SphereColliderComponent>(body).RuntimeShape);
    EXPECT_EQ(b3Shape_GetFriction(box), 0.9f);
    EXPECT_EQ(b3Shape_GetRestitution(sphere), 0.5f);
}

TEST_F(Box3DBackendTest, AMaterialSetOnABodyWithNoColliderChangesNothing) {
    const entt::entity body = AddBody(BodyType::Dynamic, 0.0f, 300.0f);
    Tick();

    m_backend.SetMaterial(m_registry, body, std::make_shared<PhysicsMaterial>());

    EXPECT_FALSE(m_registry.all_of<BRITE::BoxCollider3DComponent>(body));
    EXPECT_FALSE(m_registry.all_of<BRITE::SphereColliderComponent>(body));
}

TEST_F(Box3DBackendTest, ABouncierMaterialSetAfterTheBodyIsMadeIsABouncierBody) {
    AddGround();
    const entt::entity dead = AddBody(BodyType::Dynamic, -200.0f, 100.0f);
    AddSphere(dead, 16.0f);
    const entt::entity lively = AddBody(BodyType::Dynamic, 200.0f, 100.0f);
    AddSphere(lively, 16.0f);
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
