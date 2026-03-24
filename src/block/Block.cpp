#include "Block.h"
#include "BlockContainer.h"
#include "BlockChest.h"
#include "BlockFurnace.h"
#include "BlockSign.h"
#include "../core/Material.h"
#include "../core/AxisAlignedBB.h"
#include "../world/World.h"
#include "../world/TileEntity.h"
#include "../world/TileEntityChest.h"
#include "../world/TileEntityFurnace.h"
#include "../world/TileEntitySign.h"
#include "../entity/EntityItem.h"
#include "../entity/EntityFallingSand.h"
#include "../entity/EntityPlayerMP.h"
#include "../core/Item.h"
#include "../MinecraftServer.h"
#include <iostream>
#include <random>
#include <queue>

class BlockSand : public Block {
public:
    BlockSand(int id, Material* material) : Block(id, material) {}

    void onBlockAdded(World* world, int x, int y, int z) override {
        world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
    }

    void updateTick(World* world, int x, int y, int z) override {
        if (canFallBelow(world, x, y - 1, z) && y >= 0) {
            world->setBlockAndUpdate(x, y, z, 0);
            auto entity = std::make_unique<EntityFallingSand>(blockID, x + 0.5, y + 0.5, z + 0.5);
            world->spawnEntityInWorld(std::move(entity));
        }
    }

    int tickRate() const override { return 3; }

private:
    static bool canFallBelow(World* world, int x, int y, int z) {
        int id = world->getBlockId(x, y, z);
        if (id == 0 || id == 51) return true; // air or fire
        Block* b = Block::blocksList[id];
        if (!b) return true;
        return b->blockMaterial == &Material::water || b->blockMaterial == &Material::lava;
    }
};

class BlockFluid : public Block {
public:
    BlockFluid(int id, Material* material) : Block(id, material) {}

    void onBlockAdded(World* world, int x, int y, int z) override {
        world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
    }

    void updateTick(World* world, int x, int y, int z) override {
        // Very simplified fluid spreading logic
        int metadata = world->getBlockMetadata(x, y, z);
        if (metadata >= 8) return; // fully spread

        // Try to flow down first
        if (canFlowInto(world, x, y - 1, z)) {
            world->setBlockAndMetadataWithNotify(x, y - 1, z, blockID, 8); // Flowing down
        } else if (metadata < 7) {
            // Flow sideways
            int newMeta = metadata + 1;
            if (blockMaterial == &Material::lava) newMeta = metadata + 2; // lava flows slower/less
            
            if (newMeta < 8) {
                flowIntoBlock(world, x - 1, y, z, newMeta);
                flowIntoBlock(world, x + 1, y, z, newMeta);
                flowIntoBlock(world, x, y, z - 1, newMeta);
                flowIntoBlock(world, x, y, z + 1, newMeta);
            }
        }
    }

    int tickRate() const override {
        return blockMaterial == &Material::water ? 5 : 30;
    }
    
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World* world, int x, int y, int z) override {
        return std::nullopt; // Liquids have no collision box
    }

    bool isCollidable() const override { return false; }

private:
    bool canFlowInto(World* world, int x, int y, int z) {
        int id = world->getBlockId(x, y, z);
        if (id == 0) return true;
        if (id == 8 || id == 9 || id == 10 || id == 11) return false; // Don't flow into other liquids
        // Simplified: flow into passable blocks
        if (id == 37 || id == 38 || id == 39 || id == 40 || id == 50 || id == 51 || id == 55 || id == 59 || id == 83 || id == 78) return true;
        return false;
    }

    void flowIntoBlock(World* world, int x, int y, int z, int newMeta) {
        if (canFlowInto(world, x, y, z)) {
            int currentId = world->getBlockId(x, y, z);
            if (currentId != 0) {
                if (Block::blocksList[currentId]) {
                    Block::blocksList[currentId]->dropBlockAsItem(world, x, y, z, world->getBlockMetadata(x, y, z));
                }
            }
            world->setBlockAndMetadataWithNotify(x, y, z, blockID, newMeta);
        }
    }
};

class BlockFlower : public Block {
public:
    BlockFlower(int id, Material* mat) : Block(id, mat) {}
    bool canBlockStay(World* world, int x, int y, int z) const override {
        int below = world->getBlockId(x, y - 1, z);
        return below == 2 || below == 3 || below == 60;
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, 0);
            world->setBlockAndUpdate(x, y, z, 0);
        }
    }
    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};

class BlockMushroom : public Block {
public:
    BlockMushroom(int id, Material* mat) : Block(id, mat) {}
    bool canBlockStay(World* world, int x, int y, int z) const override {
        int below = world->getBlockId(x, y - 1, z);
        return below > 0 && Block::blocksList[below] != nullptr;
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, 0);
            world->setBlockAndUpdate(x, y, z, 0);
        }
    }
    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};

class BlockTorch : public Block {
public:
    BlockTorch(int id, Material* mat) : Block(id, mat) {}

    // Java: doesBlockAllowAttachment = block is solid and opaque
    static bool allowsAttachment(World* world, int x, int y, int z) {
        int id = world->getBlockId(x, y, z);
        if (id == 0) return false;
        Block* b = Block::blocksList[id];
        return b && b->blockMaterial->isSolid() && b->isCollidable();
    }

    // Java BlockTorch.onBlockPlaced: sets metadata based on which face was clicked
    // side: 1=bottom(floor), 2=north(+z), 3=south(-z), 4=west(+x), 5=east(-x)
    void onBlockPlaced(World* world, int x, int y, int z, int side) override {
        uint8_t meta = 5; // default: floor
        if (side == 2 && allowsAttachment(world, x, y, z + 1)) meta = 4;
        else if (side == 3 && allowsAttachment(world, x, y, z - 1)) meta = 3;
        else if (side == 4 && allowsAttachment(world, x + 1, y, z)) meta = 2;
        else if (side == 5 && allowsAttachment(world, x - 1, y, z)) meta = 1;
        world->setBlockAndMetadata(x, y, z, blockID, meta);
        // markBlockNeedsUpdate is called by handlePlace after onBlockPlaced returns
    }

    void onBlockAdded(World* world, int x, int y, int z) override {
        // Only auto-detect attachment when metadata is 0 AND no neighbors triggered
        // onBlockPlaced yet. This handles world-gen torches and chunk loading.
        // When placed by player, onBlockPlaced sets metadata after us —
        // so we must NOT send markBlockNeedsUpdate here to avoid the flicker.
        if (world->getBlockMetadata(x, y, z) != 0) return;
        uint8_t meta = 0;
        if      (allowsAttachment(world, x - 1, y, z)) meta = 1;
        else if (allowsAttachment(world, x + 1, y, z)) meta = 2;
        else if (allowsAttachment(world, x, y, z - 1)) meta = 3;
        else if (allowsAttachment(world, x, y, z + 1)) meta = 4;
        else if (allowsAttachment(world, x, y - 1, z)) meta = 5;
        if (meta != 0)
            world->setBlockAndMetadata(x, y, z, blockID, meta);
        // No markBlockNeedsUpdate — setBlockWithNotify calls it after us with final metadata
    }

    bool canBlockStay(World* world, int x, int y, int z) const override {
        return allowsAttachment(world, x-1, y, z) || allowsAttachment(world, x+1, y, z)
            || allowsAttachment(world, x, y, z-1) || allowsAttachment(world, x, y, z+1)
            || allowsAttachment(world, x, y-1, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        uint8_t meta = world->getBlockMetadata(x, y, z);
        bool detach = false;
        if (meta == 1 && !allowsAttachment(world, x - 1, y, z)) detach = true;
        if (meta == 2 && !allowsAttachment(world, x + 1, y, z)) detach = true;
        if (meta == 3 && !allowsAttachment(world, x, y, z - 1)) detach = true;
        if (meta == 4 && !allowsAttachment(world, x, y, z + 1)) detach = true;
        if (meta == 5 && !allowsAttachment(world, x, y - 1, z)) detach = true;
        if (detach) {
            dropBlockAsItem(world, x, y, z, meta);
            world->setBlockWithNotify(x, y, z, 0);
        }
    }

    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};

class BlockCactus : public Block {
public:
    BlockCactus(int id, Material* mat) : Block(id, mat) {}
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, world->getBlockMetadata(x, y, z));
            world->setBlockAndUpdate(x, y, z, 0);
        }
    }
    bool canBlockStay(World* world, int x, int y, int z) const override {
        auto solid = [&](int bx, int by, int bz) {
            int id = world->getBlockId(bx, by, bz);
            if (id == 0) return false;
            Block* b = Block::blocksList[id];
            return b && b->blockMaterial->isSolid();
        };
        if (solid(x-1,y,z) || solid(x+1,y,z) || solid(x,y,z-1) || solid(x,y,z+1)) return false;
        int below = world->getBlockId(x, y-1, z);
        return below == 12 || below == 81;
    }
};

class BlockReed : public Block {
public:
    BlockReed(int id, Material* mat) : Block(id, mat) {}
    bool canBlockStay(World* world, int x, int y, int z) const override {
        int below = world->getBlockId(x, y-1, z);
        if (below == 83) return true;
        if (below != 2 && below != 3 && below != 12) return false;
        return world->getBlockId(x-1,y-1,z) == 8 || world->getBlockId(x-1,y-1,z) == 9
            || world->getBlockId(x+1,y-1,z) == 8 || world->getBlockId(x+1,y-1,z) == 9
            || world->getBlockId(x,y-1,z-1) == 8 || world->getBlockId(x,y-1,z-1) == 9
            || world->getBlockId(x,y-1,z+1) == 8 || world->getBlockId(x,y-1,z+1) == 9;
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, 0);
            world->setBlockAndUpdate(x, y, z, 0);
        }
    }
    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};


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

class BlockOre : public Block {
public:
    BlockOre(int id, Material* material) : Block(id, material) {}

    int idDropped(int metadata) const override {
        if (blockID == 16) return 263; // coal ore -> coal (Item id 7 -> itemID 263)
        if (blockID == 56) return 264; // diamond ore -> diamond (Item id 8 -> itemID 264)
        if (blockID == 21) return 351; // lapis ore -> lapis (Item id 95 -> itemID 351)
        return blockID;                // iron/gold/redstone ore drop themselves
    }
};

class BlockStone : public Block {
public:
    BlockStone(int id, Material* material) : Block(id, material) {}
    int idDropped(int metadata) const override { return 4; } // Stone drops Cobblestone
    bool canHarvestBlock(EntityPlayer* player) const override { return false; } // Hand cannot harvest stone in Alpha
};

class BlockGrass : public Block {
public:
    BlockGrass(int id, Material* material) : Block(id, material) {}
    int idDropped(int metadata) const override { return 3; } // Grass drops Dirt
};

class BlockLeaves : public Block {
public:
    BlockLeaves(int id, Material* material) : Block(id, material) {}
    int quantityDropped() const override { return 0; } // Leaves drop nothing by default
    int idDropped(int metadata) const override { return 0; }
};

void Block::initBlocks() {
    stone = (new BlockStone(1, &Material::rock))->setHardness(1.5f)->setResistance(10.0f);
    grass = (new BlockGrass(2, &Material::ground))->setHardness(0.6f);
    dirt = (new Block(3, &Material::ground))->setHardness(0.5f);
    cobblestone = (new Block(4, &Material::rock))->setHardness(2.0f)->setResistance(10.0f);
    planks = (new Block(5, &Material::wood))->setHardness(2.0f)->setResistance(5.0f);
    sapling = (new Block(6, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);
    bedrock = (new Block(7, &Material::rock))->setHardness(-1.0f)->setResistance(6000000.0f);
    // water and lava
    blocksList[8] = (new BlockFluid(8, &Material::water))->setLightOpacity(3);
    blocksList[9] = (new BlockFluid(9, &Material::water))->setLightOpacity(3);
    blocksList[10] = (new BlockFluid(10, &Material::lava))->setLightOpacity(255);
    blocksList[11] = (new BlockFluid(11, &Material::lava))->setLightOpacity(255);
    sand = (new BlockSand(12, &Material::sand))->setHardness(0.5f);
    gravel = (new BlockSand(13, &Material::sand))->setHardness(0.6f);
    oreGold = (new BlockOre(14, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    oreIron = (new BlockOre(15, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    oreCoal = (new BlockOre(16, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    wood = (new Block(17, &Material::wood))->setHardness(2.0f);
    leaves = (new BlockLeaves(18, &Material::leaves))->setHardness(0.2f)->setLightOpacity(1);
    (new Block(19, &Material::ground))->setHardness(0.4f);   // sponge
    glass = (new Block(20, &Material::glass))->setHardness(0.3f)->setLightOpacity(0);
    (new Block(21, &Material::rock))->setHardness(3.0f);     // lapis ore
    (new Block(22, &Material::rock))->setHardness(3.0f);     // lapis block
    (new Block(23, &Material::rock))->setHardness(3.5f);     // dispenser
    (new Block(24, &Material::rock))->setHardness(0.8f);     // sandstone
    (new Block(25, &Material::wood))->setHardness(0.8f);     // noteblock
    (new Block(27, &Material::ground))->setHardness(0.7f);   // powered rail
    (new Block(28, &Material::ground))->setHardness(0.7f);   // detector rail
    (new Block(29, &Material::rock))->setHardness(3.5f);     // sticky piston
    (new Block(30, &Material::web))->setHardness(4.0f)->setLightOpacity(1);      // web
    (new Block(31, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);   // tall grass
    (new Block(32, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);   // dead bush
    (new Block(33, &Material::rock))->setHardness(3.5f);     // piston
    (new Block(35, &Material::cloth))->setHardness(0.8f);    // wool
    plantYellow = (new BlockFlower(37, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);
    plantRed = (new BlockFlower(38, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);
    mushroomBrown = (new BlockMushroom(39, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);
    mushroomRed = (new BlockMushroom(40, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);
    (new Block(41, &Material::iron))->setHardness(3.0f);     // gold block
    (new Block(42, &Material::iron))->setHardness(5.0f);     // iron block
    (new Block(43, &Material::rock))->setHardness(2.0f);     // double slab
    (new Block(44, &Material::rock))->setHardness(2.0f);     // slab
    (new Block(45, &Material::rock))->setHardness(2.0f);     // brick block
    (new Block(46, &Material::tnt))->setHardness(0.0f);      // TNT
    (new Block(47, &Material::wood))->setHardness(1.5f);     // bookshelf
    cobblestoneMossy = (new Block(48, &Material::rock))->setHardness(2.0f)->setResistance(10.0f);
    (new Block(49, &Material::rock))->setHardness(2000.0f);  // obsidian
    (new BlockTorch(50, &Material::circuits))->setHardness(0.0f)->setLightOpacity(0); // torch
    (new Block(51, &Material::fire))->setHardness(0.0f);     // fire
    (new Block(52, &Material::rock))->setHardness(5.0f);
    (new Block(53, &Material::wood))->setHardness(2.0f);     // wood stairs
    blocksList[54] = new BlockChest(54);                     // chest
    blocksList[54]->setHardness(2.5f);
    (new Block(55, &Material::circuits))->setHardness(0.0f)->setLightOpacity(0); // redstone wire
    oreDiamond = (new BlockOre(56, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    (new Block(57, &Material::iron))->setHardness(5.0f);     // diamond block
    (new Block(58, &Material::wood))->setHardness(2.5f);     // crafting table
    (new Block(59, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);   // crops
    (new Block(60, &Material::ground))->setHardness(0.6f);   // farmland
    blocksList[61] = new BlockFurnace(61, false);            // furnace idle
    blocksList[61]->setHardness(3.5f);
    blocksList[62] = new BlockFurnace(62, true);             // furnace active
    blocksList[62]->setHardness(3.5f);
    blocksList[63] = new BlockSign(63, false);  // sign post (floor)
    blocksList[63]->setHardness(1.0f)->setLightOpacity(0);
    (new Block(64, &Material::wood))->setHardness(3.0f);     // wood door
    (new Block(65, &Material::wood))->setHardness(0.4f);     // ladder
    (new Block(66, &Material::ground))->setHardness(0.7f);   // rail
    (new Block(67, &Material::rock))->setHardness(2.0f);     // cobblestone stairs
    blocksList[68] = new BlockSign(68, true);   // wall sign
    blocksList[68]->setHardness(1.0f)->setLightOpacity(0);
    (new Block(69, &Material::circuits))->setHardness(0.5f); // lever
    (new Block(70, &Material::rock))->setHardness(0.5f);     // stone pressure plate
    (new Block(71, &Material::iron))->setHardness(3.0f);     // iron door
    (new Block(72, &Material::wood))->setHardness(0.5f);     // wood pressure plate
    oreRedstone = (new BlockOre(73, &Material::rock))->setHardness(3.0f)->setResistance(5.0f);
    (new BlockOre(74, &Material::rock))->setHardness(3.0f);     // glowing redstone ore
    (new Block(75, &Material::circuits))->setHardness(0.0f); // redstone torch off
    (new Block(76, &Material::circuits))->setHardness(0.0f); // redstone torch on
    (new Block(77, &Material::circuits))->setHardness(0.5f); // stone button
    snow = (new Block(78, &Material::snow))->setHardness(0.1f);
    ice = (new Block(79, &Material::ice))->setHardness(0.5f);
    (new Block(80, &Material::snow))->setHardness(0.2f);     // snow block
    cactus = (new BlockCactus(81, &Material::cactus))->setHardness(0.4f)->setLightOpacity(0);
    blockClay = (new Block(82, &Material::clay))->setHardness(0.6f);
    reed = (new BlockReed(83, &Material::plants))->setHardness(0.0f)->setLightOpacity(0);
    (new Block(84, &Material::wood))->setHardness(0.8f);     // jukebox
    (new Block(85, &Material::wood))->setHardness(2.0f);     // fence
    pumpkin = (new Block(86, &Material::pumpkin))->setHardness(1.0f);
    (new Block(87, &Material::rock))->setHardness(0.4f);     // netherrack
    (new Block(88, &Material::sand))->setHardness(0.5f);     // soul sand
    (new Block(89, &Material::rock))->setHardness(0.3f);     // glowstone
    (new Block(91, &Material::pumpkin))->setHardness(1.0f);  // jack-o-lantern

    // TileEntities are registered via REGISTER_TILE_ENTITY macros in their headers

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
    // Don't drop items on client side (multiplayerWorld check in Java)
    if (!world->mcServer || !world->mcServer->configManager) return;
    
    int dropId = idDropped(metadata);
    if (dropId > 0) {
        int count = quantityDropped();
        int dropDamage = damageDropped(metadata);
        for (int i = 0; i < count; ++i) {
            auto entity = std::make_unique<EntityItem>(dropId, 1, dropDamage);
            // Spawn slightly above block center so it doesn't get stuck inside
            entity->setPosition(x + 0.5, y + 0.7, z + 0.5);
            entity->worldObj = world;

            std::uniform_real_distribution<double> dist(-0.1, 0.1);
            entity->motionX = dist(world->rand);
            entity->motionY = 0.2;
            entity->motionZ = dist(world->rand);

            world->spawnEntityInWorld(std::move(entity));
        }
    }
}

float Block::checkHardness(EntityPlayer* player) const {
    if (blockHardness < 0.0f) return 0.0f;

    float str = 1.0f;
    bool canHarvest = canHarvestBlock(player);

    if (player) {
        auto* mp = dynamic_cast<EntityPlayerMP*>(player);
        if (mp) {
            canHarvest = mp->inventory.canHarvestBlock(const_cast<Block*>(this));
            ItemStack* held = mp->inventory.getCurrentItem();
            if (held && held->itemID > 0 && held->itemID < 32000) {
                Item* item = Item::itemsList[held->itemID];
                if (auto* tool = dynamic_cast<ItemTool*>(item)) {
                    float toolStr = tool->getStrVsBlock(blockID);
                    if (toolStr > 1.0f) str = toolStr;
                }
            }
        }
    }

    return canHarvest ? str / blockHardness / 30.0f
                      : 1.0f / blockHardness / 100.0f;
}
