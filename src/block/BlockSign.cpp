#include "BlockSign.h"
#include "../world/World.h"
#include "../world/TileEntitySign.h"
#include "../core/Material.h"
#include "../core/AxisAlignedBB.h"

BlockSign::BlockSign(int id, bool isWallSign)
    : BlockContainer(id, &Material::wood), isWallSign_(isWallSign) {
    setHardness(1.0f);
}

std::optional<AxisAlignedBB> BlockSign::getCollisionBoundingBoxFromPool(World*, int, int, int) {
    // Signs have no collision box (players walk through them)
    return std::nullopt;
}

int BlockSign::idDropped(int /*metadata*/) const {
    return 323; // Sign item ID (Java: Item.sign.shiftedIndex = 256 + 67 = 323)
}

void BlockSign::onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) {
    bool shouldBreak = false;

    if (isWallSign_) {
        // Wall sign: needs solid block behind it based on metadata facing
        int meta = world->getBlockMetadata(x, y, z);
        // meta 2=north(+z), 3=south(-z), 4=west(+x), 5=east(-x)
        // Check if the block it's attached to is still solid
        shouldBreak = true;
        if (meta == 2 && world->getBlockMaterial(x, y, z + 1)->isSolid()) shouldBreak = false;
        if (meta == 3 && world->getBlockMaterial(x, y, z - 1)->isSolid()) shouldBreak = false;
        if (meta == 4 && world->getBlockMaterial(x + 1, y, z)->isSolid()) shouldBreak = false;
        if (meta == 5 && world->getBlockMaterial(x - 1, y, z)->isSolid()) shouldBreak = false;
    } else {
        // Sign post: needs solid block below
        if (!world->getBlockMaterial(x, y - 1, z)->isSolid())
            shouldBreak = true;
    }

    if (shouldBreak) {
        dropBlockAsItem(world, x, y, z, world->getBlockMetadata(x, y, z));
        world->setBlockWithNotify(x, y, z, 0);
    }
}

std::unique_ptr<TileEntity> BlockSign::createTileEntity() {
    return std::make_unique<TileEntitySign>();
}
