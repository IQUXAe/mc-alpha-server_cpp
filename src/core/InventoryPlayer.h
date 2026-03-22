#pragma once

#include "IInventory.h"
#include "ItemStack.h"
#include "NBT.h"
#include <vector>

class EntityPlayer;
class Block;

class InventoryPlayer : public IInventory {
public:
    std::vector<ItemStack*> mainInventory; // size 36
    std::vector<ItemStack*> armorInventory; // size 4
    std::vector<ItemStack*> craftingInventory; // size 4
    int currentItem = 0;
    EntityPlayer* player = nullptr;
    bool inventoryChanged = false;

    InventoryPlayer(EntityPlayer* p) : player(p) {
        mainInventory.resize(36, nullptr);
        armorInventory.resize(4, nullptr);
        craftingInventory.resize(4, nullptr);
    }

    ~InventoryPlayer() {
        for (auto* stack : mainInventory) delete stack;
        for (auto* stack : armorInventory) delete stack;
        for (auto* stack : craftingInventory) delete stack;
    }

    ItemStack* getCurrentItem() {
        if (currentItem >= 0 && currentItem < (int)mainInventory.size()) {
            return mainInventory[currentItem];
        }
        return nullptr;
    }

    int getInventorySlotContainItem(int id) {
        for (size_t i = 0; i < mainInventory.size(); ++i) {
            if (mainInventory[i] && mainInventory[i]->itemID == id) return i;
        }
        return -1;
    }

    int getFirstPartialMatchingStack(int id) {
        for (size_t i = 0; i < mainInventory.size(); ++i) {
            if (mainInventory[i] && mainInventory[i]->itemID == id && 
                mainInventory[i]->stackSize < mainInventory[i]->getMaxStackSize() && 
                mainInventory[i]->stackSize < getInventoryStackLimit()) {
                return i;
            }
        }
        return -1;
    }

    int getFirstEmptyStack() {
        for (size_t i = 0; i < mainInventory.size(); ++i) {
            if (!mainInventory[i]) return i;
        }
        return -1;
    }

    int addItemsToInventory(int id, int count) {
        int slot = getFirstPartialMatchingStack(id);
        if (slot < 0) slot = getFirstEmptyStack();
        if (slot < 0) return count;

        if (!mainInventory[slot]) {
            mainInventory[slot] = new ItemStack(id, 0);
        }

        int limit = std::min(mainInventory[slot]->getMaxStackSize(), getInventoryStackLimit());
        int freeSpace = limit - mainInventory[slot]->stackSize;
        int amountToAdd = std::min(count, freeSpace);

        if (amountToAdd > 0) {
            count -= amountToAdd;
            mainInventory[slot]->stackSize += amountToAdd;
            mainInventory[slot]->animationsToGo = 5;
        }
        return count;
    }

    void decrementAnimations() {
        for (auto* stack : mainInventory) {
            if (stack && stack->animationsToGo > 0) stack->animationsToGo--;
        }
    }

    bool consumeInventoryItem(int id) {
        int slot = getInventorySlotContainItem(id);
        if (slot < 0) return false;
        if (--mainInventory[slot]->stackSize <= 0) {
            delete mainInventory[slot];
            mainInventory[slot] = nullptr;
        }
        return true;
    }

    bool addItemStackToInventory(ItemStack* stack) {
        if (!stack || stack->stackSize <= 0) return false;
        if (stack->itemDamage == 0) {
            stack->stackSize = addItemsToInventory(stack->itemID, stack->stackSize);
            if (stack->stackSize <= 0) return true;
        }
        
        int slot = getFirstEmptyStack();
        if (slot >= 0) {
            mainInventory[slot] = new ItemStack(stack->itemID, stack->stackSize, stack->itemDamage);
            mainInventory[slot]->animationsToGo = 5;
            stack->stackSize = 0;
            return true;
        }
        return false;
    }

    // IInventory methods
    int getSizeInventory() override {
        return 36 + 4;
    }

    ItemStack* getStackInSlot(int slot) override {
        if (slot < 36) return mainInventory[slot];
        slot -= 36;
        if (slot < 4) return armorInventory[slot];
        slot -= 4;
        if (slot < 4) return craftingInventory[slot];
        return nullptr;
    }

    ItemStack* decrStackSize(int slot, int amount) override {
        ItemStack* stack = getStackInSlot(slot);
        if (!stack) return nullptr;
        
        if (stack->stackSize <= amount) {
            setInventorySlotContents(slot, nullptr);
            return stack;
        }
        
        ItemStack* ret = new ItemStack(stack->copy());
        ret->stackSize = amount;
        stack->stackSize -= amount;
        return ret;
    }

    void setInventorySlotContents(int slot, ItemStack* stack) override {
        if (slot < 36) {
            if (mainInventory[slot]) delete mainInventory[slot];
            mainInventory[slot] = stack;
        } else if (slot >= 36 && slot < 40) {
            if (armorInventory[slot - 36]) delete armorInventory[slot - 36];
            armorInventory[slot - 36] = stack;
        } else if (slot >= 40 && slot < 44) {
            if (craftingInventory[slot - 40]) delete craftingInventory[slot - 40];
            craftingInventory[slot - 40] = stack;
        }
    }
    
    std::string getInvName() override { return "Inventory"; }
    
    int getInventoryStackLimit() override { return 64; }
    
    void onInventoryChanged() override { inventoryChanged = true; }
    
    bool canInteractWith(EntityPlayer* pl) override { return true; }

    void dropAllItems() {
        for (size_t i = 0; i < mainInventory.size(); i++) {
            if (mainInventory[i]) {
                // TODO: Drop logic
                delete mainInventory[i];
                mainInventory[i] = nullptr;
            }
        }
        for (size_t i = 0; i < armorInventory.size(); i++) {
            if (armorInventory[i]) {
                delete armorInventory[i];
                armorInventory[i] = nullptr;
            }
        }
    }

    int getTotalArmorValue() {
        // Assume implemented
        return 0;
    }
    
    void damageArmor(int damage) {
        for (size_t i = 0; i < armorInventory.size(); i++) {
            if (armorInventory[i]) {
                armorInventory[i]->damageItem(damage);
                if (armorInventory[i]->stackSize == 0) {
                    delete armorInventory[i];
                    armorInventory[i] = nullptr;
                }
            }
        }
    }
    
    float getStrVsBlock(Block* block) {
        float f = 1.0f;
        if (mainInventory[currentItem] != nullptr) {
            f *= mainInventory[currentItem]->getStrVsBlock(block);
        }
        return f;
    }

    bool canHarvestBlock(Block* block) {
        if (block->canHarvestBlock(player)) {
            return true;
        }
        if (mainInventory[currentItem] != nullptr) {
            return mainInventory[currentItem]->canHarvestBlock(block);
        }
        return false;
    }

    // NBT serialization (mirrors Java's InventoryPlayer.writeToNBT/readFromNBT)
    void writeToNBT(std::shared_ptr<NBTCompound> nbtList) {
        // Main inventory: slots 0-35
        for (size_t i = 0; i < mainInventory.size(); ++i) {
            if (mainInventory[i] != nullptr) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i));
                mainInventory[i]->writeToNBT(tag);
                nbtList->setCompound("item_" + std::to_string(i), tag);
            }
        }
        // Armor inventory: slots 100-103
        for (size_t i = 0; i < armorInventory.size(); ++i) {
            if (armorInventory[i] != nullptr) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i + 100));
                armorInventory[i]->writeToNBT(tag);
                nbtList->setCompound("item_" + std::to_string(i + 100), tag);
            }
        }
        // Crafting inventory: slots 80-83
        for (size_t i = 0; i < craftingInventory.size(); ++i) {
            if (craftingInventory[i] != nullptr) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i + 80));
                craftingInventory[i]->writeToNBT(tag);
                nbtList->setCompound("item_" + std::to_string(i + 80), tag);
            }
        }
    }

    void readFromNBT(std::shared_ptr<NBTCompound> nbtList) {
        // Clear existing inventory
        for (auto* stack : mainInventory) delete stack;
        for (auto* stack : armorInventory) delete stack;
        for (auto* stack : craftingInventory) delete stack;
        mainInventory.assign(36, nullptr);
        armorInventory.assign(4, nullptr);
        craftingInventory.assign(4, nullptr);

        // Read all items from NBT
        for (const auto& [key, tag] : nbtList->tags) {
            if (key.find("item_") == 0) {
                auto itemTag = std::dynamic_pointer_cast<NBTCompound>(tag);
                if (!itemTag) continue;
                
                int slot = itemTag->getByte("Slot") & 0xFF;
                auto* stack = new ItemStack(itemTag);
                
                if (slot >= 0 && slot < 36) {
                    mainInventory[slot] = stack;
                } else if (slot >= 80 && slot < 84) {
                    craftingInventory[slot - 80] = stack;
                } else if (slot >= 100 && slot < 104) {
                    armorInventory[slot - 100] = stack;
                } else {
                    delete stack;
                }
            }
        }
    }
};
