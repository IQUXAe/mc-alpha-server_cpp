#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "../core/NBT.h"

class World;

// Base class for all tile entities (blocks with extra data like chests, furnaces, signs)
class TileEntity {
public:
    World* worldObj = nullptr;
    int xCoord = 0;
    int yCoord = 0;
    int zCoord = 0;

    virtual ~TileEntity() = default;

    virtual void readFromNBT(const NBTCompound& nbt);
    virtual void writeToNBT(NBTCompound& nbt) const;
    virtual void updateEntity() {}
    
    virtual std::string getEntityId() const = 0;

protected:
    void markDirty();
};
