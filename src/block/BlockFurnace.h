#pragma once

#include "BlockContainer.h"
#include "../core/Material.h"

class BlockFurnace : public BlockContainer {
public:
    BlockFurnace(int id, bool isActive);

    int idDropped(int metadata) const override;
    void onBlockPlaced(World* world, int x, int y, int z, int side) override;
    void onBlockRemoval(World* world, int x, int y, int z) override;
    bool blockActivated(World* world, int x, int y, int z, EntityPlayer* player) override;
    static void updateFurnaceBlockState(bool active, World* world, int x, int y, int z);

protected:
    std::unique_ptr<TileEntity> createTileEntity() override;

private:
    bool isActive_;
};

// Helper function for TileEntityFurnace
void updateFurnaceState(bool active, World* world, int x, int y, int z);
