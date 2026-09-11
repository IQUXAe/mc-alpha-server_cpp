#pragma once

#include "TileEntity.h"
#include "../core/IInventory.h"
#include "../core/ItemStack.h"
#include "../core/NBT.h"
#include "../core/RustBridge.h"

#include <cstring>

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
        if (ffi.item_id < 0) return nullptr;
        auto* result = new ItemStack(ffi.item_id, ffi.stack_size, ffi.item_damage);
        return result;
    }

    ItemStack* decrStackSize(int slot, int amount) override {
        RustBridge::TileTaken taken{};
        if (!RustBridge::tileSlotTake(state_.slots, CHEST_SIZE, slot, amount, &taken)
            || !taken.has_item) {
            return nullptr;
        }
        markDirty();
        auto result = std::make_unique<ItemStack>(taken.item_id, taken.count, taken.damage);
        return result.release();
    }

    void setInventorySlotContents(int slot, ItemStack* stack) override {
        std::unique_ptr<ItemStack> guard(stack);
        bool dirty;
        if (stack) {
            dirty = RustBridge::tileSlotStore(state_.slots, CHEST_SIZE, slot, true,
                                              stack->itemID, stack->stackSize, stack->itemDamage,
                                              getInventoryStackLimit());
        } else {
            dirty = RustBridge::tileSlotStore(state_.slots, CHEST_SIZE, slot, false,
                                              0, 0, 0, getInventoryStackLimit());
        }
        if (dirty) {
            markDirty();
        }
    }

    std::string getInvName() override { return "Chest"; }
    int getInventoryStackLimit() override { return 64; }
    void onInventoryChanged() override { markDirty(); }
    bool canInteractWith(EntityPlayer* player) override { return true; }

    void readFromNBT(const NBTCompound& nbt) override {
        TileEntity::readFromNBT(nbt);

        // Reset state
        RustBridge::tileSlotsClear(state_.slots, CHEST_SIZE);

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
                            ffi.item_id = itemCompound->getShort("id");
                            ffi.stack_size = itemCompound->getByte("Count");
                            ffi.item_damage = itemCompound->getShort("Damage");
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
            if (state_.slots[i].item_id >= 0) {
                auto itemCompound = std::make_shared<NBTCompound>();
                itemCompound->setByte("Slot", static_cast<int8_t>(i));
                itemCompound->setShort("id", static_cast<int16_t>(state_.slots[i].item_id));
                itemCompound->setByte("Count", static_cast<int8_t>(state_.slots[i].stack_size));
                itemCompound->setShort("Damage", static_cast<int16_t>(state_.slots[i].item_damage));
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

public:
    RustBridge::FfiChestState state_;
};
