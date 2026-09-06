#include <gtest/gtest.h>
#include "core/RustBridge.h"
#include "core/InventoryPlayer.h"
#include "core/ItemStack.h"
#include "core/Item.h"
#include "block/Block.h"

class PlayerLogicTest : public ::testing::Test {
};

// 1. Inventory & 2x2 Crafting Tests
TEST_F(PlayerLogicTest, InventoryStackingAndSlots) {
    InventoryPlayer inv(nullptr);

    // Initial slot contains 50 stone blocks (max 64)
    inv.mainInventory[0] = std::make_unique<ItemStack>(1, 50, 0);

    // Add 20 more stone blocks: should top up slot 0 to 64, and put remaining 6 in slot 1
    ItemStack addStack(1, 20, 0);
    int rem = inv.addItemsToInventory(&addStack);

    EXPECT_EQ(rem, 0);
    EXPECT_EQ(addStack.stackSize, 0);
    ASSERT_NE(inv.mainInventory[0], nullptr);
    EXPECT_EQ(inv.mainInventory[0]->stackSize, 64);
    ASSERT_NE(inv.mainInventory[1], nullptr);
    EXPECT_EQ(inv.mainInventory[1]->itemID, 1);
    EXPECT_EQ(inv.mainInventory[1]->stackSize, 6);
}

TEST_F(PlayerLogicTest, InventoryNonStackableItems) {
    InventoryPlayer inv(nullptr);

    // Add 2 diamond swords (itemID 276)
    ItemStack swords(276, 2, 0);
    int rem = inv.addItemsToInventory(&swords);

    EXPECT_EQ(rem, 0);
    ASSERT_NE(inv.mainInventory[0], nullptr);
    EXPECT_EQ(inv.mainInventory[0]->itemID, 276);
    EXPECT_EQ(inv.mainInventory[0]->stackSize, 1);
    ASSERT_NE(inv.mainInventory[1], nullptr);
    EXPECT_EQ(inv.mainInventory[1]->itemID, 276);
    EXPECT_EQ(inv.mainInventory[1]->stackSize, 1);
}

TEST_F(PlayerLogicTest, ArmorDefenseCalculation) {
    InventoryPlayer inv(nullptr);

    // Full diamond armor: helmet(310), chest(311), legs(312), boots(313)
    inv.armorInventory[0] = std::make_unique<ItemStack>(310, 1, 0);
    inv.armorInventory[1] = std::make_unique<ItemStack>(311, 1, 0);
    inv.armorInventory[2] = std::make_unique<ItemStack>(312, 1, 0);
    inv.armorInventory[3] = std::make_unique<ItemStack>(313, 1, 0);

    int defense = inv.getTotalArmorValue();
    EXPECT_EQ(defense, 20); // 3 + 8 + 6 + 3 = 20 points
}

TEST_F(PlayerLogicTest, ArmorDegradationAndBreakage) {
    InventoryPlayer inv(nullptr);

    // Leather helmet (298): maxDamage = 33
    inv.armorInventory[0] = std::make_unique<ItemStack>(298, 1, 30);

    // Damage armor by 5: 30 + 5 = 35 > 33 -> broken!
    inv.damageArmor(5);

    EXPECT_EQ(inv.armorInventory[0], nullptr);
}

TEST_F(PlayerLogicTest, Crafting2x2Grid) {
    InventoryPlayer inv(nullptr);

    // Wood log (17) in slot 0 -> 4 Planks (5)
    inv.craftingInventory[0] = std::make_unique<ItemStack>(17, 1, 0);
    ItemStack result = inv.getCraftingResult();
    EXPECT_EQ(result.itemID, 5);
    EXPECT_EQ(result.stackSize, 4);

    inv.consumeCraftingIngredients();
    EXPECT_EQ(inv.craftingInventory[0], nullptr);

    // 4 Planks -> Workbench (58)
    inv.craftingInventory[0] = std::make_unique<ItemStack>(5, 1, 0);
    inv.craftingInventory[1] = std::make_unique<ItemStack>(5, 1, 0);
    inv.craftingInventory[2] = std::make_unique<ItemStack>(5, 1, 0);
    inv.craftingInventory[3] = std::make_unique<ItemStack>(5, 1, 0);

    ItemStack wb = inv.getCraftingResult();
    EXPECT_EQ(wb.itemID, 58);
    EXPECT_EQ(wb.stackSize, 1);
}

// 2. Combat Tests
TEST_F(PlayerLogicTest, CombatDifficultyScaling) {
    // Mob attacking player (attackerIsPlayer = false)
    // Peaceful (0) -> 0 damage
    auto resPeaceful = RustBridge::calculateCombatDamage(10, false, 0, 0, 0);
    EXPECT_EQ(resPeaceful.scaled_damage, 0);
    EXPECT_EQ(resPeaceful.damage_after_armor, 0);

    // Easy (1) -> 10 / 3 + 1 = 4
    auto resEasy = RustBridge::calculateCombatDamage(10, false, 1, 0, 0);
    EXPECT_EQ(resEasy.scaled_damage, 4);
    EXPECT_EQ(resEasy.damage_after_armor, 4);

    // Normal (2) -> 10
    auto resNormal = RustBridge::calculateCombatDamage(10, false, 2, 0, 0);
    EXPECT_EQ(resNormal.scaled_damage, 10);
    EXPECT_EQ(resNormal.damage_after_armor, 10);

    // Hard (3) -> 10 * 3 / 2 = 15
    auto resHard = RustBridge::calculateCombatDamage(10, false, 3, 0, 0);
    EXPECT_EQ(resHard.scaled_damage, 15);
    EXPECT_EQ(resHard.damage_after_armor, 15);

    // PvP: ignores difficulty scaling
    auto resPvP = RustBridge::calculateCombatDamage(10, true, 0, 0, 0);
    EXPECT_EQ(resPvP.scaled_damage, 10);
    EXPECT_EQ(resPvP.damage_after_armor, 10);
}

TEST_F(PlayerLogicTest, CombatArmorAbsorption) {
    // 20 armor points, 10 damage:
    // scaled = 10 * (25 - 20) + 0 = 50
    // damage_after_armor = 50 / 25 = 2, carry = 0
    auto res = RustBridge::calculateCombatDamage(10, false, 2, 20, 0);
    EXPECT_EQ(res.damage_after_armor, 2);
    EXPECT_EQ(res.new_armor_damage_carry, 0);
}

TEST_F(PlayerLogicTest, WeaponDamageValues) {
    EXPECT_EQ(RustBridge::getWeaponDamage(0), 1);   // Bare hand
    EXPECT_EQ(RustBridge::getWeaponDamage(268), 4); // Wood sword
    EXPECT_EQ(RustBridge::getWeaponDamage(272), 6); // Stone sword
    EXPECT_EQ(RustBridge::getWeaponDamage(267), 8); // Iron sword
    EXPECT_EQ(RustBridge::getWeaponDamage(276), 10);// Diamond sword
    EXPECT_EQ(RustBridge::getWeaponDamage(283), 4); // Gold sword

    EXPECT_EQ(RustBridge::getWeaponDamage(279), 6); // Diamond axe
    EXPECT_EQ(RustBridge::getWeaponDamage(278), 5); // Diamond pickaxe
    EXPECT_EQ(RustBridge::getWeaponDamage(277), 4); // Diamond shovel
}

// 3. Movement & Fall Damage Tests
TEST_F(PlayerLogicTest, MovementValidationStanceAndBounds) {
    // Valid movement
    RustBridge::FfiMovementInput validMove{
        .from_x = 10.0, .from_y = 64.0, .from_z = 10.0,
        .to_x = 10.2, .to_y = 64.0, .to_z = 10.0,
        .stance = 65.62,
        .on_ground = true,
        .is_in_water = false,
        .fall_distance = 0.0f,
    };
    auto validRes = RustBridge::validateMovement(validMove);
    EXPECT_EQ(validRes.status, 0); // Ok

    // Illegal stance (< 0.1)
    RustBridge::FfiMovementInput badStance = validMove;
    badStance.stance = 64.0; // diff = 0.0
    auto stanceRes = RustBridge::validateMovement(badStance);
    EXPECT_EQ(stanceRes.status, 1); // IllegalStance

    // Illegal position (> 3.2e7)
    RustBridge::FfiMovementInput badPos = validMove;
    badPos.to_x = 4.0e7;
    auto posRes = RustBridge::validateMovement(badPos);
    EXPECT_EQ(posRes.status, 2); // IllegalPosition

    // Moved too quickly (> 900.0)
    RustBridge::FfiMovementInput fastMove = validMove;
    fastMove.to_x = 50.0; // 40^2 = 1600 > 900
    auto fastRes = RustBridge::validateMovement(fastMove);
    EXPECT_EQ(fastRes.status, 3); // MovedTooQuickly
}

TEST_F(PlayerLogicTest, FallDamageCalculation) {
    // Falling in air: from Y=74 to Y=64, delta = -10
    RustBridge::FfiMovementInput airMove{
        .from_x = 0.0, .from_y = 74.0, .from_z = 0.0,
        .to_x = 0.0, .to_y = 64.0, .to_z = 0.0,
        .stance = 65.62,
        .on_ground = false,
        .is_in_water = false,
        .fall_distance = 0.0f,
    };
    auto airRes = RustBridge::validateMovement(airMove);
    EXPECT_EQ(airRes.status, 0);
    EXPECT_FLOAT_EQ(airRes.new_fall_distance, 10.0f);
    EXPECT_EQ(airRes.fall_damage, 0);

    // Landing on ground: damage = ceil(10.0 - 3.0) = 7
    RustBridge::FfiMovementInput landMove{
        .from_x = 0.0, .from_y = 64.0, .from_z = 0.0,
        .to_x = 0.0, .to_y = 64.0, .to_z = 0.0,
        .stance = 65.62,
        .on_ground = true,
        .is_in_water = false,
        .fall_distance = airRes.new_fall_distance,
    };
    auto landRes = RustBridge::validateMovement(landMove);
    EXPECT_EQ(landRes.status, 0);
    EXPECT_EQ(landRes.fall_damage, 7);
    EXPECT_FLOAT_EQ(landRes.new_fall_distance, 0.0f);

    // Landing in water negates damage
    RustBridge::FfiMovementInput waterMove = landMove;
    waterMove.is_in_water = true;
    auto waterRes = RustBridge::validateMovement(waterMove);
    EXPECT_EQ(waterRes.fall_damage, 0);
    EXPECT_FLOAT_EQ(waterRes.new_fall_distance, 0.0f);
}

// 4. Mining & Tool Effectiveness Tests
TEST_F(PlayerLogicTest, MiningHarvestability) {
    // Dirt (3) can be harvested by hand
    EXPECT_TRUE(RustBridge::miningCanHarvest(3, 0));

    // Stone (1) cannot be harvested by hand, requires pickaxe
    EXPECT_FALSE(RustBridge::miningCanHarvest(1, 0));
    EXPECT_TRUE(RustBridge::miningCanHarvest(1, 270)); // Wood pickaxe

    // Iron ore (15) requires stone pickaxe or better
    EXPECT_FALSE(RustBridge::miningCanHarvest(15, 270)); // Wood pickaxe fails
    EXPECT_TRUE(RustBridge::miningCanHarvest(15, 274));  // Stone pickaxe succeeds

    // Diamond ore (56) requires iron pickaxe or better
    EXPECT_FALSE(RustBridge::miningCanHarvest(56, 274)); // Stone pickaxe fails
    EXPECT_TRUE(RustBridge::miningCanHarvest(56, 257));  // Iron pickaxe succeeds
    EXPECT_TRUE(RustBridge::miningCanHarvest(56, 278));  // Diamond pickaxe succeeds

    // Obsidian (49) requires diamond pickaxe
    EXPECT_FALSE(RustBridge::miningCanHarvest(49, 257)); // Iron pickaxe fails
    EXPECT_TRUE(RustBridge::miningCanHarvest(49, 278));  // Diamond pickaxe succeeds
}

TEST_F(PlayerLogicTest, MiningSpeedAndDestroyTicks) {
    // Bedrock is unbreakable (-1 ticks)
    EXPECT_EQ(RustBridge::miningGetDestroyTicks(7, 278, false, true), -1);

    // Torch is instant (0 ticks)
    EXPECT_EQ(RustBridge::miningGetDestroyTicks(50, 0, false, true), 0);

    // Diamond pickaxe vs stone:
    // hardness = 1.5, str = 8.0 -> ticks = 6
    int ticks = RustBridge::miningGetDestroyTicks(1, 278, false, true);
    EXPECT_EQ(ticks, 6);

    // In water penalty (5x) + in air penalty (5x) = 25x slower
    int submergedTicks = RustBridge::miningGetDestroyTicks(1, 278, true, false);
    EXPECT_GT(submergedTicks, ticks * 20);
}
