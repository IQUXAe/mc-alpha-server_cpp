#pragma once

#include "EntityLiving.h"
#include "../network/NetServerHandler.h"
#include <string>

class NetServerHandler;
class MinecraftServer;
class World;

class EntityPlayerMP : public EntityPlayer {
public:
    NetServerHandler* netHandler = nullptr;
    MinecraftServer* mcServer = nullptr;

    // Chunk tracking
    int managedPosX = 0;
    int managedPosZ = 0;

    EntityPlayerMP(MinecraftServer* server, World* world, const std::string& name)
        : mcServer(server) {
        this->worldObj = world;
        this->username = name;
        height = 1.8f;
        width = 0.6f;
    }

    void tick() override {
        EntityPlayer::tick();
    }

    void sendPacket(std::unique_ptr<Packet> pkt) {
        if (netHandler) {
            netHandler->sendPacket(std::move(pkt));
        }
    }
};
