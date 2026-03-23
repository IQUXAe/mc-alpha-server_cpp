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

    static std::unique_ptr<TileEntity> createFromNBT(const NBTCompound& nbt);
    
protected:
    void markDirty();

private:
    static std::unordered_map<std::string, std::function<std::unique_ptr<TileEntity>()>>& getRegistry();
    
    template<typename T>
    friend struct TileEntityRegistrar;
};

// Helper for automatic registration
template<typename T>
struct TileEntityRegistrar {
    TileEntityRegistrar(const std::string& id) {
        TileEntity::getRegistry()[id] = []() -> std::unique_ptr<TileEntity> {
            return std::make_unique<T>();
        };
    }
};

#define REGISTER_TILE_ENTITY(Class, ID) \
    static TileEntityRegistrar<Class> Class##_registrar(ID)
