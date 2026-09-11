#include <gtest/gtest.h>
#include <cstddef>
#include "core/RustBridge.h"

// ABI layout guards: repr(C) on both sides must stay in sync.
// If these fail, the Rust struct changed and alpha_bridge.h is stale.
static_assert(sizeof(RustBridge::FfiDigState) == sizeof(FfiDigState));
static_assert(sizeof(RustBridge::FfiDigInput) == sizeof(FfiDigInput));
static_assert(offsetof(RustBridge::FfiDigState, cur_damage) == 0);

class DiggingTest : public ::testing::Test {
protected:
    RustBridge::FfiDigInput inp(int block, int held = 0) {
        return RustBridge::FfiDigInput{block, held, false, true};
    }
};

TEST_F(DiggingTest, ClickInstantBreakTorch) {
    EXPECT_TRUE(RustBridge::digOnClick(inp(50)));
}

TEST_F(DiggingTest, ClickStoneNotInstant) {
    EXPECT_FALSE(RustBridge::digOnClick(inp(1, 278)));
}

TEST_F(DiggingTest, ClickAirNeverBreaks) {
    EXPECT_FALSE(RustBridge::digOnClick(inp(0, 278)));
}

TEST_F(DiggingTest, TickStoneDiamondPickTakesSixTicks) {
    auto s = RustBridge::digStateNew();
    auto i = inp(1, 278);
    // First tick latches target, then 6 progress ticks (0.1777/tick).
    EXPECT_FALSE(RustBridge::digOnTick(&s, 10, 64, 10, i));
    for (int k = 0; k < 5; ++k) {
        EXPECT_FALSE(RustBridge::digOnTick(&s, 10, 64, 10, i));
    }
    EXPECT_TRUE(RustBridge::digOnTick(&s, 10, 64, 10, i));
    EXPECT_EQ(s.initial_cooldown, 5);
    EXPECT_FLOAT_EQ(s.cur_damage, 0.0f);
}

TEST_F(DiggingTest, TickCooldownBlocksProgress) {
    auto s = RustBridge::digStateNew();
    s.has_target = true;
    s.target_x = 1; s.target_y = 2; s.target_z = 3;
    s.initial_cooldown = 2;
    auto i = inp(50);
    EXPECT_FALSE(RustBridge::digOnTick(&s, 1, 2, 3, i));
    EXPECT_EQ(s.initial_cooldown, 1);
    EXPECT_FALSE(RustBridge::digOnTick(&s, 1, 2, 3, i));
    EXPECT_EQ(s.initial_cooldown, 0);
}

TEST_F(DiggingTest, TickTargetSwitchResets) {
    auto s = RustBridge::digStateNew();
    auto i = inp(1, 278);
    EXPECT_FALSE(RustBridge::digOnTick(&s, 0, 64, 0, i));
    EXPECT_FALSE(RustBridge::digOnTick(&s, 0, 64, 0, i));
    EXPECT_GT(s.cur_damage, 0.0f);
    EXPECT_FALSE(RustBridge::digOnTick(&s, 5, 64, 5, i));
    EXPECT_FLOAT_EQ(s.cur_damage, 0.0f);
    EXPECT_EQ(s.target_x, 5);
}

TEST_F(DiggingTest, TickBedrockNeverBreaks) {
    auto s = RustBridge::digStateNew();
    auto i = inp(7, 278);
    EXPECT_FALSE(RustBridge::digOnTick(&s, 0, 1, 0, i));
    for (int k = 0; k < 50; ++k) {
        EXPECT_FALSE(RustBridge::digOnTick(&s, 0, 1, 0, i));
    }
}

TEST_F(DiggingTest, CancelResets) {
    auto s = RustBridge::digStateNew();
    s.cur_damage = 0.5f;
    s.initial_cooldown = 3;
    RustBridge::digCancel(&s);
    EXPECT_FLOAT_EQ(s.cur_damage, 0.0f);
    EXPECT_EQ(s.initial_cooldown, 0);
}

class TrackerMathTest : public ::testing::Test {};

TEST_F(TrackerMathTest, EncodePosTruncatesLikeCppCast) {
    EXPECT_EQ(RustBridge::trackerEncodePos(10.5), 336);
    EXPECT_EQ(RustBridge::trackerEncodePos(0.0), 0);
    EXPECT_EQ(RustBridge::trackerEncodePos(-10.5), -336);
}

TEST_F(TrackerMathTest, EncodeRotWrapsLikeCpp) {
    EXPECT_EQ(RustBridge::trackerEncodeRot(0.0f), 0);
    EXPECT_EQ(RustBridge::trackerEncodeRot(90.0f), 64);
    EXPECT_EQ(RustBridge::trackerEncodeRot(360.0f), 0);
    EXPECT_EQ(RustBridge::trackerEncodeRot(-1.0f), -1);
}

TEST_F(TrackerMathTest, MoveKindSelection) {
    EXPECT_EQ(RustBridge::trackerMoveKind(5, 0, 0, true, true), 3);
    EXPECT_EQ(RustBridge::trackerMoveKind(5, 0, 0, true, false), 1);
    EXPECT_EQ(RustBridge::trackerMoveKind(0, 0, 0, false, true), 2);
    EXPECT_EQ(RustBridge::trackerMoveKind(0, 0, 0, false, false), 0);
    EXPECT_EQ(RustBridge::trackerMoveKind(128, 0, 0, true, false), 4);
    EXPECT_EQ(RustBridge::trackerMoveKind(-129, 0, 0, true, false), 4);
    EXPECT_EQ(RustBridge::trackerMoveKind(127, 127, 127, true, false), 1);
}

TEST_F(TrackerMathTest, VelocityChanged) {
    EXPECT_FALSE(RustBridge::trackerVelocityChanged(0, 0, 0, 0, 0, 0, false));
    EXPECT_FALSE(RustBridge::trackerVelocityChanged(1, 0, 0, 1, 0, 0, true));
    EXPECT_TRUE(RustBridge::trackerVelocityChanged(1, 0, 0, 0, 0, 0, true));
    EXPECT_FALSE(RustBridge::trackerVelocityChanged(0.01, 0, 0, 0, 0, 0, true));
    EXPECT_TRUE(RustBridge::trackerVelocityChanged(0, 0, 0, 0.5, 0, 0, true));
}

TEST_F(TrackerMathTest, InRangeXzOnly) {
    EXPECT_TRUE(RustBridge::trackerInRange(10.0, 10.0, 0, 0, 64));
    EXPECT_FALSE(RustBridge::trackerInRange(100.0, 0.0, 0, 0, 64));
    EXPECT_TRUE(RustBridge::trackerInRange(64.0, 0.0, 0, 0, 64));
    EXPECT_FALSE(RustBridge::trackerInRange(64.1, 0.0, 0, 0, 64));
}
