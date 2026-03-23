#include "BlockChest.h"
#include "../world/World.h"
#include "../world/TileEntityChest.h"
#include "../entity/EntityPlayerMP.h"
#include "../entity/EntityItem.h"
#include "../network/NetServerHandler.h"
#include <random>

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

    // Send Packet59 to the player who opened the chest to sync the contents
    auto* playerMP = dynamic_cast<EntityPlayerMP*>(player);
    if (playerMP && playerMP->netHandler) {
        playerMP->netHandler->sendTileEntityPacket(chest);
    }
    
    // TODO: Handle double chest (InventoryLargeChest) when GUI system is implemented
    
    return true;
}

void BlockChest::onBlockRemoval(World* world, int x, int y, int z) {
    TileEntity* te = world->getTileEntity(x, y, z);
    if (te) {
        auto* chest = dynamic_cast<TileEntityChest*>(te);
        if (chest) {
            // Drop all items from chest
            std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            
            for (int i = 0; i < chest->getSizeInventory(); ++i) {
                ItemStack* stack = chest->getStackInSlot(i);
                if (stack) {
                    float offsetX = dist(rng) * 0.8f + 0.1f;
                    float offsetY = dist(rng) * 0.8f + 0.1f;
                    float offsetZ = dist(rng) * 0.8f + 0.1f;
                    
                    while (stack->stackSize > 0) {
                        int dropCount = std::min(stack->stackSize, 
                                                static_cast<int>(rng() % 21 + 10));
                        stack->stackSize -= dropCount;
                        
                        auto entity = std::make_unique<EntityItem>(
                            stack->itemID, dropCount, stack->itemDamage);
                        entity->setPosition(
                            x + offsetX, 
                            y + offsetY, 
                            z + offsetZ);
                        
                        std::normal_distribution<double> motion(0.0, 0.05);
                        entity->motionX = motion(rng);
                        entity->motionY = motion(rng) + 0.2;
                        entity->motionZ = motion(rng);
                        
                        world->spawnEntityInWorld(std::move(entity));
                    }
                }
            }
        }
    }
    
    BlockContainer::onBlockRemoval(world, x, y, z);
}

bool BlockChest::canPlaceBlockAt(World* world, int x, int y, int z) {
    int adjacentChests = 0;
    
    if (world->getBlockId(x - 1, y, z) == blockID) ++adjacentChests;
    if (world->getBlockId(x + 1, y, z) == blockID) ++adjacentChests;
    if (world->getBlockId(x, y, z - 1) == blockID) ++adjacentChests;
    if (world->getBlockId(x, y, z + 1) == blockID) ++adjacentChests;
    
    if (adjacentChests > 1) return false;
    
    // Check if any adjacent chest already has a neighbor
    if (hasNeighborChest(world, x - 1, y, z)) return false;
    if (hasNeighborChest(world, x + 1, y, z)) return false;
    if (hasNeighborChest(world, x, y, z - 1)) return false;
    if (hasNeighborChest(world, x, y, z + 1)) return false;
    
    return true;
}

bool BlockChest::hasNeighborChest(World* world, int x, int y, int z) {
    if (world->getBlockId(x, y, z) != blockID) return false;
    
    return world->getBlockId(x - 1, y, z) == blockID ||
           world->getBlockId(x + 1, y, z) == blockID ||
           world->getBlockId(x, y, z - 1) == blockID ||
           world->getBlockId(x, y, z + 1) == blockID;
}

std::unique_ptr<TileEntity> BlockChest::createTileEntity() {
    return std::make_unique<TileEntityChest>();
}
