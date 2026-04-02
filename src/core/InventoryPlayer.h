#pragma once

#include "IInventory.h"
#include "ItemStack.h"
#include "Item.h"
#include "NBT.h"
#include "Material.h"
#include <vector>
#include <memory>

class EntityPlayer;
class Block;

class InventoryPlayer : public IInventory {
public:
    std::vector<std::unique_ptr<ItemStack>> mainInventory; // size 36
    std::vector<std::unique_ptr<ItemStack>> armorInventory; // size 4
    std::vector<std::unique_ptr<ItemStack>> craftingInventory; // size 4
    int currentItem = 0;
    EntityPlayer* player = nullptr;
    bool inventoryChanged = false;

    InventoryPlayer(EntityPlayer* p) : player(p) {
        mainInventory.resize(36);
        armorInventory.resize(4);
        craftingInventory.resize(4);
    }

    ~InventoryPlayer() = default;

    ItemStack* getCurrentItem() {
        if (currentItem >= 0 && currentItem < (int)mainInventory.size()) {
            return mainInventory[currentItem].get();
        }
        return nullptr;
    }

    const ItemStack* getCurrentItem() const {
        if (currentItem >= 0 && currentItem < (int)mainInventory.size()) {
            return mainInventory[currentItem].get();
        }
        return nullptr;
    }

    int getInventorySlotContainItem(int id) {
        for (size_t i = 0; i < mainInventory.size(); ++i) {
            if (mainInventory[i] && mainInventory[i]->itemID == id) return i;
        }
        return -1;
    }

    int getFirstPartialMatchingStack(int id, int damage) {
        for (size_t i = 0; i < mainInventory.size(); ++i) {
            if (mainInventory[i] && mainInventory[i]->itemID == id && mainInventory[i]->itemDamage == damage &&
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

    int addItemsToInventory(ItemStack* stack) {
        if (!stack || stack->stackSize <= 0) return 0;
        int maxStack = stack->getMaxStackSize();
        bool stackable = maxStack > 1;

        if (stackable) {
            while (stack->stackSize > 0) {
                int slot = getFirstPartialMatchingStack(stack->itemID, stack->itemDamage);
                if (slot < 0) slot = getFirstEmptyStack();
                if (slot < 0) break;

                if (!mainInventory[slot]) {
                    mainInventory[slot] = std::make_unique<ItemStack>(stack->itemID, 0, stack->itemDamage);
                }

                int limit = std::min(mainInventory[slot]->getMaxStackSize(), getInventoryStackLimit());
                int freeSpace = limit - mainInventory[slot]->stackSize;
                int amountToAdd = std::min(stack->stackSize, freeSpace);

                if (amountToAdd > 0) {
                    stack->stackSize -= amountToAdd;
                    mainInventory[slot]->stackSize += amountToAdd;
                    mainInventory[slot]->animationsToGo = 5;
                } else {
                    break;
                }
            }
        } else {
            // Non-stackable
            while (stack->stackSize > 0) {
                int slot = getFirstEmptyStack();
                if (slot < 0) break;
                mainInventory[slot] = std::make_unique<ItemStack>(stack->itemID, 1, stack->itemDamage);
                mainInventory[slot]->animationsToGo = 5;
                stack->stackSize--;
            }
        }

        return stack->stackSize;
    }

    void decrementAnimations() {
        for (auto& stack : mainInventory) {
            if (stack && stack->animationsToGo > 0) stack->animationsToGo--;
        }
    }

    bool consumeInventoryItem(int id) {
        int slot = getInventorySlotContainItem(id);
        if (slot < 0) return false;
        if (--mainInventory[slot]->stackSize <= 0) {
            mainInventory[slot].reset();
        }
        return true;
    }

    bool addItemStackToInventory(ItemStack* stack) {
        if (!stack || stack->stackSize <= 0) return false;
        if (stack->itemID <= 0 || stack->itemID >= 32000) return false;
        
        stack->stackSize = addItemsToInventory(stack);
        return stack->stackSize == 0;
    }

    // IInventory methods
    int getSizeInventory() override {
        return 36 + 4;
    }

    ItemStack* getStackInSlot(int slot) override {
        if (slot < 36) return mainInventory[slot].get();
        slot -= 36;
        if (slot < 4) return armorInventory[slot].get();
        slot -= 4;
        if (slot < 4) return craftingInventory[slot].get();
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
        auto uptr = stack ? std::make_unique<ItemStack>(*stack) : nullptr;
        if (slot < 36) {
            mainInventory[slot] = std::move(uptr);
        } else if (slot >= 36 && slot < 40) {
            armorInventory[slot - 36] = std::move(uptr);
        } else if (slot >= 40 && slot < 44) {
            craftingInventory[slot - 40] = std::move(uptr);
        }
    }
    
    std::string getInvName() override { return "Inventory"; }
    
    int getInventoryStackLimit() override { return 64; }
    
    void onInventoryChanged() override { inventoryChanged = true; }
    
    bool canInteractWith(EntityPlayer* pl) override { return true; }

    void dropAllItems() {
        for (auto& s : mainInventory)  s.reset();
        for (auto& s : armorInventory) s.reset();
    }

    int getTotalArmorValue() {
        int totalReduction = 0;
        int remainingDurability = 0;
        int totalDurability = 0;

        auto armorPointsForItem = [](int itemId) -> int {
            switch (itemId) {
                case 298: case 302: case 306: case 314: return 3;
                case 299: case 303: case 307: case 315: return 8;
                case 300: case 304: case 308: case 316: return 6;
                case 301: case 305: case 309: case 317: return 3;
                default: return 0;
            }
        };

        for (const auto& stack : armorInventory) {
            if (!stack) continue;

            const int armorPoints = armorPointsForItem(stack->itemID);
            if (armorPoints <= 0) continue;

            totalReduction += armorPoints;
            totalDurability += stack->getMaxDamage();
            remainingDurability += stack->getMaxDamage() - stack->itemDamage;
        }

        if (totalDurability == 0) {
            return 0;
        }

        return (totalReduction - 1) * remainingDurability / totalDurability + 1;
    }
    
    void damageArmor(int damage) {
        for (auto& stack : armorInventory) {
            if (stack) {
                stack->damageItem(damage);
                if (stack->stackSize == 0) stack.reset();
            }
        }
    }
    
    float getStrVsBlock(Block* block) {
        float f = 1.0f;
        if (mainInventory[currentItem]) {
            f *= mainInventory[currentItem]->getStrVsBlock(block);
        }
        return f;
    }

    bool canHarvestBlock(Block* block) {
        if (block->blockMaterial != &Material::rock
         && block->blockMaterial != &Material::iron
         && block->blockMaterial != &Material::snow
         && block->blockMaterial != &Material::builtSnow) {
            return true;
        }
        ItemStack* held = mainInventory[currentItem].get();
        if (!held || held->itemID <= 0 || held->itemID >= 32000) return false;
        Item* item = Item::itemsList[held->itemID];
        auto* tool = dynamic_cast<ItemTool*>(item);
        return tool && tool->canHarvestBlock(block->blockID);
    }

    void writeToNBT(std::shared_ptr<NBTCompound> nbtList) {
        for (size_t i = 0; i < mainInventory.size(); ++i) {
            if (mainInventory[i]) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i));
                mainInventory[i]->writeToNBT(tag);
                nbtList->setCompound("item_" + std::to_string(i), tag);
            }
        }
        for (size_t i = 0; i < armorInventory.size(); ++i) {
            if (armorInventory[i]) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i + 100));
                armorInventory[i]->writeToNBT(tag);
                nbtList->setCompound("item_" + std::to_string(i + 100), tag);
            }
        }
        for (size_t i = 0; i < craftingInventory.size(); ++i) {
            if (craftingInventory[i]) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i + 80));
                craftingInventory[i]->writeToNBT(tag);
                nbtList->setCompound("item_" + std::to_string(i + 80), tag);
            }
        }
    }

    void readFromNBT(std::shared_ptr<NBTCompound> nbtList) {
        mainInventory.clear();  mainInventory.resize(36);
        armorInventory.clear();  armorInventory.resize(4);
        craftingInventory.clear(); craftingInventory.resize(4);

        for (const auto& [key, tag] : nbtList->tags) {
            if (key.find("item_") == 0) {
                auto itemTag = std::dynamic_pointer_cast<NBTCompound>(tag);
                if (!itemTag) continue;
                
                int slot = itemTag->getByte("Slot") & 0xFF;
                auto stack = std::make_unique<ItemStack>(itemTag);
                
                if (slot >= 0 && slot < 36) {
                    mainInventory[slot] = std::move(stack);
                } else if (slot >= 80 && slot < 84) {
                    craftingInventory[slot - 80] = std::move(stack);
                } else if (slot >= 100 && slot < 104) {
                    armorInventory[slot - 100] = std::move(stack);
                }
            }
        }
    }
};
