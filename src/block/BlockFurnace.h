#pragma once

#include "BlockContainer.h"
#include "../core/Material.h"

class EntityLiving;

class BlockFurnace : public BlockContainer {
public:
    BlockFurnace(int id, bool isActive);

    int idDropped(int metadata) const override;
    void onBlockAdded(World* world, int x, int y, int z) override;
    bool blockActivated(World* world, int x, int y, int z, EntityPlayer* player) override;
    static void updateFurnaceBlockState(bool active, World* world, int x, int y, int z);
    void onBlockPlacedBy(World* world, int x, int y, int z, EntityLiving* entity);

protected:
    std::unique_ptr<TileEntity> createTileEntity() override;

private:
    bool isActive_;
    void setDefaultDirection(World* world, int x, int y, int z);
};

// Helper function for TileEntityFurnace
void updateFurnaceState(bool active, World* world, int x, int y, int z);
