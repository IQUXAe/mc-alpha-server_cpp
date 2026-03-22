#pragma once

#include "EntityLiving.h"
#include "../network/NetServerHandler.h"
#include "../server/ItemInWorldManager.h"
#include "../core/InventoryPlayer.h"
#include "../core/NBT.h"
#include <string>
#include <fstream>
#include <iostream>

class NetServerHandler;
class MinecraftServer;
class World;

class EntityPlayerMP : public EntityPlayer {
public:
    NetServerHandler* netHandler = nullptr;
    MinecraftServer* mcServer = nullptr;
    ItemInWorldManager* itemInWorldManager = nullptr;
    InventoryPlayer inventory;

    // Chunk tracking
    int managedPosX = 0;
    int managedPosZ = 0;
    
    // Mining state
    int miningStartX = 0;
    int miningStartY = 0;
    int miningStartZ = 0;
    int64_t miningStartTime = 0;
    int miningTicks = -1;

    // Persisted held item id (restored into NetServerHandler on login)
    int savedHeldItemId = 0;

    EntityPlayerMP(MinecraftServer* server, World* world, const std::string& name)
        : mcServer(server), inventory(this) {
        this->worldObj = world;
        this->itemInWorldManager = new ItemInWorldManager(world);
        this->itemInWorldManager->setPlayer(this);
        this->username = name;
        height = 1.8f;
        width = 0.6f;
    }

    ~EntityPlayerMP() {
        delete itemInWorldManager;
    }

    void tick() override {
        EntityPlayer::tick();
        if (itemInWorldManager) itemInWorldManager->tick();
    }

    ItemStack* getCurrentEquippedItem() {
        return inventory.getCurrentItem();
    }

    void destroyCurrentEquippedItem() {
        inventory.setInventorySlotContents(inventory.currentItem, nullptr);
    }

    bool canHarvestBlock(Block* block) {
        return inventory.canHarvestBlock(block);
    }

    void sendPacket(std::unique_ptr<Packet> pkt) {
        if (netHandler) {
            netHandler->sendPacket(std::move(pkt));
        }
    }

    // NBT save/load (mirrors Java's EntityPlayer.writeEntityToNBT/readEntityFromNBT)
    void writeToNBT(std::shared_ptr<NBTCompound> nbt) {
        // Position
        nbt->setDouble("PosX", posX);
        nbt->setDouble("PosY", posY);
        nbt->setDouble("PosZ", posZ);
        
        // Rotation
        nbt->setFloat("RotationYaw", rotationYaw);
        nbt->setFloat("RotationPitch", rotationPitch);
        
        // Health
        nbt->setShort("Health", health);
        
        // Held item (what the player is holding, mirrors field_10_k)
        nbt->setInt("HeldItemId", savedHeldItemId);
        
        // Inventory
        auto invTag = std::make_shared<NBTCompound>();
        inventory.writeToNBT(invTag);
        nbt->setCompound("Inventory", invTag);
    }

    void readFromNBT(std::shared_ptr<NBTCompound> nbt) {
        // Position
        posX = nbt->getDouble("PosX");
        posY = nbt->getDouble("PosY");
        posZ = nbt->getDouble("PosZ");
        
        // Rotation
        rotationYaw = nbt->getFloat("RotationYaw");
        rotationPitch = nbt->getFloat("RotationPitch");
        
        // Health
        health = nbt->getShort("Health");
        
        // Held item
        savedHeldItemId = nbt->getInt("HeldItemId");
        
        // Inventory
        auto invTag = nbt->getCompound("Inventory");
        if (invTag) {
            inventory.readFromNBT(invTag);
        }
    }

    // File-based save/load
    bool saveToFile(const std::string& filepath) {
        try {
            auto nbt = std::make_shared<NBTCompound>();
            writeToNBT(nbt);
            
            ByteBuffer buf;
            nbt->writeRoot(buf, "Player");
            
            std::ofstream file(filepath, std::ios::binary);
            if (!file.is_open()) return false;
            
            file.write(reinterpret_cast<const char*>(buf.data.data()), buf.data.size());
            return true;
        } catch (...) {
            return false;
        }
    }

    bool loadFromFile(const std::string& filepath) {
        try {
            std::ifstream file(filepath, std::ios::binary);
            if (!file.is_open()) return false;
            
            file.seekg(0, std::ios::end);
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);
            
            std::vector<uint8_t> data(size);
            file.read(reinterpret_cast<char*>(data.data()), size);
            
            ByteBuffer buf(data);
            auto nbt = NBTCompound::readRoot(buf);
            if (!nbt) return false;
            
            readFromNBT(nbt);
            return true;
        } catch (...) {
            return false;
        }
    }
};
