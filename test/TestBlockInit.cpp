#include <gtest/gtest.h>
#include "block/Block.h"
#include "block/BlockContainer.h"
#include "core/Material.h"

class BlockInitTest : public ::testing::Test {
};

TEST_F(BlockInitTest, StaticPointersNonNull) {
    ASSERT_NE(Block::stone, nullptr);
    ASSERT_NE(Block::grass, nullptr);
    ASSERT_NE(Block::dirt, nullptr);
    ASSERT_NE(Block::cobblestone, nullptr);
    ASSERT_NE(Block::planks, nullptr);
    ASSERT_NE(Block::bedrock, nullptr);
    ASSERT_NE(Block::sand, nullptr);
    ASSERT_NE(Block::gravel, nullptr);
    ASSERT_NE(Block::wood, nullptr);
    ASSERT_NE(Block::leaves, nullptr);
    ASSERT_NE(Block::glass, nullptr);
    ASSERT_NE(Block::oreGold, nullptr);
    ASSERT_NE(Block::oreIron, nullptr);
    ASSERT_NE(Block::oreCoal, nullptr);
    ASSERT_NE(Block::oreDiamond, nullptr);
    ASSERT_NE(Block::oreRedstone, nullptr);
    ASSERT_NE(Block::snow, nullptr);
    ASSERT_NE(Block::ice, nullptr);
    ASSERT_NE(Block::cactus, nullptr);
    ASSERT_NE(Block::reed, nullptr);
    ASSERT_NE(Block::pumpkin, nullptr);
    ASSERT_NE(Block::plantYellow, nullptr);
    ASSERT_NE(Block::plantRed, nullptr);
    ASSERT_NE(Block::mushroomBrown, nullptr);
    ASSERT_NE(Block::mushroomRed, nullptr);
    ASSERT_NE(Block::blockClay, nullptr);
    ASSERT_NE(Block::cobblestoneMossy, nullptr);
    ASSERT_NE(Block::sapling, nullptr);
}

TEST_F(BlockInitTest, BlocksRegisteredAtCorrectIds) {
    EXPECT_EQ(Block::blocksList[1], Block::stone);
    EXPECT_EQ(Block::blocksList[2], Block::grass);
    EXPECT_EQ(Block::blocksList[3], Block::dirt);
    EXPECT_EQ(Block::blocksList[4], Block::cobblestone);
    EXPECT_EQ(Block::blocksList[5], Block::planks);
    EXPECT_EQ(Block::blocksList[7], Block::bedrock);
    EXPECT_EQ(Block::blocksList[12], Block::sand);
    EXPECT_EQ(Block::blocksList[13], Block::gravel);
    EXPECT_EQ(Block::blocksList[17], Block::wood);
    EXPECT_EQ(Block::blocksList[18], Block::leaves);
    EXPECT_EQ(Block::blocksList[20], Block::glass);
    EXPECT_EQ(Block::blocksList[78], Block::snow);
    EXPECT_EQ(Block::blocksList[79], Block::ice);
    EXPECT_EQ(Block::blocksList[81], Block::cactus);
    EXPECT_EQ(Block::blocksList[83], Block::reed);
    EXPECT_EQ(Block::blocksList[86], Block::pumpkin);
}

TEST_F(BlockInitTest, UnregisteredSlotsAreNull) {
    EXPECT_EQ(Block::blocksList[0], nullptr);
    EXPECT_EQ(Block::blocksList[26], nullptr);
    EXPECT_EQ(Block::blocksList[34], nullptr);
    EXPECT_EQ(Block::blocksList[36], nullptr);
    EXPECT_EQ(Block::blocksList[90], nullptr);
    EXPECT_EQ(Block::blocksList[92], nullptr);
    EXPECT_EQ(Block::blocksList[255], nullptr);
}

TEST_F(BlockInitTest, StoneBlockProperties) {
    Block* s = Block::stone;
    EXPECT_EQ(s->blockID, 1);
    EXPECT_EQ(s->blockMaterial, &Material::rock);
    EXPECT_FLOAT_EQ(s->blockHardness, 1.5f);
    // setResistance multiplies by 3 (vanilla Minecraft behavior)
    EXPECT_FLOAT_EQ(s->blockResistance, 30.0f);
}

TEST_F(BlockInitTest, DirtBlockProperties) {
    Block* d = Block::dirt;
    EXPECT_EQ(d->blockID, 3);
    EXPECT_EQ(d->blockMaterial, &Material::ground);
    EXPECT_FLOAT_EQ(d->blockHardness, 0.5f);
}

TEST_F(BlockInitTest, BedrockBlockProperties) {
    Block* b = Block::bedrock;
    EXPECT_EQ(b->blockID, 7);
    EXPECT_EQ(b->blockMaterial, &Material::rock);
    EXPECT_FLOAT_EQ(b->blockHardness, -1.0f);
    // setResistance multiplies by 3 (vanilla Minecraft behavior)
    EXPECT_FLOAT_EQ(b->blockResistance, 18000000.0f);
}

TEST_F(BlockInitTest, LeavesBlockProperties) {
    Block* l = Block::leaves;
    EXPECT_EQ(l->blockID, 18);
    EXPECT_EQ(l->blockMaterial, &Material::leaves);
    EXPECT_FLOAT_EQ(l->blockHardness, 0.2f);
    EXPECT_EQ(Block::lightOpacity[18], 1);
    EXPECT_TRUE(Block::tickOnLoad[18]);
}

TEST_F(BlockInitTest, GlassLightOpacityZero) {
    EXPECT_EQ(Block::lightOpacity[20], 0);
}

TEST_F(BlockInitTest, WaterLightOpacity) {
    EXPECT_EQ(Block::lightOpacity[8], 3);
    EXPECT_EQ(Block::lightOpacity[9], 3);
}

TEST_F(BlockInitTest, PlanksMaterial) {
    EXPECT_EQ(Block::planks->blockMaterial, &Material::wood);
}

TEST_F(BlockInitTest, SandGravelMaterial) {
    EXPECT_EQ(Block::sand->blockMaterial, &Material::sand);
    EXPECT_EQ(Block::gravel->blockMaterial, &Material::sand);
}
