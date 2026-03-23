#include "BlockContainer.h"
#include "../world/World.h"
#include "../core/Logger.h"

void BlockContainer::onBlockAdded(World* world, int x, int y, int z) {
    Block::onBlockAdded(world, x, y, z);
    auto tileEntity = createTileEntity();
    if (tileEntity) {
        Logger::info("Creating TileEntity '{}' at ({}, {}, {})", tileEntity->getEntityId(), x, y, z);
        world->setTileEntity(x, y, z, std::move(tileEntity));
    } else {
        Logger::severe("Failed to create TileEntity at ({}, {}, {})", x, y, z);
    }
}

void BlockContainer::onBlockRemoval(World* world, int x, int y, int z) {
    Block::onBlockRemoval(world, x, y, z);
    world->removeTileEntity(x, y, z);
}
