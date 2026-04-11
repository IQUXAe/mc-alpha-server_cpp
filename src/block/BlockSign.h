#pragma once

#include "BlockContainer.h"
#include "../core/Material.h"

// Handles both sign post (ID 63, placed on floor) and wall sign (ID 68, placed on wall)
class BlockSign : public BlockContainer {
public:
    // isWallSign: true = wall sign (ID 68), false = sign post (ID 63)
    BlockSign(int id, bool isWallSign);

    // Signs have no collision box
    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World* world, int x, int y, int z) override;

    // Drop sign item (ID 323) regardless of metadata
    int idDropped(int metadata) const override;

    // Break sign if supporting block is removed
    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override;

protected:
    std::unique_ptr<TileEntity> createTileEntity() override;

private:
    bool isWallSign_;
};
