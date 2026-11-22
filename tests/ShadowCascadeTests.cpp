// The cascades of a sun's shadow on the CPU (Backends/ShadowCascades.hpp): where the view is split, the sphere around
// a slice, the texel snap, and which casters reach a cascade. Every expected value is worked by hand above its
// assertion. Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/ShadowCascades.hpp>
#include <gtest/gtest.h>

#include <utility>

namespace Cascades = BRITE::ShadowCascades;

// Two cascades over 0.1..80 at lambda 0.75: the first edge, f = 1/2, blends the logarithmic 0.1 x 800^(1/2) =
// 0.1 x 28.284271 = 2.828427 and the even 0.1 + 79.9 / 2 = 40.05: 0.75 x 2.828427 + 0.25 x 40.05 = 2.121320 + 10.0125
// = 12.133820. The last edge is the distance itself, 80, by both.
// Mutation: the blend's weights swapped (lambda on the even split) -> 0.25 x 2.83 + 0.75 x 40.05 = 30.74, red.
TEST(ShadowCascade, ThePracticalSplitBlendsLogarithmicAndEven) {
    EXPECT_NEAR(Cascades::PracticalSplit(0, 2, 0.1f, 80.0f, 0.75f), 12.133820f, 1e-4f);
    EXPECT_NEAR(Cascades::PracticalSplit(1, 2, 0.1f, 80.0f, 0.75f), 80.0f, 1e-4f);
}

// An explicit split overrides the blend where it lies beyond the edge before it and short of the distance: 25 gives
// {25, 80}. One at or beyond the distance, 90, is ignored: {12.133820, 80}. The last edge is always the distance.
// Mutation: explicit splits never read -> {12.13, 80} for the first, red.
TEST(ShadowCascade, AnExplicitSplitOverridesTheBlendWhereItFits) {
    const auto chosen = Cascades::FarEdges(2, 0.1f, 80.0f, 0.75f, {25.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(chosen[0], 25.0f);
    EXPECT_FLOAT_EQ(chosen[1], 80.0f);
    const auto ignored = Cascades::FarEdges(2, 0.1f, 80.0f, 0.75f, {90.0f, 0.0f, 0.0f});
    EXPECT_NEAR(ignored[0], 12.133820f, 1e-4f);
    EXPECT_FLOAT_EQ(ignored[1], 80.0f);
}

// The sphere around a slice, from the eye at the origin looking down -z.
// Perspective with tan = 1 both ways, slice 1..3: corners (+-1, +-1, -1) and (+-3, +-3, -3), centre (0, 0, -2); the
// far corners are farthest, sqrt(9 + 9 + 1) = 4.358899, which x 16 = 69.74 rounds up to 70: radius 70/16 = 4.375.
// Orthographic, 2 across and 1 up either side, slice 1..3: corners (+-2, +-1, -1) and (+-2, +-1, -3), centre (0, 0,
// -2), every corner sqrt(4 + 1 + 1) = 2.449490 away, x 16 = 39.19, up to 40: 2.5. Mutation: the radius not rounded up
// (ceil -> floor) -> 69/16 = 4.3125, red.
TEST(ShadowCascade, TheSphereAroundASliceOfTheView) {
    const BRITE::Vector3 eye{0, 0, 0}, forward{0, 0, -1}, right{1, 0, 0}, up{0, 1, 0};
    const Cascades::Sphere perspective =
        Cascades::SliceSphere(eye, forward, right, up, 1.0f, 3.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    EXPECT_NEAR(perspective.Centre.z, -2.0f, 1e-5f);
    EXPECT_NEAR(perspective.Centre.x, 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(perspective.Radius, 4.375f);
    const Cascades::Sphere orthographic =
        Cascades::SliceSphere(eye, forward, right, up, 1.0f, 3.0f, 0.0f, 2.0f, 0.0f, 1.0f);
    EXPECT_NEAR(orthographic.Centre.z, -2.0f, 1e-5f);
    EXPECT_FLOAT_EQ(orthographic.Radius, 2.5f);
}

// A texel of a cascade of radius 4.375 on a 2048 map: 8.75 / 2048 = 0.0042724609. The snap moves a coordinate DOWN to
// a whole texel: 1.1 at a texel of 0.25 is 1.0, -0.1 is -0.25, and 0.75, a whole texel already, stays.
// Mutation: the snap rounds to the nearest instead (floor -> round) -> 1.1 stays 1.0 but -0.1 becomes 0, red.
TEST(ShadowCascade, ATexelAndTheSnapToWholeTexels) {
    EXPECT_FLOAT_EQ(Cascades::TexelSize(4.375f, 2048), 0.0042724609f);
    EXPECT_FLOAT_EQ(Cascades::SnapToTexel(1.1f, 0.25f), 1.0f);
    EXPECT_FLOAT_EQ(Cascades::SnapToTexel(-0.1f, 0.25f), -0.25f);
    EXPECT_FLOAT_EQ(Cascades::SnapToTexel(0.75f, 0.25f), 0.75f);
}

// A cascade of radius 10 at the light-space origin. Across the light a caster reaches it while it overlaps the square:
// at x = 12 with radius 3, 12 <= 13; at x = 14, 14 > 13, no. Toward the sun (larger z) a caster always can: at z = 50
// it is kept. Beyond the far side (z < -10) it shades nothing drawn: at z = -12 with radius 1, -11 < -10, no; at -10.5,
// -9.5 >= -10, it still touches the slice.
// Mutation: the far-side test dropped -> the caster at z = -12 is kept, red.
TEST(ShadowCascade, WhichCastersReachACascade) {
    const BRITE::Vector3 centre{0, 0, 0};
    EXPECT_TRUE(Cascades::CasterReachesCascade({12, 0, 0}, 3.0f, centre, 10.0f));
    EXPECT_FALSE(Cascades::CasterReachesCascade({14, 0, 0}, 3.0f, centre, 10.0f));
    EXPECT_FALSE(Cascades::CasterReachesCascade({0, 14, 0}, 3.0f, centre, 10.0f));
    EXPECT_TRUE(Cascades::CasterReachesCascade({0, 0, 50}, 1.0f, centre, 10.0f));
    EXPECT_FALSE(Cascades::CasterReachesCascade({0, 0, -12}, 1.0f, centre, 10.0f));
    EXPECT_TRUE(Cascades::CasterReachesCascade({0, 0, -10.5f}, 1.0f, centre, 10.0f));
}

// The sphere's size does not change as the camera turns: the same slice, 1..3 under tan 1 both ways, looked at along
// -z, along +x and along a diagonal, keeps its radius of 4.375 (worked above), which is what keeps a cascade's texel
// the same size as the camera yaws. Mutation: the radius taken from the slice's box in world axes -> it grows on the
// diagonal, red.
TEST(ShadowCascade, TheSphereKeepsItsSizeAsTheCameraTurns) {
    const float d = 0.70710678f;
    const BRITE::Vector3 up{0, 1, 0};
    for (const auto& [forward, right] : {std::pair{BRITE::Vector3{0, 0, -1}, BRITE::Vector3{1, 0, 0}},
                                         std::pair{BRITE::Vector3{1, 0, 0}, BRITE::Vector3{0, 0, 1}},
                                         std::pair{BRITE::Vector3{d, 0, -d}, BRITE::Vector3{d, 0, d}}})
        EXPECT_FLOAT_EQ(
            Cascades::SliceSphere({5, 2, -7}, forward, right, up, 1.0f, 3.0f, 1.0f, 0.0f, 1.0f, 0.0f).Radius, 4.375f);
}
