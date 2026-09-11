#pragma once

#include "../core/RustBridge.h"

class World;
class EntityPlayerMP;
class ItemStack;

class ItemInWorldManager {
public:
    World* worldObj;
    EntityPlayerMP* thisPlayerMP = nullptr;

    // Progressive-digging state is owned by Rust (player_digging.rs).
    // Stored inline: plain repr(C) value, no pointer, no extra free.
    RustBridge::FfiDigState digState = RustBridge::digStateNew();

    void cancelRemoving() {
        RustBridge::digCancel(&digState);
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
