#include <gtest/gtest.h>
#include "core/Material.h"

class MaterialTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Material statics are initialized with defaults.
        // In Block::initBlocks(), water/lava get replaced with MaterialLiquid
        // instances and various burning flags are set. For unit tests we test
        // the raw static instances as declared in Material.cpp.
    }
};

TEST_F(MaterialTest, AirProperties) {
    EXPECT_FALSE(Material::air.getIsLiquid());
    EXPECT_TRUE(Material::air.isSolid());
    EXPECT_TRUE(Material::air.blocksMovement());
}

TEST_F(MaterialTest, WaterDefaultNotLiquid) {
    // Water/lava are Material base instances until Block::initBlocks() swaps them
    EXPECT_FALSE(Material::water.getIsLiquid());
    EXPECT_TRUE(Material::water.isSolid());
}

TEST_F(MaterialTest, LavaDefaultNotLiquid) {
    EXPECT_FALSE(Material::lava.getIsLiquid());
    EXPECT_TRUE(Material::lava.isSolid());
}

TEST_F(MaterialTest, RockIsSolid) {
    EXPECT_TRUE(Material::rock.isSolid());
    EXPECT_TRUE(Material::rock.blocksMovement());
    EXPECT_FALSE(Material::rock.getIsLiquid());
    EXPECT_FALSE(Material::rock.getBurning());
}

TEST_F(MaterialTest, WoodDefaultNoBurn) {
    // Wood needs Block::initBlocks() to set burning flag
    EXPECT_FALSE(Material::wood.getBurning());
}

TEST_F(MaterialTest, TransparentSubclass) {
    auto leaves = MaterialTransparent();
    EXPECT_FALSE(leaves.isSolid());
    EXPECT_FALSE(leaves.blocksMovement());
    EXPECT_FALSE(leaves.getCanBlockGrass());
}

TEST_F(MaterialTest, LiquidSubclass) {
    MaterialLiquid liquid;
    EXPECT_TRUE(liquid.getIsLiquid());
    EXPECT_FALSE(liquid.isSolid());
    EXPECT_FALSE(liquid.blocksMovement());
}

TEST_F(MaterialTest, LogicSubclass) {
    MaterialLogic logic;
    EXPECT_FALSE(logic.isSolid());
    EXPECT_FALSE(logic.blocksMovement());
    EXPECT_FALSE(logic.getCanBlockGrass());
}

TEST_F(MaterialTest, FireDefaultNoBurn) {
    EXPECT_FALSE(Material::fire.getBurning());
}

TEST_F(MaterialTest, SetBurning) {
    Material m;
    EXPECT_FALSE(m.getBurning());
    m.setBurning();
    EXPECT_TRUE(m.getBurning());
}

TEST_F(MaterialTest, GroundProperties) {
    EXPECT_TRUE(Material::ground.isSolid());
    EXPECT_TRUE(Material::ground.blocksMovement());
    EXPECT_TRUE(Material::ground.getCanBlockGrass());
    EXPECT_FALSE(Material::ground.getIsLiquid());
}

TEST_F(MaterialTest, SandProperties) {
    EXPECT_TRUE(Material::sand.isSolid());
    EXPECT_TRUE(Material::sand.blocksMovement());
}

TEST_F(MaterialTest, WebProperties) {
    EXPECT_TRUE(Material::web.blocksMovement());
}
