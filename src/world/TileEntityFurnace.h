#pragma once

#include "TileEntity.h"
#include "../core/IInventory.h"
#include "../core/ItemStack.h"
#include "../core/Item.h"
#include "../core/NBT.h"
#include "../core/RustBridge.h"

#include <cstring>
#include <memory>

class TileEntityFurnace : public TileEntity, public IInventory {
public:
    static constexpr int SLOT_INPUT = 0;
    static constexpr int SLOT_FUEL = 1;
    static constexpr int SLOT_OUTPUT = 2;
    static constexpr int FURNACE_SIZE = 3;
    static constexpr int COOK_TIME = 200; // 10 seconds at 20 ticks/sec

    TileEntityFurnace() {
        state_ = RustBridge::furnaceCreate();
    }

    ~TileEntityFurnace() override = default;

    std::string getEntityId() const override { return "Furnace"; }

    // IInventory interface
    int getSizeInventory() override { return FURNACE_SIZE; }

    ItemStack* getStackInSlot(int slot) override {
        if (slot < 0 || slot >= FURNACE_SIZE) return nullptr;
        auto& ffi = state_.slots[slot];
        if (ffi.item_id < 0) return nullptr;
        auto* result = new ItemStack(ffi.item_id, ffi.stack_size, ffi.item_damage);
        return result;
    }

    ItemStack* decrStackSize(int slot, int amount) override {
        if (slot < 0 || slot >= FURNACE_SIZE) return nullptr;
        auto& ffi = state_.slots[slot];
        if (ffi.item_id < 0) return nullptr;

        if (ffi.stack_size <= amount) {
            auto result = std::make_unique<ItemStack>(ffi.item_id, ffi.stack_size, ffi.item_damage);
            ffi.item_id = -1;
            ffi.stack_size = 0;
            ffi.item_damage = 0;
            markDirty();
            return result.release();
        }

        auto result = std::make_unique<ItemStack>(ffi.item_id, amount, ffi.item_damage);
        ffi.stack_size -= amount;
        markDirty();
        return result.release();
    }

    void setInventorySlotContents(int slot, ItemStack* stack) override {
        std::unique_ptr<ItemStack> guard(stack);
        if (slot < 0 || slot >= FURNACE_SIZE) {
            return;
        }
        if (stack) {
            RustBridge::FfiItemStack ffi;
            std::memcpy(&ffi, stack, sizeof(ffi));
            state_.slots[slot] = ffi;
            if (state_.slots[slot].stack_size > getInventoryStackLimit())
                state_.slots[slot].stack_size = getInventoryStackLimit();
        } else {
            state_.slots[slot] = RustBridge::FfiItemStack{0, 0, -1, 0};
        }
        markDirty();
    }

    std::string getInvName() override { return "Furnace"; }
    int getInventoryStackLimit() override { return 64; }
    void onInventoryChanged() override { markDirty(); }
    bool canInteractWith(EntityPlayer* player) override { return true; }

    bool isBurning() const { return state_.burn_time > 0; }
    int getBurnTime() const { return state_.burn_time; }
    int getCookTime() const { return state_.cook_time; }

    void updateEntity() override {
        if (!worldObj) return;

        int fuelBurnTime = 0;
        if (state_.slots[SLOT_FUEL].item_id >= 0) {
            ItemStack tmp(state_.slots[SLOT_FUEL].item_id, state_.slots[SLOT_FUEL].stack_size, state_.slots[SLOT_FUEL].item_damage);
            fuelBurnTime = getItemBurnTime(&tmp);
        }

        auto result = RustBridge::furnaceTick(&state_, fuelBurnTime);

        if (result.needsBlockUpdate) {
            updateFurnaceBlockState(state_.burn_time > 0);
        }
        if (result.changed || (state_.burn_time > 0 && state_.burn_time % 5 == 0)) {
            markDirty();
        }
    }

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
                        if (slot >= 0 && slot < FURNACE_SIZE) {
                            auto& ffi = state_.slots[slot];
                            ffi.item_id = itemCompound->getShort("id");
                            ffi.stack_size = itemCompound->getByte("Count");
                            ffi.item_damage = itemCompound->getShort("Damage");
                        }
                    }
                }
            }
        }

        // Read furnace state (Java format)
        state_.burn_time = nbt.getShort("BurnTime");
        state_.cook_time = nbt.getShort("CookTime");
        state_.current_item_burn_time = nbt.getShort("BurnTimeTotal");
        // Fallback: if BurnTimeTotal wasn't saved, recalculate from fuel slot
        if (state_.current_item_burn_time == 0 && state_.burn_time > 0 && state_.slots[SLOT_FUEL].item_id >= 0) {
            ItemStack tmp(state_.slots[SLOT_FUEL].item_id, state_.slots[SLOT_FUEL].stack_size, state_.slots[SLOT_FUEL].item_damage);
            state_.current_item_burn_time = static_cast<int16_t>(getItemBurnTime(&tmp));
        }
    }

    void writeToNBT(NBTCompound& nbt) const override {
        TileEntity::writeToNBT(nbt);

        // Write furnace state first (Java order)
        nbt.setShort("BurnTime", state_.burn_time);
        nbt.setShort("CookTime", state_.cook_time);
        nbt.setShort("BurnTimeTotal", state_.current_item_burn_time);

        // Write Items list to NBT (Java format)
        std::vector<std::shared_ptr<NBTTag>> itemsList;
        for (int i = 0; i < FURNACE_SIZE; ++i) {
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
    RustBridge::FfiFurnaceState state_;

private:
    static int getItemBurnTime(ItemStack* stack) {
        if (!stack) return 0;

        int itemId = stack->itemID;

        // Blocks
        if (itemId < 256) {
            Block* block = Block::blocksList[itemId];
            if (block && block->blockMaterial == &Material::wood) {
                return 300; // 15 seconds
            }
        }

        // Items
        if (itemId == 280) return 100;   // Stick - 5 seconds
        if (itemId == 263) return 1600;  // Coal - 80 seconds
        if (itemId == 327) return 20000; // Lava Bucket - 1000 seconds

        return 0;
    }

    void updateFurnaceBlockState(bool burning);
};

// Implementation of updateFurnaceBlockState (needs to be after BlockFurnace is defined)
inline void TileEntityFurnace::updateFurnaceBlockState(bool burning) {
    if (!worldObj) return;
    // Forward declaration - actual implementation in BlockFurnace.h
    extern void updateFurnaceState(bool active, World* world, int x, int y, int z);
    updateFurnaceState(burning, worldObj, xCoord, yCoord, zCoord);
}

// Register TileEntityFurnace with ID "Furnace" (matching Java)
REGISTER_TILE_ENTITY(TileEntityFurnace, "Furnace");
