#include "BlockFurnace.h"
#include "../world/World.h"
#include "../world/TileEntityFurnace.h"
#include "../entity/EntityPlayerMP.h"
#include "../core/MathHelper.h"

BlockFurnace::BlockFurnace(int id, bool isActive) 
    : BlockContainer(id, &Material::rock), isActive_(isActive) {}

int BlockFurnace::idDropped(int metadata) const {
    return 61; // Always drop idle furnace
}

void BlockFurnace::onBlockAdded(World* world, int x, int y, int z) {
    BlockContainer::onBlockAdded(world, x, y, z);
    setDefaultDirection(world, x, y, z);
}

bool BlockFurnace::blockActivated(World* world, int x, int y, int z, EntityPlayer* player) {
    TileEntity* te = world->getTileEntity(x, y, z);
    if (!te) return true;
    
    auto* furnace = dynamic_cast<TileEntityFurnace*>(te);
    if (!furnace) return true;

    // Player opens furnace - would trigger GUI in full implementation
    return true;
}

void BlockFurnace::updateFurnaceBlockState(bool active, World* world, int x, int y, int z) {
    int metadata = world->getBlockMetadata(x, y, z);
    TileEntity* te = world->getTileEntity(x, y, z);
    
    int newBlockId = active ? 62 : 61; // 62 = burning, 61 = idle
    world->setBlockWithNotify(x, y, z, newBlockId);
    world->setBlockMetadata(x, y, z, metadata);
    
    if (te) {
        te->xCoord = x;
        te->yCoord = y;
        te->zCoord = z;
        world->setTileEntity(x, y, z, std::unique_ptr<TileEntity>(te));
    }
}

void BlockFurnace::onBlockPlacedBy(World* world, int x, int y, int z, EntityLiving* entity) {
    if (!entity) return;
    
    int facing = MathHelper::floor_double(entity->rotationYaw * 4.0f / 360.0f + 0.5) & 3;
    
    uint8_t metadata = 3; // Default: facing south
    if (facing == 0) metadata = 2; // North
    if (facing == 1) metadata = 5; // East
    if (facing == 2) metadata = 3; // South
    if (facing == 3) metadata = 4; // West
    
    world->setBlockMetadata(x, y, z, metadata);
}

std::unique_ptr<TileEntity> BlockFurnace::createTileEntity() {
    return std::make_unique<TileEntityFurnace>();
}

void BlockFurnace::setDefaultDirection(World* world, int x, int y, int z) {
    int north = world->getBlockId(x, y, z - 1);
    int south = world->getBlockId(x, y, z + 1);
    int west = world->getBlockId(x - 1, y, z);
    int east = world->getBlockId(x + 1, y, z);
    
    uint8_t metadata = 3; // Default: south
    
    if (Block::blocksList[north] && Block::blocksList[north]->isCollidable() && 
        (!Block::blocksList[south] || !Block::blocksList[south]->isCollidable())) {
        metadata = 3;
    }
    
    if (Block::blocksList[south] && Block::blocksList[south]->isCollidable() && 
        (!Block::blocksList[north] || !Block::blocksList[north]->isCollidable())) {
        metadata = 2;
    }
    
    if (Block::blocksList[west] && Block::blocksList[west]->isCollidable() && 
        (!Block::blocksList[east] || !Block::blocksList[east]->isCollidable())) {
        metadata = 5;
    }
    
    if (Block::blocksList[east] && Block::blocksList[east]->isCollidable() && 
        (!Block::blocksList[west] || !Block::blocksList[west]->isCollidable())) {
        metadata = 4;
    }
    
    world->setBlockMetadata(x, y, z, metadata);
}

// Helper function implementation
void updateFurnaceState(bool active, World* world, int x, int y, int z) {
    BlockFurnace::updateFurnaceBlockState(active, world, x, y, z);
}
