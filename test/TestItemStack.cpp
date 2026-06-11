#include <gtest/gtest.h>
#include "core/ItemStack.h"
#include "core/Item.h"
#include "core/Material.h"
#include "block/Block.h"

class ItemStackTest : public ::testing::Test {
};

TEST_F(ItemStackTest, DefaultConstructor) {
    ItemStack s;
    EXPECT_EQ(s.stackSize, 0);
    EXPECT_EQ(s.itemID, 0);
}

TEST_F(ItemStackTest, ConstructFromBlock) {
    ItemStack s(Block::stone, 4);
    EXPECT_EQ(s.itemID, 1);
    EXPECT_EQ(s.stackSize, 4);
}

TEST_F(ItemStackTest, ConstructFromItem) {
    ItemStack s(Item::diamond, 2);
    EXPECT_EQ(s.itemID, 264);
    EXPECT_EQ(s.stackSize, 2);
}

TEST_F(ItemStackTest, ConstructFromId) {
    ItemStack s(264, 3);
    EXPECT_EQ(s.itemID, 264);
    EXPECT_EQ(s.stackSize, 3);
}

TEST_F(ItemStackTest, ConstructFromIdDamage) {
    ItemStack s(264, 1, 5);
    EXPECT_EQ(s.itemID, 264);
    EXPECT_EQ(s.itemDamage, 5);
}

TEST_F(ItemStackTest, GetItem) {
    ItemStack s(Item::diamond, 1);
    ASSERT_NE(s.getItem(), nullptr);
    EXPECT_EQ(s.getItem()->itemID, 264);
}

TEST_F(ItemStackTest, GetItemNullptrForInvalidId) {
    ItemStack s(9999, 1);
    EXPECT_EQ(s.getItem(), nullptr);
}

TEST_F(ItemStackTest, NBTRoundTrip) {
    ItemStack s(264, 3, 1);
    auto nbt = s.writeToNBT(std::make_shared<NBTCompound>());
    EXPECT_EQ(nbt->getShort("id"), 264);
    EXPECT_EQ(nbt->getByte("Count"), 3);
    EXPECT_EQ(nbt->getShort("Damage"), 1);

    ItemStack s2(nbt);
    EXPECT_EQ(s2.itemID, 264);
    EXPECT_EQ(s2.stackSize, 3);
    EXPECT_EQ(s2.itemDamage, 1);
}

TEST_F(ItemStackTest, Copy) {
    ItemStack s(264, 5, 2);
    ItemStack c = s.copy();
    EXPECT_EQ(c.itemID, 264);
    EXPECT_EQ(c.stackSize, 5);
    EXPECT_EQ(c.itemDamage, 2);
}

TEST_F(ItemStackTest, GetMaxStackSizeDefault) {
    ItemStack s(264, 1);
    EXPECT_EQ(s.getMaxStackSize(), 64);
}

TEST_F(ItemStackTest, GetMaxStackSizeLimited) {
    ItemStack s(325, 1);
    ASSERT_NE(s.getItem(), nullptr);
    EXPECT_EQ(s.getMaxStackSize(), s.getItem()->maxStackSize);
}

TEST_F(ItemStackTest, GetStrVsBlockDefault) {
    ItemStack s(264, 1);
    Block* stone = Block::stone;
    ASSERT_NE(stone, nullptr);
    EXPECT_FLOAT_EQ(s.getStrVsBlock(stone), 1.0f);
}

TEST_F(ItemStackTest, PickaxeStrVsBlock) {
    ItemStack pick(Item::pickaxeSteel, 1);
    Block* stone = Block::stone;
    ASSERT_NE(pick.getItem(), nullptr);
    ASSERT_NE(stone, nullptr);
    EXPECT_FLOAT_EQ(pick.getStrVsBlock(stone), 6.0f);
}

TEST_F(ItemStackTest, PickaxeCanHarvestStone) {
    ItemStack pick(Item::pickaxeSteel, 1);
    Block* stone = Block::stone;
    ASSERT_NE(stone, nullptr);
    EXPECT_TRUE(pick.canHarvestBlock(stone));
}

TEST_F(ItemStackTest, SpadeCannotHarvestStone) {
    auto* shovelWood = Item::shovelWood;
    ASSERT_NE(shovelWood, nullptr);
    ItemStack shovel(shovelWood, 1);
    Block* stone = Block::stone;
    ASSERT_NE(stone, nullptr);
    EXPECT_FALSE(shovel.canHarvestBlock(stone));
}

TEST_F(ItemStackTest, DamageItemBreak) {
    auto* sword = Item::swordWood;
    ASSERT_NE(sword, nullptr);
    ItemStack s(sword, 1);
    s.itemDamage = 31;
    s.damageItem(1);
    EXPECT_EQ(s.itemDamage, 32);
    s.damageItem(1);
    EXPECT_EQ(s.itemDamage, 0);
    EXPECT_EQ(s.stackSize, 0);
}

TEST_F(ItemStackTest, DamageItemNoDurability) {
    ItemStack s(264, 1);
    s.damageItem(100);
    EXPECT_EQ(s.stackSize, 1);
}

TEST_F(ItemStackTest, UseItemRightClickReturnsCopy) {
    ItemStack s(264, 1);
    ItemStack r = s.useItemRightClick(nullptr, nullptr);
    EXPECT_EQ(r.itemID, 264);
    EXPECT_EQ(r.stackSize, 1);
}

TEST_F(ItemStackTest, CanHarvestBlockFurnaceWithPickaxe) {
    ItemStack pick(Item::pickaxeSteel, 1);
    Block* furnace = Block::blocksList[61];
    ASSERT_NE(furnace, nullptr);
    EXPECT_TRUE(pick.canHarvestBlock(furnace));
}

TEST_F(ItemStackTest, AxeEffectiveAgainstWood) {
    ItemStack axe(Item::axeSteel, 1);
    Block* wood = Block::wood;
    ASSERT_NE(wood, nullptr);
    EXPECT_FLOAT_EQ(axe.getStrVsBlock(wood), 6.0f);
}

TEST_F(ItemStackTest, SpadeEffectiveAgainstDirt) {
    ItemStack spade(Item::shovelSteel, 1);
    Block* dirt = Block::dirt;
    ASSERT_NE(dirt, nullptr);
    EXPECT_FLOAT_EQ(spade.getStrVsBlock(dirt), 6.0f);
}

TEST_F(ItemStackTest, ToolGetStrVsBlockNegativeId) {
    auto* tool = dynamic_cast<ItemTool*>(Item::pickaxeSteel);
    ASSERT_NE(tool, nullptr);
    EXPECT_FLOAT_EQ(tool->getStrVsBlock(-1), 1.0f);
}

TEST_F(ItemStackTest, ToolGetStrVsBlockZeroId) {
    auto* tool = dynamic_cast<ItemTool*>(Item::pickaxeSteel);
    ASSERT_NE(tool, nullptr);
    EXPECT_FLOAT_EQ(tool->getStrVsBlock(0), 1.0f);
}

TEST_F(ItemStackTest, ToolGetStrVsBlockLargeId) {
    auto* tool = dynamic_cast<ItemTool*>(Item::pickaxeSteel);
    ASSERT_NE(tool, nullptr);
    EXPECT_FLOAT_EQ(tool->getStrVsBlock(9999), 1.0f);
}

TEST_F(ItemStackTest, ToolGetStrVsBlockBoundaryId256) {
    auto* tool = dynamic_cast<ItemTool*>(Item::pickaxeSteel);
    ASSERT_NE(tool, nullptr);
    EXPECT_FLOAT_EQ(tool->getStrVsBlock(256), 1.0f);
}

TEST_F(ItemStackTest, HoeOnItemUseNullStack) {
    ASSERT_NE(Item::hoeSteel, nullptr);
    EXPECT_FALSE(Item::hoeSteel->onItemUse(nullptr, nullptr, nullptr, 0, 0, 0, 0));
}

TEST_F(ItemStackTest, HoeOnItemUseNullPlayer) {
    ItemStack stack(Item::hoeSteel, 1);
    ASSERT_NE(Item::hoeSteel, nullptr);
    // null player should be caught by null check
    EXPECT_FALSE(Item::hoeSteel->onItemUse(&stack, nullptr, nullptr, 0, 0, 0, 0));
}

TEST_F(ItemStackTest, HoeOnItemUseNullWorld) {
    ItemStack stack(Item::hoeSteel, 1);
    ASSERT_NE(Item::hoeSteel, nullptr);
    EXPECT_FALSE(Item::hoeSteel->onItemUse(&stack, nullptr, nullptr, 0, 0, 0, 0));
}

TEST_F(ItemStackTest, SeedsOnItemUseNullStack) {
    ASSERT_NE(Item::seeds, nullptr);
    EXPECT_FALSE(Item::seeds->onItemUse(nullptr, nullptr, nullptr, 0, 0, 0, 0));
}

TEST_F(ItemStackTest, SeedsOnItemUseNullWorld) {
    ItemStack stack(Item::seeds, 1);
    ASSERT_NE(Item::seeds, nullptr);
    EXPECT_FALSE(Item::seeds->onItemUse(&stack, nullptr, nullptr, 0, 0, 0, 0));
}

TEST_F(ItemStackTest, FlintOnItemUseNullStack) {
    ASSERT_NE(Item::flintAndSteel, nullptr);
    EXPECT_FALSE(Item::flintAndSteel->onItemUse(nullptr, nullptr, nullptr, 0, 0, 0, 0));
}

TEST_F(ItemStackTest, FlintOnItemUseNullWorld) {
    ItemStack stack(Item::flintAndSteel, 1);
    ASSERT_NE(Item::flintAndSteel, nullptr);
    EXPECT_FALSE(Item::flintAndSteel->onItemUse(&stack, nullptr, nullptr, 0, 0, 0, 0));
}

TEST_F(ItemStackTest, BaseOnItemUseReturnsFalse) {
    // diamond has no override, uses base Item::onItemUse which returns false
    ASSERT_NE(Item::diamond, nullptr);
    EXPECT_FALSE(Item::diamond->onItemUse(nullptr, nullptr, nullptr, 0, 0, 0, 0));
}
