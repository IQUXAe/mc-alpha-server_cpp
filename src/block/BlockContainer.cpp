#include "BlockContainer.h"
#include "../world/World.h"
#include "../core/Logger.h"
#include <execinfo.h>

void BlockContainer::onBlockAdded(World* world, int x, int y, int z) {
    Block::onBlockAdded(world, x, y, z);
    // Don't overwrite an existing TileEntity — it may have been loaded from disk with saved data
    if (world->getTileEntity(x, y, z)) return;
    auto tileEntity = createTileEntity();
    if (tileEntity) {
        Logger::info("Creating TileEntity '{}' at ({}, {}, {})", tileEntity->getEntityId(), x, y, z);
        // Log call stack to diagnose unexpected creation
        void* frames[16];
        int n = backtrace(frames, 16);
        char** syms = backtrace_symbols(frames, n);
        for (int i = 1; i < std::min(n, 6); ++i)
            Logger::info("  [bt] {}", syms ? syms[i] : "?");
        free(syms);
        world->setTileEntity(x, y, z, std::move(tileEntity));
    } else {
        Logger::severe("Failed to create TileEntity at ({}, {}, {})", x, y, z);
    }
}

void BlockContainer::onBlockRemoval(World* world, int x, int y, int z) {
    Block::onBlockRemoval(world, x, y, z);
    world->removeTileEntity(x, y, z);
}
