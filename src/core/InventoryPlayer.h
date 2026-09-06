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
        RustBridge::FfiItemStack ffiSlots[36];
        for (size_t i = 0; i < 36; ++i) {
            if (mainInventory[i] && mainInventory[i]->stackSize > 0 && mainInventory[i]->itemID > 0) {
                ffiSlots[i].stack_size = mainInventory[i]->stackSize;
                ffiSlots[i].animations_to_go = mainInventory[i]->animationsToGo;
                ffiSlots[i].item_id = mainInventory[i]->itemID;
                ffiSlots[i].item_damage = mainInventory[i]->itemDamage;
            } else {
                ffiSlots[i] = {0, 0, 0, 0};
            }
        }

        RustBridge::FfiItemStack ffiStack;
        ffiStack.stack_size = stack->stackSize;
        ffiStack.animations_to_go = stack->animationsToGo;
        ffiStack.item_id = stack->itemID;
        ffiStack.item_damage = stack->itemDamage;

        int rem = RustBridge::inventoryAddItem(ffiSlots, 36, &ffiStack, getInventoryStackLimit());
        stack->stackSize = rem;

        for (size_t i = 0; i < 36; ++i) {
            if (ffiSlots[i].item_id > 0 && ffiSlots[i].stack_size > 0) {
                if (!mainInventory[i]) {
                    mainInventory[i] = std::make_unique<ItemStack>(ffiSlots[i].item_id, ffiSlots[i].stack_size, ffiSlots[i].item_damage);
                } else {
                    mainInventory[i]->itemID = ffiSlots[i].item_id;
                    mainInventory[i]->stackSize = ffiSlots[i].stack_size;
                    mainInventory[i]->itemDamage = ffiSlots[i].item_damage;
                }
                mainInventory[i]->animationsToGo = ffiSlots[i].animations_to_go;
            } else {
                mainInventory[i].reset();
            }
        }
        return rem;
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
        RustBridge::FfiItemStack ffiArmor[4];
        for (size_t i = 0; i < 4; ++i) {
            if (armorInventory[i] && armorInventory[i]->stackSize > 0 && armorInventory[i]->itemID > 0) {
                ffiArmor[i].stack_size = armorInventory[i]->stackSize;
                ffiArmor[i].animations_to_go = armorInventory[i]->animationsToGo;
                ffiArmor[i].item_id = armorInventory[i]->itemID;
                ffiArmor[i].item_damage = armorInventory[i]->itemDamage;
            } else {
                ffiArmor[i] = {0, 0, 0, 0};
            }
        }
        return RustBridge::inventoryCalcArmor(ffiArmor, 4);
    }
    
    void damageArmor(int damage) {
        RustBridge::FfiItemStack ffiArmor[4];
        for (size_t i = 0; i < 4; ++i) {
            if (armorInventory[i] && armorInventory[i]->stackSize > 0 && armorInventory[i]->itemID > 0) {
                ffiArmor[i].stack_size = armorInventory[i]->stackSize;
                ffiArmor[i].animations_to_go = armorInventory[i]->animationsToGo;
                ffiArmor[i].item_id = armorInventory[i]->itemID;
                ffiArmor[i].item_damage = armorInventory[i]->itemDamage;
            } else {
                ffiArmor[i] = {0, 0, 0, 0};
            }
        }
        RustBridge::inventoryDamageArmor(ffiArmor, 4, damage);
        for (size_t i = 0; i < 4; ++i) {
            if (ffiArmor[i].item_id > 0 && ffiArmor[i].stack_size > 0) {
                if (armorInventory[i]) {
                    armorInventory[i]->stackSize = ffiArmor[i].stack_size;
                    armorInventory[i]->itemDamage = ffiArmor[i].item_damage;
                }
            } else {
                armorInventory[i].reset();
            }
        }
    }
    
    float getStrVsBlock(Block* block) {
        if (!block) return 1.0f;
        ItemStack* held = getCurrentItem();
        int heldId = (held && held->stackSize > 0) ? held->itemID : 0;
        return RustBridge::miningGetStrVsBlock(block->blockID, heldId);
    }

    bool canHarvestBlock(Block* block) {
        if (!block) return false;
        ItemStack* held = getCurrentItem();
        int heldId = (held && held->stackSize > 0) ? held->itemID : 0;
        return RustBridge::miningCanHarvest(block->blockID, heldId);
    }

    ItemStack getCraftingResult() const {
        RustBridge::FfiItemStack grid[4];
        for (size_t i = 0; i < 4; ++i) {
            if (craftingInventory[i] && craftingInventory[i]->stackSize > 0 && craftingInventory[i]->itemID > 0) {
                grid[i].stack_size = craftingInventory[i]->stackSize;
                grid[i].animations_to_go = craftingInventory[i]->animationsToGo;
                grid[i].item_id = craftingInventory[i]->itemID;
                grid[i].item_damage = craftingInventory[i]->itemDamage;
            } else {
                grid[i] = {0, 0, 0, 0};
            }
        }
        RustBridge::FfiItemStack out = RustBridge::inventoryCraft2x2(grid);
        if (out.item_id > 0 && out.stack_size > 0) {
            return ItemStack(out.item_id, out.stack_size, out.item_damage);
        }
        return ItemStack();
    }

    void consumeCraftingIngredients() {
        RustBridge::FfiItemStack grid[4];
        for (size_t i = 0; i < 4; ++i) {
            if (craftingInventory[i] && craftingInventory[i]->stackSize > 0 && craftingInventory[i]->itemID > 0) {
                grid[i].stack_size = craftingInventory[i]->stackSize;
                grid[i].animations_to_go = craftingInventory[i]->animationsToGo;
                grid[i].item_id = craftingInventory[i]->itemID;
                grid[i].item_damage = craftingInventory[i]->itemDamage;
            } else {
                grid[i] = {0, 0, 0, 0};
            }
        }
        RustBridge::inventoryConsumeCraft2x2(grid);
        for (size_t i = 0; i < 4; ++i) {
            if (grid[i].item_id > 0 && grid[i].stack_size > 0) {
                if (craftingInventory[i]) {
                    craftingInventory[i]->stackSize = grid[i].stack_size;
                }
            } else {
                craftingInventory[i].reset();
            }
        }
    }

    void writeToNBT(std::shared_ptr<NBTList> nbtList) {
        nbtList->tagType = NBTTagType::TAG_Compound;
        for (size_t i = 0; i < mainInventory.size(); ++i) {
            if (mainInventory[i]) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i));
                mainInventory[i]->writeToNBT(tag);
                nbtList->tags.push_back(tag);
            }
        }
        for (size_t i = 0; i < armorInventory.size(); ++i) {
            if (armorInventory[i]) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i + 100));
                armorInventory[i]->writeToNBT(tag);
                nbtList->tags.push_back(tag);
            }
        }
        for (size_t i = 0; i < craftingInventory.size(); ++i) {
            if (craftingInventory[i]) {
                auto tag = std::make_shared<NBTCompound>();
                tag->setByte("Slot", static_cast<int8_t>(i + 80));
                craftingInventory[i]->writeToNBT(tag);
                nbtList->tags.push_back(tag);
            }
        }
    }

    void readFromNBT(std::shared_ptr<NBTList> nbtList) {
        mainInventory.clear();  mainInventory.resize(36);
        armorInventory.clear();  armorInventory.resize(4);
        craftingInventory.clear(); craftingInventory.resize(4);

        if (!nbtList) return;
        for (const auto& tag : nbtList->tags) {
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
};
