#include <gtest/gtest.h>
#include "core/RustBridge.h"

// Living-entity seam: EntityLiving::heal/attackEntityFrom/tick/
// moveEntityWithHeading delegate to Rust (entity_living.rs).

TEST(LivingTest, HealClamps) {
    EXPECT_EQ(RustBridge::livingHeal(10, 20, 5, false), 15);
    EXPECT_EQ(RustBridge::livingHeal(18, 20, 5, false), 20);
    EXPECT_EQ(RustBridge::livingHeal(10, 20, 0, false), 10);
    EXPECT_EQ(RustBridge::livingHeal(0, 20, 5, false), 0);
}

TEST(LivingTest, AttackFreshHit) {
    RustBridge::AttackResult r{};
    ASSERT_TRUE(RustBridge::livingAttack(20, 0, 20, 0, 0, 0, false, 6, false,
                                         0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 3.0, &r));
    EXPECT_EQ(r.health, 14);
    EXPECT_EQ(r.last_damage, 6);
    EXPECT_EQ(r.hurt_resist, 20);
    EXPECT_EQ(r.hurt_time, 10);
    EXPECT_FALSE(r.knocked);
    EXPECT_FALSE(r.died);
}

TEST(LivingTest, AttackResistWindow) {
    RustBridge::AttackResult r{};
    EXPECT_FALSE(RustBridge::livingAttack(14, 15, 20, 6, 0, 0, false, 5, false,
                                          0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, &r));
    ASSERT_TRUE(RustBridge::livingAttack(14, 15, 20, 6, 4, 7, false, 9, false,
                                         0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, &r));
    EXPECT_EQ(r.health, 11);
    // Untouched timers round-trip.
    EXPECT_EQ(r.hurt_time, 4);
    EXPECT_EQ(r.attack_time, 7);
}

TEST(LivingTest, TickDrownsAtLimit) {
    const RustBridge::LivingTick t =
        RustBridge::livingTick(true, false, true, -19, 0, 0, 0);
    EXPECT_EQ(t.air, 0);
    EXPECT_TRUE(t.drown);
    EXPECT_FALSE(t.suffocate);
}

TEST(LivingTest, FallDamageTable) {
    EXPECT_EQ(RustBridge::livingFallDamage(3.0f), 0);
    EXPECT_EQ(RustBridge::livingFallDamage(3.5f), 1);
    EXPECT_EQ(RustBridge::livingFallDamage(10.0f), 7);
}
