#pragma once

class World;
class EntityPlayerMP;
class ItemStack;

class ItemInWorldManager {
public:
    World* worldObj;
    EntityPlayerMP* thisPlayerMP;
    
    // Digging tracking
    float curblockDamage = 0.0f;
    int initialDamage = 0;
    float blockDamage = 0.0f;
    
    // Target position
    int partiallyDestroyedBlockX = 0;
    int partiallyDestroyedBlockY = 0;
    int partiallyDestroyedBlockZ = 0;

    void cancelRemoving() {
        curblockDamage = 0.0f;
        initialDamage = 0;
    }

    ItemInWorldManager(World* world);

    void setPlayer(EntityPlayerMP* player) {
        thisPlayerMP = player;
    }

    void onBlockClicked(int x, int y, int z, int side);
    void tick();
    void blockRemoving(int x, int y, int z, int side);
    bool removeBlock(int x, int y, int z);
    bool harvestBlock(int x, int y, int z);
    
    bool useItem(EntityPlayerMP* player, World* world, ItemStack* itemstack);
    bool activeBlockOrUseItem(EntityPlayerMP* player, World* world, ItemStack* itemstack, int x, int y, int z, int side);
};
