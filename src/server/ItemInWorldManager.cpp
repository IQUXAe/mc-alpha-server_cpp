#include "ItemInWorldManager.h"
#include "../world/World.h"
#include "../entity/EntityPlayerMP.h"
#include "../block/Block.h"
#include "../core/ItemStack.h"
#include "../core/Item.h"
#include "../core/AxisAlignedBB.h"
#include "../core/Logger.h"

ItemInWorldManager::ItemInWorldManager(World* world) {
    this->worldObj = world;
}

void ItemInWorldManager::tick() {
    // We can put some logic here for progressive digging if needed.
    // In multiplayer server (NetServerHandler) we usually just trust client mostly in Alpha,
    // but tracking tick is useful.
}

void ItemInWorldManager::onBlockClicked(int x, int y, int z, int side) {
    int id = worldObj->getBlockId(x, y, z);
    if (id == 0) return;
    
    Block* block = Block::blocksList[id];
    // In Alpha, onBlockClicked is called when player starts digging (status=0)
    // If block breaks instantly (hardness check >= 1.0), harvest it immediately
    float hardness = block->checkHardness(thisPlayerMP);
    if (hardness >= 1.0f) {
        // Instant break (e.g., flowers, torches)
        harvestBlock(x, y, z);
    }
    // Otherwise, blockRemoving will handle progressive breaking
}

void ItemInWorldManager::blockRemoving(int x, int y, int z, int side) {
    if (initialDamage > 0) {
        initialDamage--;
    } else {
        if (x == partiallyDestroyedBlockX && y == partiallyDestroyedBlockY && z == partiallyDestroyedBlockZ) {
            int id = worldObj->getBlockId(x, y, z);
            if (id == 0) return;
            Block* block = Block::blocksList[id];
            float hardnessTick = block->checkHardness(thisPlayerMP);
            curblockDamage += hardnessTick;
            blockDamage += 1.0f;
            if (curblockDamage >= 1.0f) {
                harvestBlock(x, y, z);
                curblockDamage = 0.0f;
                blockDamage = 0.0f;
                initialDamage = 5;
            }
        } else {
            curblockDamage = 0.0f;
            blockDamage = 0.0f;
            partiallyDestroyedBlockX = x;
            partiallyDestroyedBlockY = y;
            partiallyDestroyedBlockZ = z;
        }
    }
}

bool ItemInWorldManager::removeBlock(int x, int y, int z) {
    Block* block = Block::blocksList[worldObj->getBlockId(x, y, z)];
    int metadata = worldObj->getBlockMetadata(x, y, z);
    bool flag = worldObj->setBlockWithNotify(x, y, z, 0);
    if (block && flag) {
        block->onBlockDestroyedByPlayer(worldObj, x, y, z, metadata);
    }
    return flag;
}

bool ItemInWorldManager::harvestBlock(int x, int y, int z) {
    int id = worldObj->getBlockId(x, y, z);
    int meta = worldObj->getBlockMetadata(x, y, z);
    bool removed = removeBlock(x, y, z);

    ItemStack* stack = thisPlayerMP->getCurrentEquippedItem();
    if (stack && stack->itemID > 0 && stack->itemID < 32000) {
        // Java: ItemTool.hitBlock damages tool by 1
        Item* item = Item::itemsList[stack->itemID];
        if (item && dynamic_cast<ItemTool*>(item)) {
            stack->damageItem(1);
            if (stack->stackSize <= 0 || stack->itemDamage > item->maxDamage) {
                thisPlayerMP->destroyCurrentEquippedItem();
                stack = nullptr;
            }
        }
    }

    // Java func_325_c: drop only if player.func_167_b(block) == true
    // func_167_b = inventory.canHarvestBlock(block)
    if (removed && id > 0 && Block::blocksList[id]) {
        if (thisPlayerMP->canHarvestBlock(Block::blocksList[id]))
            Block::blocksList[id]->dropBlockAsItemWithChance(worldObj, x, y, z, meta, 1.0f);
    }
    return removed;
}

bool ItemInWorldManager::useItem(EntityPlayerMP* player, World* world, ItemStack* itemstack) {
    if (!itemstack || itemstack->stackSize <= 0) return false;
    const ItemStack original = itemstack->copy();
    const ItemStack result = itemstack->useItemRightClick(world, player);

    const bool changed = result.itemID != original.itemID
        || result.stackSize != original.stackSize
        || result.itemDamage != original.itemDamage;
    if (!changed) {
        return false;
    }

    if (result.itemID <= 0 || result.stackSize <= 0) {
        player->inventory.mainInventory[player->inventory.currentItem].reset();
    } else {
        player->inventory.mainInventory[player->inventory.currentItem] = std::make_unique<ItemStack>(result);
    }

    if (player->netHandler) {
        player->netHandler->sendInventory();
    }
    return true;
}

bool ItemInWorldManager::activeBlockOrUseItem(EntityPlayerMP* player, World* world, ItemStack* itemstack, int x, int y, int z, int side) {
    int id = world->getBlockId(x, y, z);
    if (id > 0 && Block::blocksList[id] && Block::blocksList[id]->blockActivated(world, x, y, z, player))
        return true;
    if (!itemstack || itemstack->stackSize <= 0) return false;

    bool used = itemstack->useItem(player, world, x, y, z, side);
    if (used) {
        if (player->netHandler) player->netHandler->sendInventory();
    }
    return used;
}
