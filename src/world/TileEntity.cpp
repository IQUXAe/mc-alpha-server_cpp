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

std::unordered_map<std::string, std::function<std::unique_ptr<TileEntity>()>>& TileEntity::getRegistry() {
    static std::unordered_map<std::string, std::function<std::unique_ptr<TileEntity>()>> registry;
    return registry;
}

std::unique_ptr<TileEntity> TileEntity::createFromNBT(const NBTCompound& nbt) {
    auto it = nbt.tags.find("id");
    if (it == nbt.tags.end()) return nullptr;
    
    auto strTag = std::dynamic_pointer_cast<NBTString>(it->second);
    if (!strTag) return nullptr;
    
    auto& registry = getRegistry();
    auto regIt = registry.find(strTag->value);
    if (regIt == registry.end()) return nullptr;
    
    auto entity = regIt->second();
    entity->readFromNBT(nbt);
    return entity;
}
