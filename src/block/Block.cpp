#include "Block.h"
#include "BlockFire.h"
#include "BlockContainer.h"
#include "BlockChest.h"
#include "BlockFurnace.h"
#include "BlockSign.h"
#include "../core/Material.h"
#include "../core/RustBridge.h"
#include "../core/AxisAlignedBB.h"
#include "../world/World.h"
#include "../world/TileEntity.h"
#include "../world/TileEntityChest.h"
#include "../world/TileEntityFurnace.h"
#include "../world/TileEntitySign.h"
#include "../../rust/alpha_bridge/alpha_bridge.h"
static thread_local World* current_world = nullptr;
#include "../entity/EntityItem.h"
#include "../entity/EntityFallingSand.h"
#include "../entity/EntityPlayerMP.h"
#include "../core/Item.h"
#include "../MinecraftServer.h"
#include <iostream>
#include <random>
#include <queue>

namespace {

constexpr int kPlantGrowthStageMax = 15;

void scheduleBlockTick(World* world, Block* block, int x, int y, int z) {
    if (!world || !block) {
        return;
    }
    world->scheduleBlockUpdate(x, y, z, block->blockID, block->tickRate());
}

bool randomChance(World* world, int oneIn) {
    if (!world || oneIn <= 1) {
        return true;
    }
    std::uniform_int_distribution<int> dist(0, oneIn - 1);
    return dist(world->rand) == 0;
}

bool isChunkAreaLoaded(World* world, int minX, int minZ, int maxX, int maxZ) {
    if (!world) {
        return false;
    }
    for (int chunkX = minX >> 4; chunkX <= maxX >> 4; ++chunkX) {
        for (int chunkZ = minZ >> 4; chunkZ <= maxZ >> 4; ++chunkZ) {
            if (!world->chunkExists(chunkX, chunkZ)) {
                return false;
            }
        }
    }
    return true;
}

void markBlocksForUpdate(World* world, int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
    if (!world) {
        return;
    }

    minY = std::max(0, minY);
    maxY = std::min(CHUNK_SIZE_Y - 1, maxY);
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                world->markBlockNeedsUpdate(x, y, z);
            }
        }
    }
}

} // namespace

class BlockSand : public Block {
public:
    explicit BlockSand(int id) : Block(id) {}

    void onBlockAdded(World* world, int x, int y, int z) override {
        world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
    }

    void updateTick(World* world, int x, int y, int z) override {
        if (y >= 0 && canFallBelow(world, x, y - 1, z)) {
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
    explicit BlockFluid(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }

    bool canCollideCheck(int metadata, bool includeLiquids) const override {
        if (metadata >= 8) {
            metadata = 0;
        }
        return includeLiquids && metadata == 0;
    }

    void onBlockAdded(World* world, int x, int y, int z) override {
        world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
    }

    void updateTick(World* world, int x, int y, int z) override {
        // Lava creates fire on adjacent burnable blocks (Java BlockStationary)
        if (blockMaterial == &Material::lava) {
            if (world->getBlockMetadata(x, y, z) == 0) { // source block or flowing
                for (int side = 0; side < 6; ++side) {
                    static const int dx[6] = {1,-1,0,0,0,0};
                    static const int dy[6] = {0,0,1,-1,0,0};
                    static const int dz[6] = {0,0,0,0,1,-1};
                    int nx = x + dx[side], ny = y + dy[side], nz = z + dz[side];
                    int nid = world->getBlockId(nx, ny, nz);
                    if (nid == 0 && std::rand() % 4 == 0) {
                        // Check if block below allows attachment or is burnable
                        if (world->doesBlockAllowAttachment(nx, ny - 1, nz)
                            || world->getBlockId(nx, ny - 1, nz) == 87) {
                            world->setBlockWithNotify(nx, ny, nz, 51);
                        }
                    }
                }
            }
        }

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
    explicit BlockFlower(int id) : Block(id) {} // props from Rust data table
    bool allowsAttachment() const override { return false; }
    bool canBlockStay(World* world, int x, int y, int z) const override {
        int below = world->getBlockId(x, y - 1, z);
        return (world->getBlockLightValue(x, y, z) >= 8 || world->canBlockSeeSky(x, y, z))
            && (below == 2 || below == 3 || below == 60);
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, 0);
            world->setBlockAndUpdate(x, y, z, 0);
        }
    }
    void updateTick(World* world, int x, int y, int z) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, world->getBlockMetadata(x, y, z));
            world->setBlockWithNotify(x, y, z, 0);
        }
    }
    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};

class BlockTallGrass : public BlockFlower {
public:
    explicit BlockTallGrass(int id) : BlockFlower(id) {}

    void dropBlockAsItemWithChance(World* world, int x, int y, int z, int metadata, float chance) override {
        if (!world || !Item::seeds) {
            return;
        }

        std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
        if (chanceDist(world->rand) > chance) {
            return;
        }

        if (!randomChance(world, 8)) {
            return;
        }

        auto entity = std::make_unique<EntityItem>(Item::seeds->itemID, 1, 0);
        entity->setPosition(x + 0.5, y + 0.5, z + 0.5);
        entity->worldObj = world;
        std::uniform_real_distribution<double> velocityDist(-0.05, 0.05);
        entity->motionX = velocityDist(world->rand);
        entity->motionY = 0.15;
        entity->motionZ = velocityDist(world->rand);
        world->spawnEntityInWorld(std::move(entity));
    }

    int quantityDropped() const override { return 0; }
    int idDropped(int metadata) const override { return 0; }
};

class BlockMushroom : public Block {
public:
    explicit BlockMushroom(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }
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
    explicit BlockTorch(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }

    // Java: doesBlockAllowAttachment = block is solid and opaque
    static bool doesBlockAllowAttachment(World* world, int x, int y, int z) {
        int id = world->getBlockId(x, y, z);
        if (id == 0) return false;
        Block* b = Block::blocksList[id];
        return b && b->blockMaterial->isSolid() && b->isCollidable();
    }

    // Java BlockTorch.onBlockPlaced: sets metadata based on which face was clicked
    // side: 1=bottom(floor), 2=north(+z), 3=south(-z), 4=west(+x), 5=east(-x)
    void onBlockPlaced(World* world, int x, int y, int z, int side) override {
        uint8_t meta = 5; // default: floor
        if (side == 2 && doesBlockAllowAttachment(world, x, y, z + 1)) meta = 4;
        else if (side == 3 && doesBlockAllowAttachment(world, x, y, z - 1)) meta = 3;
        else if (side == 4 && doesBlockAllowAttachment(world, x + 1, y, z)) meta = 2;
        else if (side == 5 && doesBlockAllowAttachment(world, x - 1, y, z)) meta = 1;
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
        if      (doesBlockAllowAttachment(world, x - 1, y, z)) meta = 1;
        else if (doesBlockAllowAttachment(world, x + 1, y, z)) meta = 2;
        else if (doesBlockAllowAttachment(world, x, y, z - 1)) meta = 3;
        else if (doesBlockAllowAttachment(world, x, y, z + 1)) meta = 4;
        else if (doesBlockAllowAttachment(world, x, y - 1, z)) meta = 5;
        if (meta != 0)
            world->setBlockAndMetadata(x, y, z, blockID, meta);
        // No markBlockNeedsUpdate — setBlockWithNotify calls it after us with final metadata
    }

    bool canBlockStay(World* world, int x, int y, int z) const override {
        return doesBlockAllowAttachment(world, x-1, y, z) || doesBlockAllowAttachment(world, x+1, y, z)
            || doesBlockAllowAttachment(world, x, y, z-1) || doesBlockAllowAttachment(world, x, y, z+1)
            || doesBlockAllowAttachment(world, x, y-1, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        uint8_t meta = world->getBlockMetadata(x, y, z);
        bool detach = false;
        if (meta == 1 && !doesBlockAllowAttachment(world, x - 1, y, z)) detach = true;
        if (meta == 2 && !doesBlockAllowAttachment(world, x + 1, y, z)) detach = true;
        if (meta == 3 && !doesBlockAllowAttachment(world, x, y, z - 1)) detach = true;
        if (meta == 4 && !doesBlockAllowAttachment(world, x, y, z + 1)) detach = true;
        if (meta == 5 && !doesBlockAllowAttachment(world, x, y - 1, z)) detach = true;
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
    explicit BlockCactus(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }
    void onBlockAdded(World* world, int x, int y, int z) override {
        scheduleBlockTick(world, this, x, y, z);
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, world->getBlockMetadata(x, y, z));
            world->setBlockWithNotify(x, y, z, 0);
            return;
        }
        scheduleBlockTick(world, this, x, y, z);
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
    void updateTick(World* world, int x, int y, int z) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, world->getBlockMetadata(x, y, z));
            world->setBlockWithNotify(x, y, z, 0);
            return;
        }

        if (world->getBlockId(x, y + 1, z) != 0) {
            scheduleBlockTick(world, this, x, y, z);
            return;
        }

        int height = 1;
        while (world->getBlockId(x, y - height, z) == blockID) {
            ++height;
        }

        if (height < 3) {
            uint8_t age = world->getBlockMetadata(x, y, z);
            if (age >= kPlantGrowthStageMax) {
                world->setBlockWithNotify(x, y + 1, z, blockID);
                world->setBlockMetadata(x, y, z, 0);
            } else {
                world->setBlockMetadata(x, y, z, static_cast<uint8_t>(age + 1));
            }
        }

        scheduleBlockTick(world, this, x, y, z);
    }
    int tickRate() const override { return 20; }
};

class BlockReed : public Block {
public:
    explicit BlockReed(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }
    void onBlockAdded(World* world, int x, int y, int z) override {
        scheduleBlockTick(world, this, x, y, z);
    }
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
            world->setBlockWithNotify(x, y, z, 0);
            return;
        }
        scheduleBlockTick(world, this, x, y, z);
    }
    void updateTick(World* world, int x, int y, int z) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, 0);
            world->setBlockWithNotify(x, y, z, 0);
            return;
        }

        if (world->getBlockId(x, y + 1, z) != 0) {
            scheduleBlockTick(world, this, x, y, z);
            return;
        }

        int height = 1;
        while (world->getBlockId(x, y - height, z) == blockID) {
            ++height;
        }

        if (height < 3) {
            uint8_t age = world->getBlockMetadata(x, y, z);
            if (age >= kPlantGrowthStageMax) {
                world->setBlockWithNotify(x, y + 1, z, blockID);
                world->setBlockMetadata(x, y, z, 0);
            } else {
                world->setBlockMetadata(x, y, z, static_cast<uint8_t>(age + 1));
            }
        }

        scheduleBlockTick(world, this, x, y, z);
    }
    int tickRate() const override { return 20; }
    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};


Block* Block::blocksList[256] = {nullptr};
bool Block::tickOnLoad[256] = {false};
bool Block::isBlockContainer[256] = {false};
int Block::lightOpacity[256] = {0};
int Block::lightValue[256] = {0};
bool Block::allowsAttachmentArr[256] = {false};

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

Block::Block(int id)
    : blockID(id), blockMaterial(nullptr), blockHardness(0.0f), blockResistance(0.0f) {
    if (blocksList[id] != nullptr) {
        throw std::runtime_error("Block slot " + std::to_string(id) + " is already occupied!");
    }
    blocksList[id] = this;
    auto props = RustBridge::blockProperties(id);
    blockHardness = props.hardness;
    blockResistance = props.resistance;
    blockMaterial = RustBridge::materialFromId(props.material);
    lightOpacity[id] = props.light_opacity;
    lightValue[id] = props.light_value;
    tickOnLoad[id] = props.tick_on_load;
    isBlockContainer[id] = props.is_block_container;
    setBlockBounds(props.min_x, props.min_y, props.min_z,
                   props.max_x, props.max_y, props.max_z);
}

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

int Block::idDropped(int metadata) const {
    auto props = RustBridge::blockProperties(blockID);
    return props.id_dropped ? props.id_dropped : blockID;
}

bool Block::canHarvestBlock(EntityPlayer* player) const {
    auto props = RustBridge::blockProperties(blockID);
    return props.can_harvest_block;
}

class BlockLeaves : public Block {
public:
    explicit BlockLeaves(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }
    void onBlockAdded(World* world, int x, int y, int z) override {
        scheduleBlockTick(world, this, x, y, z);
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        field_663_c = 0;
        updateLeafDistance(world, x, y, z);
    }
    void updateTick(World* world, int x, int y, int z) override {
        if (!world) {
            return;
        }

        int metadata = world->getBlockMetadata(x, y, z);
        if (metadata == 0) {
            field_663_c = 0;
            updateLeafDistance(world, x, y, z);
        } else if (metadata == 1) {
            dropBlockAsItem(world, x, y, z, metadata);
            world->setBlockWithNotify(x, y, z, 0);
        } else if (randomChance(world, 10)) {
            updateLeafDistance(world, x, y, z);
        }
    }
    int tickRate() const override { return 40; }
    void dropBlockAsItemWithChance(World* world, int x, int y, int z, int metadata, float chance) override {
        if (!world || !Item::itemsList[6]) {
            return;
        }

        std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
        if (chanceDist(world->rand) > chance) {
            return;
        }

        if (!randomChance(world, 20)) {
            return;
        }

        auto entity = std::make_unique<EntityItem>(6, 1, 0);
        entity->setPosition(x + 0.5, y + 0.5, z + 0.5);
        entity->worldObj = world;
        std::uniform_real_distribution<double> velocityDist(-0.05, 0.05);
        entity->motionX = velocityDist(world->rand);
        entity->motionY = 0.15;
        entity->motionZ = velocityDist(world->rand);
        world->spawnEntityInWorld(std::move(entity));
    }
    int quantityDropped() const override { return 0; }
    int idDropped(int metadata) const override { return 0; }

private:
    int field_663_c = 0;

    void updateLeafDistance(World* world, int x, int y, int z) {
        if (!world || field_663_c++ >= 100) {
            return;
        }

        // FIX 2: In Alpha 1.2.6, leaves on solid ground don't decay!
        int candidate = 0;
        Material* materialBelow = world->getBlockMaterialNoChunkLoad(x, y - 1, z);
        if (materialBelow && materialBelow->isSolid()) {
            candidate = 16;
        }

        int metadata = world->getBlockMetadata(x, y, z);
        if (metadata == 0) {
            metadata = 1;
            world->setBlockMetadata(x, y, z, 1);
            world->markBlockNeedsUpdate(x, y, z);
        }

        // Check neighbors (Notch forgot y+1, but we keep it for canopy reliability)
        candidate = propagateLeafDistance(world, x - 1, y, z, candidate);
        candidate = propagateLeafDistance(world, x + 1, y, z, candidate);
        candidate = propagateLeafDistance(world, x, y - 1, z, candidate);
        candidate = propagateLeafDistance(world, x, y + 1, z, candidate);
        candidate = propagateLeafDistance(world, x, y, z - 1, candidate);
        candidate = propagateLeafDistance(world, x, y, z + 1, candidate);

        int newMetadata = candidate - 1;
        if (newMetadata < 10) {
            newMetadata = 1; // Charge depleted, order to decay
        }

        if (newMetadata != metadata) {
            world->setBlockMetadata(x, y, z, static_cast<uint8_t>(newMetadata));
            world->markBlockNeedsUpdate(x, y, z); // Packet to client
            
            // FIX 1: CRITICAL!
            // Force neighbors to recalculate their light/life,
            // otherwise the flood-fill chain reaction stops here!
            world->notifyBlocksOfNeighborChange(x, y, z, blockID);

            // Specific decay propagation when losing connection to log
            updateNeighborLeafDistance(world, x - 1, y, z, metadata);
            updateNeighborLeafDistance(world, x + 1, y, z, metadata);
            updateNeighborLeafDistance(world, x, y - 1, z, metadata);
            updateNeighborLeafDistance(world, x, y + 1, z, metadata);
            updateNeighborLeafDistance(world, x, y, z - 1, metadata);
            updateNeighborLeafDistance(world, x, y, z + 1, metadata);
        }
    }

    int propagateLeafDistance(World* world, int x, int y, int z, int current) const {
        const int blockId = world->getBlockIdNoChunkLoad(x, y, z);
        
        if (blockId == 17) { // 17 = Log
            return 16; // Maximum life charge
        }
        
        if (blockId == blockID) { // Neighboring leaf
            const int metadata = world->getBlockMetadata(x, y, z);
            // If neighbor is alive and its charge is greater than ours, adopt it
            if (metadata != 0 && metadata > current) {
                return metadata;
            }
        }

        return current;
    }

    void updateNeighborLeafDistance(World* world, int x, int y, int z, int previousMetadata) {
        if (world->getBlockIdNoChunkLoad(x, y, z) != blockID) {
            return;
        }

        const int metadata = world->getBlockMetadata(x, y, z);
        if (metadata != 0 && metadata == previousMetadata - 1) {
            updateLeafDistance(world, x, y, z);
        }
    }
};

class BlockSapling : public Block {
public:
    explicit BlockSapling(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }

    void onBlockAdded(World* world, int x, int y, int z) override {
        scheduleBlockTick(world, this, x, y, z);
    }

    bool canBlockStay(World* world, int x, int y, int z) const override {
        const int below = world->getBlockId(x, y - 1, z);
        return (world->getBlockLightValue(x, y, z) >= 8 || world->canBlockSeeSky(x, y, z))
            && (below == 2 || below == 3 || below == 60);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, 0);
            world->setBlockWithNotify(x, y, z, 0);
            return;
        }
        scheduleBlockTick(world, this, x, y, z);
    }

    void updateTick(World* world, int x, int y, int z) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItem(world, x, y, z, 0);
            world->setBlockWithNotify(x, y, z, 0);
            return;
        }

        if (world->getBlockLightValue(x, y + 1, z) < 9 || !randomChance(world, 5)) {
            scheduleBlockTick(world, this, x, y, z);
            return;
        }

        const uint8_t metadata = world->getBlockMetadata(x, y, z);
        if (metadata < 15) {
            world->setBlockMetadata(x, y, z, static_cast<uint8_t>(metadata + 1));
            world->markBlockNeedsUpdate(x, y, z);
            scheduleBlockTick(world, this, x, y, z);
            return;
        }

        int64_t treeSeed = static_cast<int64_t>(world->rand());
        world->setBlockWithNotify(x, y, z, 0);

        current_world = world;

        WorldAccessor accessor {
            .get_block_id = [](int32_t x, int32_t y, int32_t z) -> uint8_t {
                return current_world->getBlockId(x, y, z);
            },
            .set_block_id = [](int32_t x, int32_t y, int32_t z, uint8_t id) {
                current_world->setBlock(x, y, z, id);
            },
            .get_block_meta = [](int32_t x, int32_t y, int32_t z) -> uint8_t {
                return current_world->getBlockMetadata(x, y, z);
            },
            .set_block_meta = [](int32_t x, int32_t y, int32_t z, uint8_t meta) {
                current_world->setBlockMetadata(x, y, z, meta);
            },
            .allows_attachment = [](int32_t x, int32_t y, int32_t z) -> bool {
                int id = current_world->getBlockId(x, y, z);
                return id >= 0 && id < 256 && Block::allowsAttachmentArr[id];
            },
            .is_block_solid = [](int32_t x, int32_t y, int32_t z) -> bool {
                return current_world->isBlockSolid(x, y, z);
            },
            .get_height_value = [](int32_t x, int32_t z) -> int32_t {
                return current_world->getHeightValue(x, z);
            }
        };

        bool generated = false;
        if (randomChance(world, 10)) {
            generated = alpha_generate_big_tree(accessor, treeSeed, x, y, z);
        }
        if (!generated) {
            generated = alpha_generate_tree(accessor, treeSeed, x, y, z);
        }

        current_world = nullptr;

        if (!generated) {
            world->setBlockWithNotify(x, y, z, blockID);
            return;
        }

        markBlocksForUpdate(world, x - 8, y - 1, z - 8, x + 8, y + 16, z + 8);
    }

    int tickRate() const override { return 100; }
    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};

class BlockCrops : public Block {
public:
    explicit BlockCrops(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }

    void onBlockAdded(World* world, int x, int y, int z) override {
        scheduleBlockTick(world, this, x, y, z);
    }

    bool canBlockStay(World* world, int x, int y, int z) const override {
        return world->getBlockId(x, y - 1, z) == 60
            && (world->getBlockLightValue(x, y, z) >= 8 || world->canBlockSeeSky(x, y, z));
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItemWithChance(world, x, y, z, world->getBlockMetadata(x, y, z), 1.0f);
            world->setBlockWithNotify(x, y, z, 0);
            return;
        }
        scheduleBlockTick(world, this, x, y, z);
    }

    void updateTick(World* world, int x, int y, int z) override {
        if (!canBlockStay(world, x, y, z)) {
            dropBlockAsItemWithChance(world, x, y, z, world->getBlockMetadata(x, y, z), 1.0f);
            world->setBlockWithNotify(x, y, z, 0);
            return;
        }

        if (world->getBlockLightValue(x, y + 1, z) >= 9) {
            const int metadata = world->getBlockMetadata(x, y, z);
            if (metadata < 7) {
                float growthRate = 1.0f;
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        float soilBonus = 0.0f;
                        if (world->getBlockId(x + dx, y - 1, z + dz) == 60) {
                            soilBonus = (dx == 0 && dz == 0) ? 3.0f : 1.0f;
                            if (dx != 0 || dz != 0) {
                                soilBonus /= 4.0f;
                            }
                        }
                        growthRate += soilBonus;
                    }
                }

                const bool sameRow = world->getBlockId(x - 1, y, z) == blockID || world->getBlockId(x + 1, y, z) == blockID;
                const bool sameColumn = world->getBlockId(x, y, z - 1) == blockID || world->getBlockId(x, y, z + 1) == blockID;
                const bool sameDiagonal =
                    world->getBlockId(x - 1, y, z - 1) == blockID || world->getBlockId(x + 1, y, z - 1) == blockID ||
                    world->getBlockId(x + 1, y, z + 1) == blockID || world->getBlockId(x - 1, y, z + 1) == blockID;
                if (sameDiagonal || (sameRow && sameColumn)) {
                    growthRate /= 2.0f;
                }

                const int growthChance = std::max(2, static_cast<int>(100.0f / growthRate));
                std::uniform_int_distribution<int> dist(0, growthChance - 1);
                if (dist(world->rand) == 0) {
                    world->setBlockMetadata(x, y, z, static_cast<uint8_t>(metadata + 1));
                    world->markBlockNeedsUpdate(x, y, z);
                }
            }
        }

        scheduleBlockTick(world, this, x, y, z);
    }

    int tickRate() const override { return 20; }

    void dropBlockAsItemWithChance(World* world, int x, int y, int z, int metadata, float chance) override {
        if (!world || !Item::seeds || !Item::wheat) {
            return;
        }

        std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
        if (chanceDist(world->rand) > chance) {
            return;
        }

        auto spawnDrop = [&](int itemId, int count) {
            if (count <= 0) {
                return;
            }
            auto entity = std::make_unique<EntityItem>(itemId, count, 0);
            entity->setPosition(x + 0.5, y + 0.5, z + 0.5);
            entity->worldObj = world;
            std::uniform_real_distribution<double> velocityDist(-0.05, 0.05);
            entity->motionX = velocityDist(world->rand);
            entity->motionY = 0.15;
            entity->motionZ = velocityDist(world->rand);
            world->spawnEntityInWorld(std::move(entity));
        };

        if (metadata >= 7) {
            spawnDrop(Item::wheat->itemID, 1);
            std::uniform_int_distribution<int> seedsDist(0, 2);
            spawnDrop(Item::seeds->itemID, 1 + seedsDist(world->rand));
        } else {
            spawnDrop(Item::seeds->itemID, 1);
        }
    }

    int quantityDropped() const override { return 0; }
    int idDropped(int metadata) const override { return 0; }
    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};

class BlockSoil : public Block {
public:
    explicit BlockSoil(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }

    void onBlockAdded(World* world, int x, int y, int z) override {
        scheduleBlockTick(world, this, x, y, z);
    }

    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int x, int y, int z) override {
        return AxisAlignedBB::getBoundingBox(x, y, z, x + 1, y + 1, z + 1);
    }

    void updateTick(World* world, int x, int y, int z) override {
        if (!world) {
            return;
        }

        if (randomChance(world, 5)) {
            if (hasNearbyWater(world, x, y, z)) {
                setMoisture(world, x, y, z, 7);
            } else {
                const int moisture = world->getBlockMetadata(x, y, z);
                if (moisture > 0) {
                    setMoisture(world, x, y, z, moisture - 1);
                } else if (!hasCrops(world, x, y, z)) {
                    world->setBlockWithNotify(x, y, z, 3);
                    return;
                }
            }
        }

        scheduleBlockTick(world, this, x, y, z);
    }

    void onEntityWalking(World* world, int x, int y, int z, Entity* entity) override {
        if (!world || !entity) {
            return;
        }
        if (randomChance(world, 4)) {
            world->setBlockWithNotify(x, y, z, 3);
        }
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!world) {
            return;
        }

        const int aboveId = world->getBlockId(x, y + 1, z);
        Block* aboveBlock = (aboveId > 0 && aboveId < 256) ? Block::blocksList[aboveId] : nullptr;
        if (aboveBlock && aboveBlock->isCollidable()
            && aboveBlock->getCollisionBoundingBoxFromPool(world, x, y + 1, z).has_value()) {
            world->setBlockWithNotify(x, y, z, 3);
            return;
        }

        scheduleBlockTick(world, this, x, y, z);
    }

    int idDropped(int metadata) const override { return 3; }

private:
    static bool hasNearbyWater(World* world, int x, int y, int z) {
        for (int checkX = x - 4; checkX <= x + 4; ++checkX) {
            for (int checkY = y; checkY <= y + 1; ++checkY) {
                for (int checkZ = z - 4; checkZ <= z + 4; ++checkZ) {
                    Material* material = world->getBlockMaterial(checkX, checkY, checkZ);
                    if (material == &Material::water) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    static bool hasCrops(World* world, int x, int y, int z) {
        return world->getBlockId(x, y + 1, z) == 59;
    }

    static void setMoisture(World* world, int x, int y, int z, int moisture) {
        if (world->getBlockMetadata(x, y, z) == moisture) {
            return;
        }
        world->setBlockMetadata(x, y, z, static_cast<uint8_t>(moisture));
        world->markBlockNeedsUpdate(x, y, z);
    }
};

void Block::initBlocks() {
    for (int id = 1; id < 256; ++id) {
        auto props = RustBridge::blockProperties(id);
        if (props.material == BLOCK_MATERIAL_AIR) continue;

        switch (id) {
            case 51: blocksList[51] = new BlockFire(51, 0); break;
            case 54: blocksList[54] = new BlockChest(54); break;
            case 61: blocksList[61] = new BlockFurnace(61, false); break;
            case 62: blocksList[62] = new BlockFurnace(62, true); break;
            case 63: blocksList[63] = new BlockSign(63, false); break;
            case 68: blocksList[68] = new BlockSign(68, true); break;
            default:
                switch (static_cast<BlockTypeId>(props.block_type)) {
                    case BLOCK_TYPE_SAND: blocksList[id] = new BlockSand(id); break;
                    case BLOCK_TYPE_FLUID: blocksList[id] = new BlockFluid(id); break;
                    case BLOCK_TYPE_FLOWER: blocksList[id] = new BlockFlower(id); break;
                    case BLOCK_TYPE_TALL_GRASS: blocksList[id] = new BlockTallGrass(id); break;
                    case BLOCK_TYPE_MUSHROOM: blocksList[id] = new BlockMushroom(id); break;
                    case BLOCK_TYPE_TORCH: blocksList[id] = new BlockTorch(id); break;
                    case BLOCK_TYPE_CACTUS: blocksList[id] = new BlockCactus(id); break;
                    case BLOCK_TYPE_REED: blocksList[id] = new BlockReed(id); break;
                    case BLOCK_TYPE_LEAVES: blocksList[id] = new BlockLeaves(id); break;
                    case BLOCK_TYPE_SAPLING: blocksList[id] = new BlockSapling(id); break;
                    case BLOCK_TYPE_CROPS: blocksList[id] = new BlockCrops(id); break;
                    case BLOCK_TYPE_SOIL: blocksList[id] = new BlockSoil(id); break;
                    default: blocksList[id] = new Block(id); break;
                }
        }
    }

    // Assign static pointers
    stone = blocksList[1];
    grass = blocksList[2];
    dirt = blocksList[3];
    cobblestone = blocksList[4];
    planks = blocksList[5];
    sapling = blocksList[6];
    bedrock = blocksList[7];
    sand = blocksList[12];
    gravel = blocksList[13];
    oreGold = blocksList[14];
    oreIron = blocksList[15];
    oreCoal = blocksList[16];
    wood = blocksList[17];
    leaves = blocksList[18];
    glass = blocksList[20];
    plantYellow = blocksList[37];
    plantRed = blocksList[38];
    mushroomBrown = blocksList[39];
    mushroomRed = blocksList[40];
    cobblestoneMossy = blocksList[48];
    oreDiamond = blocksList[56];
    oreRedstone = blocksList[73];
    snow = blocksList[78];
    ice = blocksList[79];
    cactus = blocksList[81];
    blockClay = blocksList[82];
    reed = blocksList[83];
    pumpkin = blocksList[86];

    // TileEntities are registered via REGISTER_TILE_ENTITY macros in their headers

    // Java: field_540_p — populate allowsAttachment array
    for (int i = 0; i < 256; ++i) {
        allowsAttachmentArr[i] = (blocksList[i] != nullptr && blocksList[i]->allowsAttachment());
    }

    const std::vector<int> nonAttachingBlocks = {
        6, 8, 9, 10, 11, 20, 27, 28, 30, 31, 32, 37, 38, 39, 40,
        44, 50, 51, 52, 53, 55, 59, 60, 63, 64, 65, 66, 67, 68,
        69, 70, 71, 72, 75, 76, 77, 78, 79, 81, 83, 85, 90
    };
    for (int id : nonAttachingBlocks) {
        allowsAttachmentArr[id] = false;
    }

    // Adjust lightOpacity post-init to match Java
    for (int i = 0; i < 256; ++i) {
        if (blocksList[i] != nullptr) {
            bool hasExplicit = (i == 6 || i == 8 || i == 9 || i == 10 || i == 11 || i == 18 || i == 20 ||
                                i == 30 || i == 31 || i == 32 || i == 37 || i == 38 || i == 39 || i == 40 ||
                                i == 44 || i == 50 || i == 53 || i == 55 || i == 59 || i == 60 || i == 63 ||
                                i == 67 || i == 68 || i == 79 || i == 81 || i == 83);
            if (!hasExplicit && !allowsAttachmentArr[i]) {
                lightOpacity[i] = 0;
            }
        }
    }

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
