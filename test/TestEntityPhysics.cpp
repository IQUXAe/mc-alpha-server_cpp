#include <gtest/gtest.h>
#include <cmath>
#include "core/RustBridge.h"

// Entity physics seam: Entity::moveEntity/updateFallState/applyEntityCollision
// delegate resolution math to Rust (entity_physics.rs).

namespace {

RustBridge::FfiAabb makeBox(double x0, double y0, double z0, double x1, double y1, double z1) {
    return RustBridge::FfiAabb{x0, y0, z0, x1, y1, z1};
}

} // namespace

TEST(EntityPhysicsTest, FallOntoFloorStops) {
    const RustBridge::FfiAabb entity = makeBox(-0.3, 2.0, -0.3, 0.3, 3.8, 0.3);
    const RustBridge::FfiAabb floor = makeBox(-8.0, 0.0, -8.0, 8.0, 1.0, 8.0);
    RustBridge::ResolvedMove out{};
    ASSERT_TRUE(RustBridge::entityResolveMove(entity, 0.0, -5.0, 0.0, &floor, 1, &out));
    EXPECT_DOUBLE_EQ(out.dy, -1.0);
    EXPECT_DOUBLE_EQ(out.box_.min_y, 1.0);
}

TEST(EntityPhysicsTest, WallBlocksXOnly) {
    const RustBridge::FfiAabb entity = makeBox(0.0, 1.0, 0.0, 0.6, 2.8, 0.6);
    const RustBridge::FfiAabb wall = makeBox(1.0, 0.0, -8.0, 2.0, 4.0, 8.0);
    RustBridge::ResolvedMove out{};
    ASSERT_TRUE(RustBridge::entityResolveMove(entity, 2.0, -0.5, 0.3, &wall, 1, &out));
    EXPECT_NEAR(out.dx, 0.4, 1e-9);
    EXPECT_DOUBLE_EQ(out.dy, -0.5);
    EXPECT_DOUBLE_EQ(out.dz, 0.3);
}

TEST(EntityPhysicsTest, FallStepLandingFiresEvent) {
    float event = -1.0f;
    const float next = RustBridge::entityFallStep(true, -3.0, 3.5f, &event);
    EXPECT_FLOAT_EQ(next, 0.0f);
    EXPECT_FLOAT_EQ(event, 3.5f);
}

TEST(EntityPhysicsTest, FallStepAccumulatesInAir) {
    float event = -1.0f;
    const float next = RustBridge::entityFallStep(false, -2.0, 1.5f, &event);
    EXPECT_FLOAT_EQ(event, -1.0f);
    EXPECT_FLOAT_EQ(next, 3.5f);
}

TEST(EntityPhysicsTest, PushImpulseMatchesFormula) {
    RustBridge::PushOut out{};
    ASSERT_TRUE(RustBridge::entityPush(0.0, 0.0, 3.0, 4.0, true, true, &out));
    EXPECT_NEAR(out.dvx2, 0.01875, 1e-9);
    EXPECT_NEAR(out.dvz2, 0.025, 1e-9);
    EXPECT_NEAR(out.dvx1, -0.01875, 1e-9);
    EXPECT_FALSE(RustBridge::entityPush(0.0, 0.0, 0.005, 0.0, true, true, &out));
}
