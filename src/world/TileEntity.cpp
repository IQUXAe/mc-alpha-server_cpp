#include "TileEntity.h"
#include "World.h"
#include <iostream>

void TileEntity::readFromNBT(const NBTCompound& nbt) {
    xCoord = nbt.getInt("x");
    yCoord = nbt.getInt("y");
    zCoord = nbt.getInt("z");
}

void TileEntity::writeToNBT(NBTCompound& nbt) const {
    nbt.setString("id", getEntityId());
    nbt.setInt("x", xCoord);
    nbt.setInt("y", yCoord);
    nbt.setInt("z", zCoord);
}

void TileEntity::markDirty() {
    if (worldObj) {
        worldObj->markTileEntityChanged(xCoord, yCoord, zCoord, this);
    }
}
