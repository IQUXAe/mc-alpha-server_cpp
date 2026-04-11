#pragma once

#include "TileEntity.h"
#include "../core/IInventory.h"
#include "../core/ItemStack.h"
#include "../core/Item.h"
#include "../core/NBT.h"
#include <array>

class TileEntityFurnace : public TileEntity, public IInventory {
public:
    static constexpr int SLOT_INPUT = 0;
    static constexpr int SLOT_FUEL = 1;
    static constexpr int SLOT_OUTPUT = 2;
    static constexpr int FURNACE_SIZE = 3;
    static constexpr int COOK_TIME = 200; // 10 seconds at 20 ticks/sec
    
    TileEntityFurnace() {
        inventory_.fill(nullptr);
    }

    ~TileEntityFurnace() override {
        for (auto* stack : inventory_) {
            delete stack;
        }
    }

    std::string getEntityId() const override { return "Furnace"; }

    // IInventory interface
    int getSizeInventory() override { return FURNACE_SIZE; }
    
    ItemStack* getStackInSlot(int slot) override {
        if (slot < 0 || slot >= FURNACE_SIZE) return nullptr;
        return inventory_[slot];
    }

    ItemStack* decrStackSize(int slot, int amount) override {
        if (slot < 0 || slot >= FURNACE_SIZE || !inventory_[slot]) return nullptr;
        
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
        if (slot < 0 || slot >= FURNACE_SIZE) return;
        
        delete inventory_[slot];
        inventory_[slot] = stack;
        
        if (stack && stack->stackSize > getInventoryStackLimit()) {
            stack->stackSize = getInventoryStackLimit();
        }
        markDirty();
    }

    std::string getInvName() override { return "Furnace"; }
    int getInventoryStackLimit() override { return 64; }
    void onInventoryChanged() override { markDirty(); }
    bool canInteractWith(EntityPlayer* player) override { return true; }

    bool isBurning() const { return burnTime_ > 0; }
    int getBurnTime() const { return burnTime_; }
    int getCookTime() const { return cookTime_; }

    void updateEntity() override {
        if (!worldObj) return;

        bool wasBurning = burnTime_ > 0;
        bool changed = false;

        if (burnTime_ > 0) {
            --burnTime_;
        }

        // Try to start burning if we can smelt
        if (burnTime_ == 0 && canSmelt()) {
            currentItemBurnTime_ = burnTime_ = getItemBurnTime(inventory_[SLOT_FUEL]);
            if (burnTime_ > 0) {
                changed = true;
                if (inventory_[SLOT_FUEL]) {
                    --inventory_[SLOT_FUEL]->stackSize;
                    if (inventory_[SLOT_FUEL]->stackSize == 0) {
                        delete inventory_[SLOT_FUEL];
                        inventory_[SLOT_FUEL] = nullptr;
                    }
                }
            }
        }

        // Cook item if burning
        if (isBurning() && canSmelt()) {
            ++cookTime_;
            if (cookTime_ == COOK_TIME) {
                cookTime_ = 0;
                smeltItem();
                changed = true;
            }
        } else {
            cookTime_ = 0;
        }

        // Update furnace block state if burning state changed
        if (wasBurning != (burnTime_ > 0)) {
            changed = true;
            updateFurnaceBlockState(burnTime_ > 0);
        }

        if (changed) {
            markDirty();
        }

        // Send state to client every 5 ticks while burning so flame/cook progress bars animate.
        // The client uses BurnTime/BurnTimeTotal/CookTime to render the GUI progress.
        if (isBurning() && burnTime_ % 5 == 0) {
            markDirty();
        }
    }

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
                        int8_t slot = itemCompound->getByte("Slot");
                        if (slot >= 0 && slot < FURNACE_SIZE) {
                            int itemId = itemCompound->getShort("id");
                            int count = itemCompound->getByte("Count");
                            int damage = itemCompound->getShort("Damage");
                            inventory_[slot] = new ItemStack(itemId, count, damage);
                        }
                    }
                }
            }
        }
        
        // Read furnace state (Java format)
        burnTime_ = nbt.getShort("BurnTime");
        cookTime_ = nbt.getShort("CookTime");
        currentItemBurnTime_ = nbt.getShort("BurnTimeTotal");
        // Fallback: if BurnTimeTotal wasn't saved, recalculate from fuel slot
        if (currentItemBurnTime_ == 0 && burnTime_ > 0) {
            currentItemBurnTime_ = getItemBurnTime(inventory_[SLOT_FUEL]);
        }
    }

    void writeToNBT(NBTCompound& nbt) const override {
        TileEntity::writeToNBT(nbt);
        
        // Write furnace state first (Java order)
        nbt.setShort("BurnTime", static_cast<int16_t>(burnTime_));
        nbt.setShort("CookTime", static_cast<int16_t>(cookTime_));
        nbt.setShort("BurnTimeTotal", static_cast<int16_t>(currentItemBurnTime_));
        
        // Write Items list to NBT (Java format)
        std::vector<std::shared_ptr<NBTTag>> itemsList;
        for (int i = 0; i < FURNACE_SIZE; ++i) {
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
    std::array<ItemStack*, FURNACE_SIZE> inventory_;
    int burnTime_ = 0;          // Current burn time remaining
    int cookTime_ = 0;          // Current cooking progress (0-200)
    int currentItemBurnTime_ = 0; // Max burn time of current fuel item

    bool canSmelt() const {
        if (!inventory_[SLOT_INPUT]) return false;
        
        int resultId = getSmeltingResult(inventory_[SLOT_INPUT]->itemID);
        if (resultId < 0) return false;
        if (resultId >= 32000) return false;
        
        // Validate result item exists
        Item* resultItem = (resultId < 256) ? nullptr : Item::itemsList[resultId];
        int maxStack = resultItem ? resultItem->getMaxStackSize() : 64;
        
        if (!inventory_[SLOT_OUTPUT]) return true;
        if (inventory_[SLOT_OUTPUT]->itemID != resultId) return false;
        
        int resultSize = inventory_[SLOT_OUTPUT]->stackSize + 1;
        return resultSize <= 64 && resultSize <= maxStack;
    }

    void smeltItem() {
        if (!canSmelt()) return;
        
        int resultId = getSmeltingResult(inventory_[SLOT_INPUT]->itemID);
        
        if (!inventory_[SLOT_OUTPUT]) {
            inventory_[SLOT_OUTPUT] = new ItemStack(resultId, 1, 0);
        } else if (inventory_[SLOT_OUTPUT]->itemID == resultId) {
            ++inventory_[SLOT_OUTPUT]->stackSize;
        }
        
        --inventory_[SLOT_INPUT]->stackSize;
        if (inventory_[SLOT_INPUT]->stackSize <= 0) {
            delete inventory_[SLOT_INPUT];
            inventory_[SLOT_INPUT] = nullptr;
        }
    }

    static int getSmeltingResult(int itemId) {
        // Smelting recipes matching Java Alpha 1.2.6 exactly
        // Block IDs
        if (itemId == 15) return 265; // Iron Ore -> Iron Ingot
        if (itemId == 14) return 266; // Gold Ore -> Gold Ingot  
        if (itemId == 56) return 264; // Diamond Ore -> Diamond
        if (itemId == 12) return 20;  // Sand -> Glass
        if (itemId == 4) return 1;    // Cobblestone -> Stone
        
        // Item IDs
        if (itemId == 319) return 320; // Raw Pork -> Cooked Pork
        if (itemId == 349) return 350; // Raw Fish -> Cooked Fish
        if (itemId == 337) return 336; // Clay (item) -> Brick (item)
        
        return -1;
    }

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
