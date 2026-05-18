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
    EXPECT_FALSE(Material::air.isSolid());
    EXPECT_FALSE(Material::air.blocksMovement());
}

TEST_F(MaterialTest, WaterIsLiquid) {
    EXPECT_TRUE(Material::water.getIsLiquid());
    EXPECT_FALSE(Material::water.isSolid());
}

TEST_F(MaterialTest, LavaIsLiquid) {
    EXPECT_TRUE(Material::lava.getIsLiquid());
    EXPECT_FALSE(Material::lava.isSolid());
}

TEST_F(MaterialTest, RockIsSolid) {
    EXPECT_TRUE(Material::rock.isSolid());
    EXPECT_TRUE(Material::rock.blocksMovement());
    EXPECT_FALSE(Material::rock.getIsLiquid());
    EXPECT_FALSE(Material::rock.getBurning());
}

TEST_F(MaterialTest, WoodIsBurning) {
    // Static init sets burning for flammable materials
    EXPECT_TRUE(Material::wood.getBurning());
    EXPECT_TRUE(Material::leaves.getBurning());
    EXPECT_TRUE(Material::cloth.getBurning());
    EXPECT_TRUE(Material::tnt.getBurning());
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
