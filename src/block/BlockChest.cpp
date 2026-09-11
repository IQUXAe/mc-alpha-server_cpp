#include "BlockChest.h"
#include "BlockTickWorld.h"
#include "../world/World.h"
#include "../world/TileEntityChest.h"
#include "../entity/EntityPlayerMP.h"
#include "../entity/EntityItem.h"
#include "../network/NetServerHandler.h"

bool BlockChest::blockActivated(World* world, int x, int y, int z, EntityPlayer* player) {
    TileEntity* te = world->getTileEntity(x, y, z);
    if (!te) return true;
    
    auto* chest = dynamic_cast<TileEntityChest*>(te);
    if (!chest) return true;

    // Check if chest can be opened (no solid block above)
    if (world->isBlockSolid(x, y + 1, z)) return true;
    
    // Check for adjacent chests and their accessibility
    if (world->getBlockId(x - 1, y, z) == blockID && world->isBlockSolid(x - 1, y + 1, z)) return true;
    if (world->getBlockId(x + 1, y, z) == blockID && world->isBlockSolid(x + 1, y + 1, z)) return true;
    if (world->getBlockId(x, y, z - 1) == blockID && world->isBlockSolid(x, y + 1, z - 1)) return true;
    if (world->getBlockId(x, y, z + 1) == blockID && world->isBlockSolid(x, y + 1, z + 1)) return true;

    // Send Packet59 to sync chest contents with client before GUI opens.
    // Client stores items in its local TileEntityChest for rendering the GUI.
    auto* playerMP = dynamic_cast<EntityPlayerMP*>(player);
    if (playerMP && playerMP->netHandler) {
        playerMP->netHandler->sendTileEntityPacket(chest);
    }


    
    return true;
}

void BlockChest::onBlockRemoval(World* world, int x, int y, int z) {
    TileEntity* te = world->getTileEntity(x, y, z);
    if (te) {
        auto* chest = dynamic_cast<TileEntityChest*>(te);
        if (chest) {
            // Slot contents scatter via Rust (block_container.rs).
            BlockTickGuard guard(world);
            for (int i = 0; i < chest->getSizeInventory(); ++i) {
                ItemStack* stack = chest->getStackInSlot(i);
                if (stack && stack->stackSize > 0) {
                    stack->stackSize = RustBridge::blockChestScatterStack(
                        &scatterWorld(), stack->itemID, stack->stackSize, stack->itemDamage, x, y, z);
                }
            }
        }
    }

    BlockContainer::onBlockRemoval(world, x, y, z);
}

bool BlockChest::canPlaceBlockAt(World* world, int x, int y, int z) {
    BlockTickGuard guard(world);
    return RustBridge::blockChestCanPlace(&scatterGetBlockId, blockID, x, y, z);
}

std::unique_ptr<TileEntity> BlockChest::createTileEntity() {
    return std::make_unique<TileEntityChest>();
}
