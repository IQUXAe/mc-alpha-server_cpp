#pragma once

#include "TileEntity.h"
#include "../core/IInventory.h"
#include "../core/ItemStack.h"
#include "../core/Item.h"
#include "../core/NBT.h"

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
        if (ffi.itemID < 0) return nullptr;
        return reinterpret_cast<ItemStack*>(&ffi);
    }

    ItemStack* decrStackSize(int slot, int amount) override {
        if (slot < 0 || slot >= FURNACE_SIZE) return nullptr;
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
        if (slot < 0 || slot >= FURNACE_SIZE) {
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

    std::string getInvName() override { return "Furnace"; }
    int getInventoryStackLimit() override { return 64; }
    void onInventoryChanged() override { markDirty(); }
    bool canInteractWith(EntityPlayer* player) override { return true; }

    bool isBurning() const { return state_.burnTime > 0; }
    int getBurnTime() const { return state_.burnTime; }
    int getCookTime() const { return state_.cookTime; }

    void updateEntity() override {
        if (!worldObj) return;

        int fuelBurnTime = 0;
        if (state_.slots[SLOT_FUEL].itemID >= 0) {
            fuelBurnTime = getItemBurnTime(
                reinterpret_cast<ItemStack*>(&state_.slots[SLOT_FUEL]));
        }

        auto result = RustBridge::furnaceTick(&state_, fuelBurnTime);

        if (result.needsBlockUpdate) {
            updateFurnaceBlockState(state_.burnTime > 0);
        }
        if (result.changed || (state_.burnTime > 0 && state_.burnTime % 5 == 0)) {
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
                        int8_t slot = itemCompound->getByte("Slot");
                        if (slot >= 0 && slot < FURNACE_SIZE) {
                            auto& ffi = state_.slots[slot];
                            ffi.itemID = itemCompound->getShort("id");
                            ffi.stackSize = itemCompound->getByte("Count");
                            ffi.itemDamage = itemCompound->getShort("Damage");
                        }
                    }
                }
            }
        }

        // Read furnace state (Java format)
        state_.burnTime = nbt.getShort("BurnTime");
        state_.cookTime = nbt.getShort("CookTime");
        state_.currentItemBurnTime = nbt.getShort("BurnTimeTotal");
        // Fallback: if BurnTimeTotal wasn't saved, recalculate from fuel slot
        if (state_.currentItemBurnTime == 0 && state_.burnTime > 0 && state_.slots[SLOT_FUEL].itemID >= 0) {
            state_.currentItemBurnTime = static_cast<int16_t>(getItemBurnTime(
                reinterpret_cast<ItemStack*>(&state_.slots[SLOT_FUEL])));
        }
    }

    void writeToNBT(NBTCompound& nbt) const override {
        TileEntity::writeToNBT(nbt);

        // Write furnace state first (Java order)
        nbt.setShort("BurnTime", state_.burnTime);
        nbt.setShort("CookTime", state_.cookTime);
        nbt.setShort("BurnTimeTotal", state_.currentItemBurnTime);

        // Write Items list to NBT (Java format)
        std::vector<std::shared_ptr<NBTTag>> itemsList;
        for (int i = 0; i < FURNACE_SIZE; ++i) {
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
    RustBridge::FfiFurnaceState state_;

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
