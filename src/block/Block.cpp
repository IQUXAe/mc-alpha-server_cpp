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
#include "BlockTickWorld.h"
thread_local World* gBlockTickWorld = nullptr;
#include "../entity/EntityItem.h"
#include "../entity/EntityFallingSand.h"
#include "../core/Item.h"
#include "../MinecraftServer.h"
#include <iostream>
#include <random>
#include <queue>

namespace {

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

// Block-behavior FFI trampolines (declared in BlockTickWorld.h).
// Guard and table builders live in the header; this file owns the definitions.

extern "C" int32_t blockTickNextInt(int32_t bound) {
    std::uniform_int_distribution<int> dist(0, bound - 1);
    return dist(gBlockTickWorld->rand);
}

extern "C" float blockTickNextFloat01() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(gBlockTickWorld->rand);
}

extern "C" uint64_t blockTickNextU64() {
    return gBlockTickWorld->rand();
}

extern "C" uint8_t blockTickGetId(int32_t x, int32_t y, int32_t z) {
    return gBlockTickWorld->getBlockId(x, y, z);
}

extern "C" uint8_t blockTickGetIdNc(int32_t x, int32_t y, int32_t z) {
    return gBlockTickWorld->getBlockIdNoChunkLoad(x, y, z);
}

extern "C" uint8_t blockTickGetMeta(int32_t x, int32_t y, int32_t z) {
    return gBlockTickWorld->getBlockMetadata(x, y, z);
}

extern "C" void blockTickSet(int32_t x, int32_t y, int32_t z, uint8_t id) {
    gBlockTickWorld->setBlock(x, y, z, id);
}

extern "C" void blockTickSetMeta(int32_t x, int32_t y, int32_t z, uint8_t meta) {
    gBlockTickWorld->setBlockMetadata(x, y, z, meta);
}

extern "C" void blockTickSetNotify(int32_t x, int32_t y, int32_t z, uint8_t id) {
    gBlockTickWorld->setBlockWithNotify(x, y, z, id);
}

extern "C" void blockTickSetUpdate(int32_t x, int32_t y, int32_t z, uint8_t id) {
    gBlockTickWorld->setBlockAndUpdate(x, y, z, id);
}

extern "C" void blockTickSetMetaNotify(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t meta) {
    gBlockTickWorld->setBlockAndMetadataWithNotify(x, y, z, id, meta);
}

extern "C" void blockTickSetAndMeta(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t meta) {
    gBlockTickWorld->setBlockAndMetadata(x, y, z, id, meta);
}

extern "C" int32_t blockTickLight(int32_t x, int32_t y, int32_t z) {
    return gBlockTickWorld->getBlockLightValue(x, y, z);
}

extern "C" bool blockTickSeeSky(int32_t x, int32_t y, int32_t z) {
    return gBlockTickWorld->canBlockSeeSky(x, y, z);
}

extern "C" bool blockTickAttachWorld(int32_t x, int32_t y, int32_t z) {
    return gBlockTickWorld->doesBlockAllowAttachment(x, y, z);
}

extern "C" bool blockTickAttachTorch(int32_t x, int32_t y, int32_t z) {
    int id = gBlockTickWorld->getBlockId(x, y, z);
    if (id == 0) return false;
    Block* b = Block::blocksList[id];
    return b && b->blockMaterial->isSolid() && b->isCollidable();
}

extern "C" bool blockTickIsSolid(int32_t x, int32_t y, int32_t z) {
    int id = gBlockTickWorld->getBlockId(x, y, z);
    if (id == 0) return false;
    Block* b = Block::blocksList[id];
    return b && b->blockMaterial->isSolid();
}

extern "C" bool blockTickIsSolidNc(int32_t x, int32_t y, int32_t z) {
    Material* material = gBlockTickWorld->getBlockMaterialNoChunkLoad(x, y, z);
    return material && material->isSolid();
}

extern "C" bool blockTickWaterLava(int32_t x, int32_t y, int32_t z) {
    int id = gBlockTickWorld->getBlockId(x, y, z);
    if (id == 0 || id == 51) return true;
    Block* b = Block::blocksList[id];
    if (!b) return true;
    return b->blockMaterial == &Material::water || b->blockMaterial == &Material::lava;
}

extern "C" bool blockTickIsWater(int32_t x, int32_t y, int32_t z) {
    return gBlockTickWorld->getBlockMaterial(x, y, z) == &Material::water;
}

extern "C" bool blockTickRegistered(uint8_t id) {
    return Block::blocksList[id] != nullptr;
}

extern "C" bool blockTickCollidableBox(int32_t x, int32_t y, int32_t z) {
    const int aboveId = gBlockTickWorld->getBlockId(x, y, z);
    Block* aboveBlock = (aboveId > 0 && aboveId < 256) ? Block::blocksList[aboveId] : nullptr;
    return aboveBlock && aboveBlock->isCollidable()
        && aboveBlock->getCollisionBoundingBoxFromPool(gBlockTickWorld, x, y, z).has_value();
}

extern "C" void blockTickSchedule(int32_t x, int32_t y, int32_t z, uint8_t id, int32_t delay) {
    gBlockTickWorld->scheduleBlockUpdate(x, y, z, id, delay);
}

extern "C" void blockTickMark(int32_t x, int32_t y, int32_t z) {
    gBlockTickWorld->markBlockNeedsUpdate(x, y, z);
}

extern "C" void blockTickNotifyNeighbors(int32_t x, int32_t y, int32_t z, uint8_t id) {
    gBlockTickWorld->notifyBlocksOfNeighborChange(x, y, z, id);
}

extern "C" void blockTickSpawnDrop(int32_t itemId, int32_t count, int32_t damage,
                                   double fx, double fy, double fz, double spread, double up) {
    auto entity = std::make_unique<EntityItem>(itemId, count, damage);
    entity->setPosition(fx, fy, fz);
    entity->worldObj = gBlockTickWorld;
    std::uniform_real_distribution<double> dist(-spread, spread);
    entity->motionX = dist(gBlockTickWorld->rand);
    entity->motionY = up;
    entity->motionZ = dist(gBlockTickWorld->rand);
    gBlockTickWorld->spawnEntityInWorld(std::move(entity));
}

extern "C" void blockTickSpawnFalling(uint8_t blockId, double fx, double fy, double fz) {
    auto entity = std::make_unique<EntityFallingSand>(blockId, fx, fy, fz);
    gBlockTickWorld->spawnEntityInWorld(std::move(entity));
}

extern "C" void blockTickDropOccupant(int32_t x, int32_t y, int32_t z) {
    int id = gBlockTickWorld->getBlockId(x, y, z);
    if (id > 0 && Block::blocksList[id]) {
        Block::blocksList[id]->dropBlockAsItem(gBlockTickWorld, x, y, z, gBlockTickWorld->getBlockMetadata(x, y, z));
    }
}

extern "C" void blockTickDetonateTnt(int32_t x, int32_t y, int32_t z) {
    if (Block::blocksList[46]) {
        Block::blocksList[46]->onBlockDestroyedByPlayer(gBlockTickWorld, x, y, z, 0);
    }
}

extern "C" int32_t scatterNextInt(int32_t bound) {
    return RustBridge::rngNextInt(bound);
}

extern "C" float scatterNextFloat01() {
    return RustBridge::rngNextFloat();
}

extern "C" double scatterNextFloat64() {
    return RustBridge::rngNextDouble();
}

extern "C" void scatterSpawnItem(int32_t itemId, int32_t count, int32_t damage, double fx, double fy,
                                 double fz, double mx, double my, double mz) {
    auto entity = std::make_unique<EntityItem>(itemId, count, damage);
    entity->setPosition(fx, fy, fz);
    entity->worldObj = gBlockTickWorld;
    entity->motionX = mx;
    entity->motionY = my;
    entity->motionZ = mz;
    gBlockTickWorld->spawnEntityInWorld(std::move(entity));
}

extern "C" uint8_t scatterGetBlockId(int32_t x, int32_t y, int32_t z) {
    return gBlockTickWorld->getBlockId(x, y, z);
}

class BlockSand : public Block {
public:
    explicit BlockSand(int id) : Block(id) {}

    void onBlockAdded(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockSandAdded(&blockTickWorld(), blockID, x, y, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        RustBridge::blockSandNeighbor(&blockTickWorld(), blockID, x, y, z);
    }

    void updateTick(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockSandTick(&blockTickWorld(), blockID, x, y, z);
    }

    int tickRate() const override { return 3; }
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
        BlockTickGuard guard(world);
        RustBridge::blockFluidAdded(&blockTickWorld(), blockID, tickRate(), x, y, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        RustBridge::blockFluidNeighbor(&blockTickWorld(), blockID, tickRate(), x, y, z);
    }

    void updateTick(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockFluidTick(&blockTickWorld(), blockID, blockMaterial == &Material::lava, x, y, z);
    }

    int tickRate() const override {
        return blockMaterial == &Material::water ? 5 : 30;
    }

    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World* world, int x, int y, int z) override {
        return std::nullopt; // Liquids have no collision box
    }

    bool isCollidable() const override { return false; }
};

class BlockFlower : public Block {
public:
    explicit BlockFlower(int id) : Block(id) {} // props from Rust data table
    bool allowsAttachment() const override { return false; }
    bool canBlockStay(World* world, int x, int y, int z) const override {
        BlockTickGuard guard(world);
        return RustBridge::blockFlowerCanStay(&blockTickWorld(), x, y, z);
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        RustBridge::blockFlowerNeighbor(&blockTickWorld(), idDropped(0), quantityDropped(), damageDropped(0), x, y, z);
    }
    void updateTick(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockFlowerTick(&blockTickWorld(), idDropped(world->getBlockMetadata(x, y, z)),
                                    quantityDropped(), damageDropped(world->getBlockMetadata(x, y, z)), x, y, z);
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
        BlockTickGuard guard(world);
        RustBridge::blockTallgrassDrop(&blockTickWorld(), Item::seeds->itemID, chance, x, y, z);
    }

    int quantityDropped() const override { return 0; }
    int idDropped(int metadata) const override { return 0; }
};

class BlockMushroom : public Block {
public:
    explicit BlockMushroom(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }
    bool canBlockStay(World* world, int x, int y, int z) const override {
        BlockTickGuard guard(world);
        return RustBridge::blockMushroomCanStay(&blockTickWorld(), x, y, z);
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        RustBridge::blockMushroomNeighbor(&blockTickWorld(), idDropped(0), quantityDropped(), damageDropped(0), x, y, z);
    }
    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};

class BlockTorch : public Block {
public:
    explicit BlockTorch(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }

    // Java BlockTorch.onBlockPlaced: sets metadata based on which face was clicked
    // side: 1=bottom(floor), 2=north(+z), 3=south(-z), 4=west(+x), 5=east(-x)
    void onBlockPlaced(World* world, int x, int y, int z, int side) override {
        BlockTickGuard guard(world);
        uint8_t meta = RustBridge::blockTorchAttachMeta(&blockTickWorld(), side, x, y, z);
        world->setBlockAndMetadata(x, y, z, blockID, meta);
        // markBlockNeedsUpdate is called by handlePlace after onBlockPlaced returns
    }

    void onBlockAdded(World* world, int x, int y, int z) override {
        // Only auto-detect attachment when metadata is 0 AND no neighbors triggered
        // onBlockPlaced yet. This handles world-gen torches and chunk loading.
        // When placed by player, onBlockPlaced sets metadata after us —
        // so we must NOT send markBlockNeedsUpdate here to avoid the flicker.
        BlockTickGuard guard(world);
        RustBridge::blockTorchAdded(&blockTickWorld(), blockID, x, y, z);
        // No markBlockNeedsUpdate — setBlockWithNotify calls it after us with final metadata
    }

    bool canBlockStay(World* world, int x, int y, int z) const override {
        BlockTickGuard guard(world);
        return RustBridge::blockTorchCanStay(&blockTickWorld(), x, y, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        const uint8_t meta = world->getBlockMetadata(x, y, z);
        RustBridge::blockTorchNeighbor(&blockTickWorld(), idDropped(meta), quantityDropped(), damageDropped(meta), x, y, z);
    }

    bool isReplaceable() const override { return true; }
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override { return std::nullopt; }
};

class BlockCactus : public Block {
public:
    explicit BlockCactus(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }
    void onBlockAdded(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockCactusAdded(&blockTickWorld(), blockID, x, y, z);
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        const uint8_t meta = world->getBlockMetadata(x, y, z);
        RustBridge::blockCactusNeighbor(&blockTickWorld(), blockID, idDropped(meta), quantityDropped(), damageDropped(meta), x, y, z);
    }
    bool canBlockStay(World* world, int x, int y, int z) const override {
        BlockTickGuard guard(world);
        return RustBridge::blockCactusCanStay(&blockTickWorld(), x, y, z);
    }
    void updateTick(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        const uint8_t meta = world->getBlockMetadata(x, y, z);
        RustBridge::blockCactusTick(&blockTickWorld(), blockID, idDropped(meta), quantityDropped(), damageDropped(meta), x, y, z);
    }
    int tickRate() const override { return 20; }
};

class BlockReed : public Block {
public:
    explicit BlockReed(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }
    void onBlockAdded(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockReedAdded(&blockTickWorld(), blockID, x, y, z);
    }
    bool canBlockStay(World* world, int x, int y, int z) const override {
        BlockTickGuard guard(world);
        return RustBridge::blockReedCanStay(&blockTickWorld(), x, y, z);
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        RustBridge::blockReedNeighbor(&blockTickWorld(), blockID, idDropped(0), quantityDropped(), damageDropped(0), x, y, z);
    }
    void updateTick(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockReedTick(&blockTickWorld(), blockID, idDropped(0), quantityDropped(), damageDropped(0), x, y, z);
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
        BlockTickGuard guard(world);
        RustBridge::blockLeavesAdded(&blockTickWorld(), blockID, x, y, z);
    }
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        RustBridge::blockLeavesNeighbor(&blockTickWorld(), blockID, blockID, &field_663_c, x, y, z);
    }
    void updateTick(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        const uint8_t meta = world->getBlockMetadata(x, y, z);
        RustBridge::blockLeavesTick(&blockTickWorld(), blockID, blockID, idDropped(meta), quantityDropped(), damageDropped(meta), &field_663_c, x, y, z);
    }
    int tickRate() const override { return 40; }
    void dropBlockAsItemWithChance(World* world, int x, int y, int z, int metadata, float chance) override {
        if (!world || !Item::itemsList[6]) {
            return;
        }
        BlockTickGuard guard(world);
        RustBridge::blockLeavesDrop(&blockTickWorld(), 6, chance, x, y, z);
    }
    int quantityDropped() const override { return 0; }
    int idDropped(int metadata) const override { return 0; }

private:
    int field_663_c = 0;
};

class BlockSapling : public Block {
public:
    explicit BlockSapling(int id) : Block(id) {}
    bool allowsAttachment() const override { return false; }

    void onBlockAdded(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockSaplingAdded(&blockTickWorld(), blockID, x, y, z);
    }

    bool canBlockStay(World* world, int x, int y, int z) const override {
        BlockTickGuard guard(world);
        return RustBridge::blockSaplingCanStay(&blockTickWorld(), x, y, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        const uint8_t meta = world->getBlockMetadata(x, y, z);
        RustBridge::blockSaplingNeighbor(&blockTickWorld(), blockID, idDropped(meta), quantityDropped(), damageDropped(meta), x, y, z);
    }

    void updateTick(World* world, int x, int y, int z) override {
        uint8_t meta = world->getBlockMetadata(x, y, z);
        RustBridge::TickAction action;
        {
            BlockTickGuard guard(world);
            action = RustBridge::blockSaplingTick(&blockTickWorld(), blockID, idDropped(meta),
                                                  quantityDropped(), damageDropped(meta), x, y, z);
        }
        if (action.kind != 1) {
            return;
        }

        const int64_t treeSeed = static_cast<int64_t>(action.seed);
        world->setBlockWithNotify(x, y, z, 0);

        gBlockTickWorld = world;

        WorldAccessor accessor {
            .get_block_id = [](int32_t x, int32_t y, int32_t z) -> uint8_t {
                return gBlockTickWorld->getBlockId(x, y, z);
            },
            .set_block_id = [](int32_t x, int32_t y, int32_t z, uint8_t id) {
                gBlockTickWorld->setBlock(x, y, z, id);
            },
            .get_block_meta = [](int32_t x, int32_t y, int32_t z) -> uint8_t {
                return gBlockTickWorld->getBlockMetadata(x, y, z);
            },
            .set_block_meta = [](int32_t x, int32_t y, int32_t z, uint8_t meta) {
                gBlockTickWorld->setBlockMetadata(x, y, z, meta);
            },
            .allows_attachment = [](int32_t x, int32_t y, int32_t z) -> bool {
                int id = gBlockTickWorld->getBlockId(x, y, z);
                return id >= 0 && id < 256 && Block::allowsAttachmentArr[id];
            },
            .is_block_solid = [](int32_t x, int32_t y, int32_t z) -> bool {
                return gBlockTickWorld->isBlockSolid(x, y, z);
            },
            .get_height_value = [](int32_t x, int32_t z) -> int32_t {
                return gBlockTickWorld->getHeightValue(x, z);
            }
        };

        bool generated = false;
        {
            std::uniform_int_distribution<int> bigTreeRoll(0, 9);
            if (bigTreeRoll(world->rand) == 0) {
                generated = alpha_generate_big_tree(accessor, treeSeed, x, y, z);
            }
        }
        if (!generated) {
            generated = alpha_generate_tree(accessor, treeSeed, x, y, z);
        }

        gBlockTickWorld = nullptr;

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
        BlockTickGuard guard(world);
        RustBridge::blockCropsAdded(&blockTickWorld(), blockID, x, y, z);
    }

    bool canBlockStay(World* world, int x, int y, int z) const override {
        BlockTickGuard guard(world);
        return RustBridge::blockCropsCanStay(&blockTickWorld(), blockID, x, y, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        RustBridge::blockCropsNeighbor(&blockTickWorld(), blockID, blockID,
                                       Item::wheat ? Item::wheat->itemID : 0,
                                       Item::seeds ? Item::seeds->itemID : 0, x, y, z);
    }

    void updateTick(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockCropsTick(&blockTickWorld(), blockID, blockID,
                                   Item::wheat ? Item::wheat->itemID : 0,
                                   Item::seeds ? Item::seeds->itemID : 0, x, y, z);
    }

    int tickRate() const override { return 20; }

    void dropBlockAsItemWithChance(World* world, int x, int y, int z, int metadata, float chance) override {
        if (!world || !Item::seeds || !Item::wheat) {
            return;
        }
        BlockTickGuard guard(world);
        RustBridge::blockCropsDrop(&blockTickWorld(), Item::wheat->itemID, Item::seeds->itemID,
                                   x, y, z, metadata, chance);
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
        BlockTickGuard guard(world);
        RustBridge::blockSoilAdded(&blockTickWorld(), blockID, x, y, z);
    }

    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int x, int y, int z) override {
        return AxisAlignedBB::getBoundingBox(x, y, z, x + 1, y + 1, z + 1);
    }

    void updateTick(World* world, int x, int y, int z) override {
        if (!world) {
            return;
        }
        BlockTickGuard guard(world);
        RustBridge::blockSoilTick(&blockTickWorld(), blockID, x, y, z);
    }

    void onEntityWalking(World* world, int x, int y, int z, Entity* entity) override {
        if (!world || !entity) {
            return;
        }
        BlockTickGuard guard(world);
        RustBridge::blockSoilWalking(&blockTickWorld(), x, y, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!world) {
            return;
        }
        BlockTickGuard guard(world);
        RustBridge::blockSoilNeighbor(&blockTickWorld(), blockID, x, y, z);
    }

    int idDropped(int metadata) const override { return 3; }
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
    
    const int dropId = idDropped(metadata);
    if (dropId <= 0) {
        return;
    }
    BlockTickGuard guard(world);
    RustBridge::blockBaseDrop(&blockTickWorld(), dropId, quantityDropped(), damageDropped(metadata),
                              x, y, z, chance);
}
