#pragma once

#include "Block.h"
#include "BlockTickWorld.h"
#include "../core/Material.h"
#include "../world/World.h"
#include "../core/MathHelper.h"
#include <cstdlib>

class BlockFire : public Block {
public:
    // Burn tables live in Rust (block_fire.rs); the ctor only chains Block.
    BlockFire(int id, int tex)
        : Block(id) {
        (void)tex;
    }

    bool allowsAttachment() const override { return false; }

    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override {
        return std::nullopt;
    }

    bool isCollidable() const override { return false; }

    int quantityDropped() const override { return 0; }

    int tickRate() const override { return 10; }

    void updateTick(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockFireTick(&fireWorld(), blockID, tickRate(), x, y, z);
    }

    bool canPlaceBlockAt(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        return RustBridge::blockFireCanPlace(&fireWorld(), x, y, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        BlockTickGuard guard(world);
        RustBridge::blockFireNeighbor(&fireWorld(), x, y, z);
    }

    void onBlockAdded(World* world, int x, int y, int z) override {
        BlockTickGuard guard(world);
        RustBridge::blockFireAdded(&fireWorld(), blockID, tickRate(), x, y, z);
    }
};
