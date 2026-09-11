#include <gtest/gtest.h>
#include "core/RustBridge.h"

// Creature AI seam: EntityCreature::faceEntity/followPath steering
// delegates angle math to Rust (entity_ai.rs).

TEST(CreatureAiTest, ClampAngle) {
    EXPECT_FLOAT_EQ(RustBridge::aiClampAngle(0.0f, 10.0f, 30.0f), 10.0f);
    EXPECT_FLOAT_EQ(RustBridge::aiClampAngle(0.0f, 100.0f, 30.0f), 30.0f);
    EXPECT_FLOAT_EQ(RustBridge::aiClampAngle(170.0f, -170.0f, 30.0f), 190.0f);
}

TEST(CreatureAiTest, FaceEastTurnLimited) {
    float yaw = 0.0f;
    float pitch = 0.0f;
    ASSERT_TRUE(RustBridge::aiFaceAngles(5.0, 0.0, 0.0, 0.0f, 0.0f, 30.0f, &yaw, &pitch));
    EXPECT_FLOAT_EQ(yaw, -30.0f);
}

TEST(CreatureAiTest, SteerStraightKeepsYaw) {
    RustBridge::SteerOut out{};
    ASSERT_TRUE(RustBridge::aiSteerToPoint(5.0, 0.0, 0.0, -90.0f, false, false,
                                           0.0, 0.0, 0.0f, &out));
    EXPECT_NEAR(out.new_yaw, -90.0f, 1e-4);
    EXPECT_FALSE(out.jump);
}

TEST(CreatureAiTest, SteerUpFlagsJump) {
    RustBridge::SteerOut out{};
    ASSERT_TRUE(RustBridge::aiSteerToPoint(1.0, 0.0, 2.0, -90.0f, false, false,
                                           0.0, 0.0, 0.0f, &out));
    EXPECT_TRUE(out.jump);
}
