#include <gtest/gtest.h>
#include "core/RustBridge.h"

// Spawn math parity: World::spawnHostileMobs/spawnPassiveMobs delegate
// cap, pack-spread step, and world-spawn exclusion to Rust (mob_spawning.rs).

TEST(MobSpawningTest, MaxCountHostileBudget) {
    // 100 per 256 eligible chunks.
    EXPECT_EQ(RustBridge::spawnMaxCount(256, 100), 100);
    EXPECT_EQ(RustBridge::spawnMaxCount(128, 100), 50);
    EXPECT_EQ(RustBridge::spawnMaxCount(0, 100), 0);
    // Truncation like C++ int division.
    EXPECT_EQ(RustBridge::spawnMaxCount(1, 100), 0);
    EXPECT_EQ(RustBridge::spawnMaxCount(3, 100), 1);
}

TEST(MobSpawningTest, MaxCountPassiveBudget) {
    // 20 per 256 eligible chunks.
    EXPECT_EQ(RustBridge::spawnMaxCount(256, 20), 20);
    EXPECT_EQ(RustBridge::spawnMaxCount(128, 20), 10);
    EXPECT_EQ(RustBridge::spawnMaxCount(0, 20), 0);
}

TEST(MobSpawningTest, PackOffsetShape) {
    EXPECT_EQ(RustBridge::spawnPackOffset(5, 0), 5);
    EXPECT_EQ(RustBridge::spawnPackOffset(0, 5), -5);
    EXPECT_EQ(RustBridge::spawnPackOffset(3, 3), 0);
    for (int a = 0; a < 6; ++a) {
        for (int b = 0; b < 6; ++b) {
            const int d = RustBridge::spawnPackOffset(a, b);
            EXPECT_GE(d, -5);
            EXPECT_LE(d, 5);
        }
    }
}

TEST(MobSpawningTest, TooCloseToSpawn) {
    EXPECT_TRUE(RustBridge::spawnTooCloseToSpawn(0.5f, 64.0f, 0.5f, 0, 64, 0));
    EXPECT_FALSE(RustBridge::spawnTooCloseToSpawn(100.5f, 64.0f, 100.5f, 0, 64, 0));
    // Boundary exclusive: exactly 24 blocks (576.0) is allowed.
    EXPECT_FALSE(RustBridge::spawnTooCloseToSpawn(24.0f, 64.0f, 0.0f, 0, 64, 0));
    EXPECT_TRUE(RustBridge::spawnTooCloseToSpawn(23.5f, 64.0f, 0.0f, 0, 64, 0));
}
