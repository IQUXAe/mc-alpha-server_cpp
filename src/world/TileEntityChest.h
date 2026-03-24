#pragma once

#include "TileEntity.h"
#include "../core/IInventory.h"
#include "../core/ItemStack.h"
#include "../core/NBT.h"
#include <array>
#include <iostream>

class TileEntityChest : public TileEntity, public IInventory {
public:
    // Java uses array of 36 but returns 27 in getSizeInventory()
    // Client also uses 27 slots, so we must match this!
    static constexpr int CHEST_ARRAY_SIZE = 27;  // Match client's getSizeInventory()
    static constexpr int CHEST_SIZE = 27;
    
    TileEntityChest() {
        inventory_.fill(nullptr);
    }

    ~TileEntityChest() override {
        for (auto* stack : inventory_) {
            delete stack;
        }
    }

    std::string getEntityId() const override { return "Chest"; }

    // IInventory interface
    int getSizeInventory() override { return CHEST_SIZE; }
    
    ItemStack* getStackInSlot(int slot) override {
        if (slot < 0 || slot >= CHEST_SIZE) return nullptr;
        return inventory_[slot];
    }

    ItemStack* decrStackSize(int slot, int amount) override {
        if (slot < 0 || slot >= CHEST_SIZE || !inventory_[slot]) return nullptr;
        
        ItemStack* stack = inventory_[slot];
        if (stack->stackSize <= amount) {
            inventory_[slot] = nullptr;
            markDirty();
            return stack;
        }
        
        ItemStack* result = new ItemStack(stack->itemID, amount, stack->itemDamage);
        stack->stackSize -= amount;
        markDirty();
        return result;
    }

    void setInventorySlotContents(int slot, ItemStack* stack) override {
        if (slot < 0 || slot >= CHEST_SIZE) return;
        
        delete inventory_[slot];
        inventory_[slot] = stack;
        
        if (stack && stack->stackSize > getInventoryStackLimit()) {
            stack->stackSize = getInventoryStackLimit();
        }
        markDirty();
    }

    std::string getInvName() override { return "Chest"; }
    int getInventoryStackLimit() override { return 64; }
    void onInventoryChanged() override { markDirty(); }
    bool canInteractWith(EntityPlayer* player) override { return true; }

    void readFromNBT(const NBTCompound& nbt) override {
        TileEntity::readFromNBT(nbt);
        
        // Clear existing inventory
        for (auto* stack : inventory_) {
            delete stack;
        }
        inventory_.fill(nullptr);
        
        // Read Items list from NBT (Java format)
        auto itemsTag = nbt.tags.find("Items");
        if (itemsTag != nbt.tags.end()) {
            auto listTag = std::dynamic_pointer_cast<NBTList>(itemsTag->second);
            if (listTag) {
                for (const auto& tag : listTag->tags) {
                    auto itemCompound = std::dynamic_pointer_cast<NBTCompound>(tag);
                    if (itemCompound) {
                        int slot = itemCompound->getByte("Slot") & 0xFF;
                        if (slot >= 0 && slot < CHEST_ARRAY_SIZE) {
                            int itemId = itemCompound->getShort("id");
                            int count = itemCompound->getByte("Count");
                            int damage = itemCompound->getShort("Damage");
                            inventory_[slot] = new ItemStack(itemId, count, damage);
                        }
                    }
                }
            }
        }
    }

    void writeToNBT(NBTCompound& nbt) const override {
        TileEntity::writeToNBT(nbt);
        
        // Write Items list to NBT (Java format)
        std::vector<std::shared_ptr<NBTTag>> itemsList;
        for (int i = 0; i < CHEST_ARRAY_SIZE; ++i) {
            if (inventory_[i]) {
                auto itemCompound = std::make_shared<NBTCompound>();
                itemCompound->setByte("Slot", static_cast<int8_t>(i));
                itemCompound->setShort("id", static_cast<int16_t>(inventory_[i]->itemID));
                itemCompound->setByte("Count", static_cast<int8_t>(inventory_[i]->stackSize));
                itemCompound->setShort("Damage", static_cast<int16_t>(inventory_[i]->itemDamage));
                itemsList.push_back(itemCompound);
            }
        }
        
        if (!itemsList.empty()) {
            auto listTag = std::make_shared<NBTList>();
            listTag->tags = itemsList;
            listTag->tagType = NBTTagType::TAG_Compound;
            nbt.tags["Items"] = listTag;
        }
    }

private:
    std::array<ItemStack*, CHEST_ARRAY_SIZE> inventory_;
};

// Register TileEntityChest with ID "Chest" (matching Java)
REGISTER_TILE_ENTITY(TileEntityChest, "Chest");
