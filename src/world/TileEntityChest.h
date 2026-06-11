#pragma once

#include "TileEntity.h"
#include "../core/IInventory.h"
#include "../core/ItemStack.h"
#include "../core/NBT.h"
#include "../core/RustBridge.h"

class TileEntityChest : public TileEntity, public IInventory {
public:
    static constexpr int CHEST_SIZE = 27;

    TileEntityChest() {
        state_ = RustBridge::chestCreate();
    }

    ~TileEntityChest() override = default;

    std::string getEntityId() const override { return "Chest"; }

    // IInventory interface
    int getSizeInventory() override { return CHEST_SIZE; }

    ItemStack* getStackInSlot(int slot) override {
        if (slot < 0 || slot >= CHEST_SIZE) return nullptr;
        auto& ffi = state_.slots[slot];
        if (ffi.itemID < 0) return nullptr;
        return reinterpret_cast<ItemStack*>(&ffi);
    }

    ItemStack* decrStackSize(int slot, int amount) override {
        if (slot < 0 || slot >= CHEST_SIZE) return nullptr;
        auto& ffi = state_.slots[slot];
        if (ffi.itemID < 0) return nullptr;

        if (ffi.stackSize <= amount) {
            auto result = new ItemStack(ffi.itemID, ffi.stackSize, ffi.itemDamage);
            ffi.itemID = -1;
            ffi.stackSize = 0;
            ffi.itemDamage = 0;
            markDirty();
            return result;
        }

        auto result = new ItemStack(ffi.itemID, amount, ffi.itemDamage);
        ffi.stackSize -= amount;
        markDirty();
        return result;
    }

    void setInventorySlotContents(int slot, ItemStack* stack) override {
        if (slot < 0 || slot >= CHEST_SIZE) {
            delete stack;
            return;
        }
        if (stack) {
            state_.slots[slot] = *reinterpret_cast<RustBridge::FfiItemStack*>(stack);
            if (state_.slots[slot].stackSize > getInventoryStackLimit())
                state_.slots[slot].stackSize = getInventoryStackLimit();
        } else {
            state_.slots[slot] = RustBridge::FfiItemStack{0, 0, -1, 0};
        }
        delete stack;
        markDirty();
    }

    std::string getInvName() override { return "Chest"; }
    int getInventoryStackLimit() override { return 64; }
    void onInventoryChanged() override { markDirty(); }
    bool canInteractWith(EntityPlayer* player) override { return true; }

    void readFromNBT(const NBTCompound& nbt) override {
        TileEntity::readFromNBT(nbt);

        // Reset state
        constexpr RustBridge::FfiItemStack emptySlot = {0, 0, -1, 0};
        for (auto& slot : state_.slots) {
            slot = emptySlot;
        }

        // Read Items list from NBT (Java format)
        auto itemsTag = nbt.tags.find("Items");
        if (itemsTag != nbt.tags.end()) {
            auto listTag = std::dynamic_pointer_cast<NBTList>(itemsTag->second);
            if (listTag) {
                for (const auto& tag : listTag->tags) {
                    auto itemCompound = std::dynamic_pointer_cast<NBTCompound>(tag);
                    if (itemCompound) {
                        int slot = itemCompound->getByte("Slot") & 0xFF;
                        if (slot >= 0 && slot < CHEST_SIZE) {
                            auto& ffi = state_.slots[slot];
                            ffi.itemID = itemCompound->getShort("id");
                            ffi.stackSize = itemCompound->getByte("Count");
                            ffi.itemDamage = itemCompound->getShort("Damage");
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
        for (int i = 0; i < CHEST_SIZE; ++i) {
            if (state_.slots[i].itemID >= 0) {
                auto itemCompound = std::make_shared<NBTCompound>();
                itemCompound->setByte("Slot", static_cast<int8_t>(i));
                itemCompound->setShort("id", static_cast<int16_t>(state_.slots[i].itemID));
                itemCompound->setByte("Count", static_cast<int8_t>(state_.slots[i].stackSize));
                itemCompound->setShort("Damage", static_cast<int16_t>(state_.slots[i].itemDamage));
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
    RustBridge::FfiChestState state_;
};

// Register TileEntityChest with ID "Chest" (matching Java)
REGISTER_TILE_ENTITY(TileEntityChest, "Chest");
