#include "Block.h"
#include "../core/Material.h"
#include "../core/AxisAlignedBB.h"
#include <iostream>

Block* Block::blocksList[256] = {nullptr};
bool Block::tickOnLoad[256] = {false};
bool Block::isBlockContainer[256] = {false};
int Block::lightOpacity[256] = {0};
int Block::lightValue[256] = {0};

Block* Block::stone = nullptr;
Block* Block::grass = nullptr;
Block* Block::dirt = nullptr;
Block* Block::cobblestone = nullptr;
Block* Block::planks = nullptr;
Block* Block::bedrock = nullptr;
Block* Block::sand = nullptr;
Block* Block::gravel = nullptr;
Block* Block::wood = nullptr;
Block* Block::leaves = nullptr;
Block* Block::glass = nullptr;
Block* Block::oreGold = nullptr;
Block* Block::oreIron = nullptr;
Block* Block::oreCoal = nullptr;
Block* Block::oreDiamond = nullptr;
Block* Block::oreRedstone = nullptr;
Block* Block::blockClay = nullptr;
Block* Block::cactus = nullptr;
Block* Block::reed = nullptr;
Block* Block::pumpkin = nullptr;
Block* Block::snow = nullptr;
Block* Block::ice = nullptr;
Block* Block::cobblestoneMossy = nullptr;
Block* Block::mobSpawner = nullptr;
Block* Block::plantYellow = nullptr;
Block* Block::plantRed = nullptr;
Block* Block::mushroomBrown = nullptr;
Block* Block::mushroomRed = nullptr;
Block* Block::sapling = nullptr;

Block::Block(int id, Material* material)
    : blockID(id), blockMaterial(material), blockHardness(0.0f), blockResistance(0.0f) {
    
    if (blocksList[id] != nullptr) {
        throw std::runtime_error("Block slot " + std::to_string(id) + " is already occupied!");
    }
    
    blocksList[id] = this;
    setBlockBounds(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    lightOpacity[id] = 255;
    isBlockContainer[id] = false;
}

void Block::initBlocks() {
    stone = (new Block(1, &Material::rock))->setHardness(1.5f)->setResistance(10.0f);
    grass = (new Block(2, &Material::ground))->setHardness(0.6f);
    dirt = (new Block(3, &Material::ground))->setHardness(0.5f);
    cobblestone = (new Block(4, &Material::rock))->setHardness(2.0f)->setResistance(10.0f);
    planks = (new Block(5, &Material::wood))->setHardness(2.0f)->setResistance(5.0f);
    sapling = (new Block(6, &Material::plants))->setHardness(0.0f);
    bedrock = (new Block(7, &Material::rock))->setHardness(-1.0f)->setResistance(6000000.0f);
    // water and lava
    blocksList[8] = new Block(8, &Material::water);
    blocksList[9] = new Block(9, &Material::water);
    blocksList[10] = new Block(10, &Material::lava);
    blocksList[11] = new Block(11, &Material::lava);
    sand = (new Block(12, &Material::sand))->setHardness(0.5f);
    gravel = (new Block(13, &Material::sand))->setHardness(0.6f);
    oreGold = (new Block(14, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    oreIron = (new Block(15, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    oreCoal = (new Block(16, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    wood = (new Block(17, &Material::wood))->setHardness(2.0f);
    leaves = (new Block(18, &Material::leaves))->setHardness(0.2f)->setLightOpacity(1);
    plantYellow = (new Block(37, &Material::plants))->setHardness(0.0f);
    plantRed = (new Block(38, &Material::plants))->setHardness(0.0f);
    mushroomBrown = (new Block(39, &Material::plants))->setHardness(0.0f);
    mushroomRed = (new Block(40, &Material::plants))->setHardness(0.0f);
    cobblestoneMossy = (new Block(48, &Material::rock))->setHardness(2.0f)->setResistance(10.0f);
    mobSpawner = (new Block(52, &Material::rock))->setHardness(5.0f);
    oreDiamond = (new Block(56, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    oreRedstone = (new Block(73, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    snow = (new Block(78, &Material::snow))->setHardness(0.1f);
    ice = (new Block(79, &Material::ice))->setHardness(0.5f);
    cactus = (new Block(81, &Material::cactus))->setHardness(0.4f);
    blockClay = (new Block(82, &Material::clay))->setHardness(0.6f);
    reed = (new Block(83, &Material::plants))->setHardness(0.0f);
    pumpkin = (new Block(86, &Material::pumpkin))->setHardness(1.0f);

    std::cout << "[INFO] Registered all standard blocks." << std::endl;
}

Block* Block::setHardness(float hardness) {
    blockHardness = hardness;
    if (blockResistance < hardness * 5.0f) {
        blockResistance = hardness * 5.0f;
    }
    return this;
}

Block* Block::setResistance(float resistance) {
    blockResistance = resistance * 3.0f;
    return this;
}

Block* Block::setLightOpacity(int opacity) {
    lightOpacity[blockID] = opacity;
    return this;
}

Block* Block::setLightValue(float value) {
    lightValue[blockID] = static_cast<int>(15.0f * value);
    return this;
}

Block* Block::setTickOnLoad(bool tick) {
    tickOnLoad[blockID] = tick;
    return this;
}

void Block::setBlockBounds(float mnX, float mnY, float mnZ, float mxX, float mxY, float mxZ) {
    minX = mnX; minY = mnY; minZ = mnZ;
    maxX = mxX; maxY = mxY; maxZ = mxZ;
}

std::optional<AxisAlignedBB> Block::getCollisionBoundingBoxFromPool(World* world, int x, int y, int z) {
    return AxisAlignedBB::getBoundingBox(x + minX, y + minY, z + minZ, x + maxX, y + maxY, z + maxZ);
}

void Block::getCollidingBoundingBoxes(World* world, int x, int y, int z, const AxisAlignedBB& mask, std::vector<AxisAlignedBB>& list) {
    auto bb = getCollisionBoundingBoxFromPool(world, x, y, z);
    if (bb && mask.intersectsWith(*bb)) {
        list.push_back(*bb);
    }
}

void Block::dropBlockAsItem(World* world, int x, int y, int z, int metadata) {
    dropBlockAsItemWithChance(world, x, y, z, metadata, 1.0f);
}

void Block::dropBlockAsItemWithChance(World* world, int x, int y, int z, int metadata, float chance) {
    // Generate an ItemEntity drop
}

float Block::checkHardness(EntityPlayer* player) const {
    if (blockHardness < 0.0f) return 0.0f; // Unbreakable
    // Depending on what player holds...
    return 1.0f / blockHardness / 100.0f;
}
