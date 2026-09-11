#include <gtest/gtest.h>
#include "core/RustBridge.h"

// Small-entity seam: item/falling/boat/arrow kernels in Rust (entity_misc.rs).

TEST(EntityMiscTest, PushSideSelectsNearestFreeFace) {
    EXPECT_EQ(RustBridge::itemPushSide(true, false, false, false, false, false, 0.1, 0.5, 0.5), 0);
    EXPECT_EQ(RustBridge::itemPushSide(false, false, false, false, false, false, 0.1, 0.5, 0.5), -1);
    EXPECT_EQ(RustBridge::itemPushSide(true, true, false, false, false, false, 0.9, 0.5, 0.5), 1);
}

TEST(EntityMiscTest, ItemDampGroundBounce) {
    RustBridge::ItemMotion m{1.0, -2.0, 1.0};
    ASSERT_TRUE(RustBridge::itemDamp(true, &m));
    EXPECT_NEAR(m.mx, 0.588, 1e-6);
    EXPECT_NEAR(m.my, 0.98, 1e-6);
}

TEST(EntityMiscTest, FallingLandDecisions) {
    EXPECT_EQ(RustBridge::fallingLand(0, false, 60, 0, false, true, 0), 3);
    EXPECT_EQ(RustBridge::fallingLand(12, false, 60, 0, false, true, 0), 0);
    EXPECT_EQ(RustBridge::fallingLand(12, false, 60, 0, false, true, 101), 2);
    EXPECT_EQ(RustBridge::fallingLand(12, true, 60, 0, false, true, 5), 1);
    EXPECT_EQ(RustBridge::fallingLand(12, true, 60, 3, false, true, 5), 2);
}

TEST(EntityMiscTest, BoatSteerClampsTo20) {
    float yaw = 0.0f;
    EXPECT_FALSE(RustBridge::boatSteer(0.0, 0.0, 10.0f, &yaw));
    EXPECT_FLOAT_EQ(yaw, 0.0f);
    ASSERT_TRUE(RustBridge::boatSteer(1.0, 0.0, 0.0f, &yaw));
    EXPECT_FLOAT_EQ(yaw, -20.0f);
}

TEST(EntityMiscTest, BoatRiderOffsetFacesEast) {
    double ox = 0.0;
    double oz = 0.0;
    ASSERT_TRUE(RustBridge::boatRiderOffset(0.0f, &ox, &oz));
    EXPECT_NEAR(ox, 0.4, 1e-9);
    EXPECT_NEAR(oz, 0.0, 1e-9);
}

TEST(EntityMiscTest, ArrowFacesVelocity) {
    float yaw = 0.0f;
    float pitch = 0.0f;
    ASSERT_TRUE(RustBridge::arrowFaceVelocity(0.0, 0.0, 2.0, &yaw, &pitch));
    EXPECT_NEAR(yaw, 0.0f, 1e-6);
    EXPECT_NEAR(pitch, 0.0f, 1e-6);
}
