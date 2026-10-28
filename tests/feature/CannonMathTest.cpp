#include <gtest/gtest.h>

#include "feature/cannon/CannonMath.h"

using namespace cm::feature::cannon::math;

namespace {
void expectNear(V3 a, V3 b) {
    EXPECT_NEAR(a.x, b.x, 1e-9);
    EXPECT_NEAR(a.y, b.y, 1e-9);
    EXPECT_NEAR(a.z, b.z, 1e-9);
}
} // namespace

TEST(CannonMath, FloorCannonUsesMinecraftYaw) {
    expectNear(localToWorld(1, 0, 0), {0, 0, 1});   // south
    expectNear(localToWorld(1, 90, 0), {-1, 0, 0}); // west
    expectNear(localToWorld(1, -90, 0), {1, 0, 0}); // east
    expectNear(localToWorld(1, 0, 90), {0, 1, 0});  // straight up
}

TEST(CannonMath, PitchNinetyPointsAwayFromTheBase) {
    for (int face = 0; face < 6; ++face) expectNear(localToWorld(face, 37, 90), faceNormal(face));
}

TEST(CannonMath, LocalWorldRoundTrip) {
    for (int face = 0; face < 6; ++face) {
        for (double yaw = -170; yaw <= 170; yaw += 34) {
            for (double pitch = -20; pitch <= 80; pitch += 20) {
                auto [y, p] = worldToLocal(face, localToWorld(face, yaw, pitch));
                EXPECT_NEAR(y, yaw, 1e-6) << "face " << face;
                EXPECT_NEAR(p, pitch, 1e-6) << "face " << face;
            }
        }
    }
}

TEST(CannonMath, WorldAnglesMatchMinecraft) {
    auto [yaw, pitch] = worldAngles({-1, 0, 0});
    EXPECT_NEAR(yaw, 90, 1e-9);
    EXPECT_NEAR(pitch, 0, 1e-9);
    auto [yaw2, pitch2] = worldAngles(V3{0, 1, 1});
    EXPECT_NEAR(yaw2, 0, 1e-9);
    EXPECT_NEAR(pitch2, 45, 1e-9);
}

TEST(CannonMath, WrapAndClamp) {
    EXPECT_NEAR(wrapYaw(190), -170, 1e-9);
    EXPECT_NEAR(wrapYaw(-190), 170, 1e-9);
    EXPECT_EQ(clampPitch(120), kMaxPitch);
    EXPECT_EQ(clampPitch(-60), kMinPitch);
}

TEST(CannonMath, TrajectoryFallsAndStopsAtHit) {
    bool blocked = false;
    // free flight for 10 ticks: no drag, gravity 0.05
    auto pts = trajectory({0, 10, 0}, {1, 0, 0}, 0.05, 1.0, 10, [](V3, V3) { return 2.0; }, &blocked);
    ASSERT_EQ(pts.size(), 11u);
    EXPECT_FALSE(blocked);
    EXPECT_NEAR(pts.back().x, 10, 1e-9);
    EXPECT_NEAR(pts.back().y, 10 - 0.05 * 45, 1e-9); // sum of 0..9 * 0.05
    // a wall at x = 3.5
    auto hitWall = [](V3 a, V3 b) { return (b.x >= 3.5 && a.x < 3.5) ? (3.5 - a.x) / (b.x - a.x) : 2.0; };
    pts = trajectory({0, 10, 0}, {1, 0, 0}, 0.0, 1.0, 50, hitWall, &blocked);
    EXPECT_TRUE(blocked);
    EXPECT_NEAR(pts.back().x, 3.5, 1e-9);
}

TEST(CannonMath, SphereDirectionsAreUnitAndSpread) {
    auto dirs = sphereDirections(24);
    ASSERT_EQ(dirs.size(), 24u);
    V3 sum{};
    for (auto d : dirs) {
        EXPECT_NEAR(d.length(), 1.0, 1e-9);
        sum = sum + d;
    }
    EXPECT_LT(sum.length(), 1.0); // roughly balanced
}

TEST(CannonMath, RayBox) {
    EXPECT_NEAR(rayBox({0, 0, 0}, {1, 0, 0}, {2, -1, -1}, {3, 1, 1}), 2.0, 1e-9);
    EXPECT_LT(rayBox({0, 0, 0}, {-1, 0, 0}, {2, -1, -1}, {3, 1, 1}), 0.0);
    EXPECT_LT(rayBox({0, 5, 0}, {1, 0, 0}, {2, -1, -1}, {3, 1, 1}), 0.0);
    EXPECT_NEAR(breakBudget(4.0, 3.0), 6.0, 1e-9);
}
