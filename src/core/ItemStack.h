#pragma once

#include "Item.h"
#include "../block/Block.h"
#include "NBT.h"

class EntityLiving;
class EntityPlayerMP;
class ItemStack {
public:
    int stackSize = 0;
    int animationsToGo = 0;
    int itemID = 0;
    int itemDamage = 0;

    ItemStack() {}
    
    ItemStack(Block* block) : ItemStack(block, 1) {}
    ItemStack(Block* block, int size) : ItemStack(block ? block->blockID : 0, size) {}
    
    ItemStack(Item* item) : ItemStack(item, 1) {}
    ItemStack(Item* item, int size) : ItemStack(item ? item->itemID : 0, size) {}
    
    ItemStack(int id) : ItemStack(id, 1) {}
    ItemStack(int id, int size) : ItemStack(id, size, 0) {}
    ItemStack(int id, int size, int damage) {
        this->itemID = id;
        this->stackSize = size;
        this->itemDamage = damage;
    }
    
    ItemStack(std::shared_ptr<NBTCompound> nbt) {
        readFromNBT(nbt);
    }

    Item* getItem() {
        return Item::itemsList[itemID];
    }

    const Item* getItem() const {
        return Item::itemsList[itemID];
    }
    
    bool useItem(EntityPlayerMP* player, World* world, int x, int y, int z, int side) {
        Item* item = getItem();
        return item ? item->onItemUse(this, player, world, x, y, z, side) : false;
    }
    
    float getStrVsBlock(Block* block) {
        if (auto* tool = dynamic_cast<ItemTool*>(getItem()))
            return tool->getStrVsBlock(block->blockID);
        return 1.0f;
    }
    
    ItemStack useItemRightClick(World* world, EntityPlayerMP* player) {
        Item* item = getItem();
        return item ? item->onItemRightClick(this, world, player) : *this;
    }

    std::shared_ptr<NBTCompound> writeToNBT(std::shared_ptr<NBTCompound> nbt) {
        nbt->setShort("id", static_cast<int16_t>(itemID));
        nbt->setByte("Count", static_cast<int8_t>(stackSize));
        nbt->setShort("Damage", static_cast<int16_t>(itemDamage));
        return nbt;
    }

    void readFromNBT(std::shared_ptr<NBTCompound> nbt) {
        itemID = nbt->getShort("id");
        stackSize = nbt->getByte("Count");
        itemDamage = nbt->getShort("Damage");
    }

    int getMaxStackSize() {
        if (getItem()) return getItem()->maxStackSize;
        return 64;
    }

    int getMaxDamage() {
        if (getItem()) return getItem()->maxDamage;
        return 0;
    }
    
    void damageItem(int damage) {
        if (getMaxDamage() <= 0) return; // item has no durability
        itemDamage += damage;
        if (itemDamage > getMaxDamage()) {
            stackSize--;
            if (stackSize < 0) stackSize = 0;
            itemDamage = 0;
        }
    }
    
    void hitEntity(EntityLiving* entity) {
        if (Item* item = getItem()) {
            item->hitEntity(this, entity);
        }
    }
    
    void hitBlock(int x, int y, int z, int side) {
        // getItem()->hitBlock(this, x, y, z, side);
    }
    
    int getDamageVsEntity(Entity* entity) {
        if (Item* item = getItem()) {
            return item->getDamageVsEntity(entity);
        }
        return 1;
    }
    
    bool canHarvestBlock(Block* block) {
        if (auto* tool = dynamic_cast<ItemTool*>(getItem()))
            return tool->canHarvestBlock(block->blockID);
        return false;
    }
    
    void interactWith(EntityPlayerMP* player) {
    }
    
    ItemStack copy() {
        return ItemStack(itemID, stackSize, itemDamage);
    }
};
