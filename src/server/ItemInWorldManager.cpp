#include "ItemInWorldManager.h"
#include "../world/World.h"
#include "../entity/EntityPlayerMP.h"
#include "../block/Block.h"
#include "../core/ItemStack.h"
#include "../core/Item.h"
#include "../core/AxisAlignedBB.h"
#include "../core/Logger.h"
#include "../core/RustBridge.h"

namespace {

// Build the digging input for Rust from the live player state.
// Mirrors the held-item/water/ground extraction in Block::checkHardness.
RustBridge::FfiDigInput makeDigInput(int blockId, EntityPlayerMP* player) {
    int heldId = 0;
    bool inWater = false;
    bool onGround = true;
    if (player) {
        if (ItemStack* held = player->inventory.getCurrentItem()) {
            if (held->stackSize > 0 && held->itemID > 0) {
                heldId = held->itemID;
            }
        }
        inWater = player->isInWater;
        onGround = player->onGround;
    }
    return RustBridge::FfiDigInput{blockId, heldId, inWater, onGround};
}

} // namespace

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

    // Instant-break decision lives in Rust (hardness >= 1.0).
    // One FFI call replaces the old Block::blocksList + checkHardness round-trip.
    if (RustBridge::digOnClick(makeDigInput(id, thisPlayerMP))) {
        harvestBlock(x, y, z);
    }
    // Otherwise, blockRemoving will handle progressive breaking.
}

void ItemInWorldManager::blockRemoving(int x, int y, int z, int side) {
    // Exact C++ semantics (cooldown tick, target latch, air no-op,
    // accumulate, break at 1.0) live in Rust now. Air packets also go
    // through FFI so the cooldown keeps ticking, as before.
    int id = worldObj->getBlockId(x, y, z);
    if (RustBridge::digOnTick(&digState, x, y, z, makeDigInput(id, thisPlayerMP))) {
        harvestBlock(x, y, z);
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
        Item* item = Item::itemsList[stack->itemID];
        if (item && dynamic_cast<ItemTool*>(item)) {
            stack->damageItem(1);
            if (stack->stackSize <= 0 || stack->itemDamage > item->maxDamage) {
                stack = nullptr;
                thisPlayerMP->destroyCurrentEquippedItem();
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
