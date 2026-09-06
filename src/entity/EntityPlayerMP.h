#pragma once

#include "EntityLiving.h"
#include "../network/NetServerHandler.h"
#include "../server/ItemInWorldManager.h"
#include "../core/InventoryPlayer.h"
#include "../core/NBT.h"
#include "../core/RustBridge.h"
#include <string>
#include <fstream>
#include <iostream>

class NetServerHandler;
class MinecraftServer;
class World;

class EntityPlayerMP : public EntityPlayer {
public:
    using EntityLiving::readFromNBT;
    using EntityLiving::writeToNBT;

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
    int armorDamageCarry = 0;
    int respawnInvulnerabilityTicks = 60;
    int armSwingTicks = 0;
    bool isSwinging = false;
    std::string lastDeathMessage_;

    EntityPlayerMP(MinecraftServer* server, World* world, const std::string& name)
        : mcServer(server), inventory(this) {
        this->worldObj = world;
        this->itemInWorldManager = new ItemInWorldManager(world); // ALLOW_NEW
        this->itemInWorldManager->setPlayer(this);
        this->username = name;
        height = 1.8f;
        width = 0.6f;
        stepHeight = 0.5f;
    }

    ~EntityPlayerMP() { // ALLOW_DELETE: owned raw ptr
        delete itemInWorldManager; // ALLOW_DELETE
    }

    void tick() override;
    void onDeath() override;
    void swingItem();
    void resetCombatState();
    bool canAttackNow() const;
    void markAttackPerformed();

    void damageEntity(int amount) override { attackEntityFrom(nullptr, amount); }

    void heal(int amount) override {
        EntityPlayer::heal(amount);
        if (netHandler) {
            netHandler->sendPacket(std::make_unique<Packet8UpdateHealth>(health));
        }
    }

    void attackEntityFrom(Entity* attacker, int amount) override;

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
        auto invTag = std::make_shared<NBTList>();
        inventory.writeToNBT(invTag);
        nbt->setList("Inventory", invTag);
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
        auto invIt = nbt->tags.find("Inventory");
        if (invIt != nbt->tags.end()) {
            if (auto listTag = std::dynamic_pointer_cast<NBTList>(invIt->second)) {
                inventory.readFromNBT(listTag);
            }
        }
    }

    // File-based save/load via Rust GZip NBT storage
    bool saveToFile(const std::string& filepath) {
        RustBridge::AlphaPlayerData data{};
        data.pos_x = posX;
        data.pos_y = posY;
        data.pos_z = posZ;
        data.motion_x = motionX;
        data.motion_y = motionY;
        data.motion_z = motionZ;
        data.rotation_yaw = rotationYaw;
        data.rotation_pitch = rotationPitch;
        data.fall_distance = fallDistance;
        data.fire = static_cast<int16_t>(fire);
        data.air = static_cast<int16_t>(air);
        data.on_ground = onGround;
        data.health = static_cast<int16_t>(health);
        data.hurt_time = static_cast<int16_t>(hurtTime);
        data.death_time = static_cast<int16_t>(deathTime);
        data.attack_time = static_cast<int16_t>(attackTime);
        data.dimension = dimension;
        data.score = score;
        data.held_item_id = savedHeldItemId;

        size_t count = 0;
        for (size_t i = 0; i < inventory.mainInventory.size() && count < 64; ++i) {
            if (inventory.mainInventory[i]) {
                data.slots[count++] = RustBridge::FfiPlayerSlot{
                    static_cast<uint8_t>(i),
                    static_cast<int16_t>(inventory.mainInventory[i]->itemID),
                    static_cast<int8_t>(inventory.mainInventory[i]->stackSize),
                    static_cast<int16_t>(inventory.mainInventory[i]->itemDamage)
                };
            }
        }
        for (size_t i = 0; i < inventory.armorInventory.size() && count < 64; ++i) {
            if (inventory.armorInventory[i]) {
                data.slots[count++] = RustBridge::FfiPlayerSlot{
                    static_cast<uint8_t>(i + 100),
                    static_cast<int16_t>(inventory.armorInventory[i]->itemID),
                    static_cast<int8_t>(inventory.armorInventory[i]->stackSize),
                    static_cast<int16_t>(inventory.armorInventory[i]->itemDamage)
                };
            }
        }
        for (size_t i = 0; i < inventory.craftingInventory.size() && count < 64; ++i) {
            if (inventory.craftingInventory[i]) {
                data.slots[count++] = RustBridge::FfiPlayerSlot{
                    static_cast<uint8_t>(i + 80),
                    static_cast<int16_t>(inventory.craftingInventory[i]->itemID),
                    static_cast<int8_t>(inventory.craftingInventory[i]->stackSize),
                    static_cast<int16_t>(inventory.craftingInventory[i]->itemDamage)
                };
            }
        }
        data.slots_count = count;

        return RustBridge::savePlayerData(filepath, data);
    }

    bool loadFromFile(const std::string& filepath) {
        RustBridge::AlphaPlayerData data{};
        if (!RustBridge::loadPlayerData(filepath, data)) {
            return false;
        }

        posX = data.pos_x;
        posY = data.pos_y;
        posZ = data.pos_z;
        motionX = data.motion_x;
        motionY = data.motion_y;
        motionZ = data.motion_z;
        rotationYaw = data.rotation_yaw;
        rotationPitch = data.rotation_pitch;
        fallDistance = data.fall_distance;
        fire = data.fire;
        air = data.air;
        onGround = data.on_ground;
        health = data.health;
        hurtTime = data.hurt_time;
        deathTime = data.death_time;
        attackTime = data.attack_time;
        dimension = data.dimension;
        score = data.score;
        savedHeldItemId = data.held_item_id;

        inventory.mainInventory.clear(); inventory.mainInventory.resize(36);
        inventory.armorInventory.clear(); inventory.armorInventory.resize(4);
        inventory.craftingInventory.clear(); inventory.craftingInventory.resize(4);

        for (size_t i = 0; i < data.slots_count && i < 64; ++i) {
            const auto& s = data.slots[i];
            auto stack = std::make_unique<ItemStack>(s.item_id, s.count, s.damage);
            if (s.slot < 36) {
                inventory.mainInventory[s.slot] = std::move(stack);
            } else if (s.slot >= 80 && s.slot < 84) {
                inventory.craftingInventory[s.slot - 80] = std::move(stack);
            } else if (s.slot >= 100 && s.slot < 104) {
                inventory.armorInventory[s.slot - 100] = std::move(stack);
            }
        }
        return true;
    }

private:
    void updateDeathMessage(Entity* attacker);
};
