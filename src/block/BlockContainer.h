#pragma once

#include "Block.h"
#include "../world/TileEntity.h"

// Base class for blocks that have associated TileEntity data
class BlockContainer : public Block {
public:
    BlockContainer(int id, Material* material) : Block(id, material) {
        isBlockContainer[id] = true;
    }

    void onBlockAdded(World* world, int x, int y, int z) override;
    void onBlockRemoval(World* world, int x, int y, int z) override;

protected:
    virtual std::unique_ptr<TileEntity> createTileEntity() = 0;
};
