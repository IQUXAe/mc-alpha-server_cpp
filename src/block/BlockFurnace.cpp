#include "BlockFurnace.h"
#include "../world/World.h"
#include "../world/TileEntityFurnace.h"
#include "../entity/EntityItem.h"
#include "../entity/EntityPlayerMP.h"
#include "../network/NetServerHandler.h"
#include <random>

BlockFurnace::BlockFurnace(int id, bool isActive) 
    : BlockContainer(id, &Material::rock), isActive_(isActive) {}

int BlockFurnace::idDropped(int metadata) const {
    return 61; // Always drop idle furnace
}

void BlockFurnace::onBlockPlaced(World* world, int x, int y, int z, int side) {
    // Facing is already set from player yaw in ItemBlock::onItemUse — nothing to do here.
}

void BlockFurnace::onBlockRemoval(World* world, int x, int y, int z) {
    TileEntity* te = world->getTileEntity(x, y, z);
    if (te) {
        auto* furnace = dynamic_cast<TileEntityFurnace*>(te);
        if (furnace) {
            // Drop all inventory slots (input, fuel, output)
            for (int i = 0; i < furnace->getSizeInventory(); ++i) {
                ItemStack* stack = furnace->getStackInSlot(i);
                if (!stack || stack->stackSize <= 0) continue;
                auto entity = std::make_unique<EntityItem>(stack->itemID, stack->stackSize, stack->itemDamage);
                entity->setPosition(x + 0.5, y + 0.7, z + 0.5);
                std::uniform_real_distribution<double> dist(-0.1, 0.1);
                entity->motionX = dist(world->rand);
                entity->motionY = 0.2;
                entity->motionZ = dist(world->rand);
                world->spawnEntityInWorld(std::move(entity));
            }
        }
    }
    
    BlockContainer::onBlockRemoval(world, x, y, z);
}

bool BlockFurnace::blockActivated(World* world, int x, int y, int z, EntityPlayer* player) {
    TileEntity* te = world->getTileEntity(x, y, z);
    if (!te) return true;
    
    auto* furnace = dynamic_cast<TileEntityFurnace*>(te);
    if (!furnace) return true;

    // Send furnace contents to client so GUI is populated correctly
    auto* playerMP = dynamic_cast<EntityPlayerMP*>(player);
    if (playerMP && playerMP->netHandler) {
        playerMP->netHandler->sendTileEntityPacket(furnace);
    }

    return true;
}

void BlockFurnace::updateFurnaceBlockState(bool active, World* world, int x, int y, int z) {
    int metadata = world->getBlockMetadata(x, y, z);
    int newBlockId = active ? 62 : 61;

    // Use setBlock (no notify) to avoid triggering onBlockRemoval/onBlockAdded
    // which would destroy and recreate the TileEntity while it's still running.
    // Then manually notify the client with a block change packet.
    world->setBlockAndMetadata(x, y, z, newBlockId, metadata);
    world->markBlockNeedsUpdate(x, y, z);
}

std::unique_ptr<TileEntity> BlockFurnace::createTileEntity() {
    return std::make_unique<TileEntityFurnace>();
}

// Helper function implementation
void updateFurnaceState(bool active, World* world, int x, int y, int z) {
    BlockFurnace::updateFurnaceBlockState(active, world, x, y, z);
}
