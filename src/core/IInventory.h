#pragma once

#include <string>
#include <memory>
#include "ItemStack.h"

class EntityPlayer;

class IInventory {
public:
    virtual ~IInventory() = default;

    virtual int getSizeInventory() = 0;
    virtual ItemStack* getStackInSlot(int slot) = 0;
    virtual ItemStack* decrStackSize(int slot, int amount) = 0;
    virtual void setInventorySlotContents(int slot, ItemStack* stack) = 0;
    virtual std::string getInvName() = 0;
    virtual int getInventoryStackLimit() = 0;
    virtual void onInventoryChanged() = 0;
    virtual bool canInteractWith(EntityPlayer* player) = 0;
};
