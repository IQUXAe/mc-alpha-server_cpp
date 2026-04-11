#pragma once

#include "BlockContainer.h"
#include "../core/Material.h"

class BlockChest : public BlockContainer {
public:
    BlockChest(int id) : BlockContainer(id, &Material::wood) {}

    bool blockActivated(World* world, int x, int y, int z, EntityPlayer* player) override;
    void onBlockRemoval(World* world, int x, int y, int z) override;
    bool canPlaceBlockAt(World* world, int x, int y, int z);

protected:
    std::unique_ptr<TileEntity> createTileEntity() override;

private:
    bool hasNeighborChest(World* world, int x, int y, int z);
};
