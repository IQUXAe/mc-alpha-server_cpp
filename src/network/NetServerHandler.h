#pragma once

#include "NetHandler.h"
#include "NetworkManager.h"
#include "packets/AllPackets.h"
#include "../forward.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

class MinecraftServer;
class EntityPlayerMP;

class NetServerHandler : public NetHandler {
public:
    bool disconnected = false;

    NetServerHandler(MinecraftServer* server, std::unique_ptr<NetworkManager> netMgr, EntityPlayerMP* player);
    ~NetServerHandler();

    void tick();
    void kick(const std::string& reason);
    void sendPacket(std::unique_ptr<Packet> pkt);
    void teleport(double x, double y, double z, float yaw, float pitch);
    void sendChunks();
    void sendInventory();
    void restoreHeldItem(int itemId);  // called on login to restore saved held item
    int getHeldItemId() const;
    bool hasChunkLoaded(int64_t key) const { return sentChunks_.find(key) != sentChunks_.end(); }
    std::unordered_set<int64_t> sentChunks_;

    // NetHandler overrides
    void handleChat(Packet3Chat& pkt) override;
    void handleFlying(Packet10Flying& pkt) override;
    void handlePlayerPosition(Packet11PlayerPosition& pkt) override;
    void handlePlayerLook(Packet12PlayerLook& pkt) override;
    void handlePlayerLookMove(Packet13PlayerLookMove& pkt) override;
    void handleBlockDig(Packet14BlockDig& pkt) override;
    void handlePlace(Packet15Place& pkt) override;
    void handleBlockItemSwitch(Packet16BlockItemSwitch& pkt) override;
    void handleArmAnimation(Packet18ArmAnimation& pkt) override;
    void handleKickDisconnect(Packet255KickDisconnect& pkt) override;
    void handlePlayerInventory(Packet5PlayerInventory& pkt) override;
    void handlePickupSpawn(Packet21PickupSpawn& pkt) override;
    void handleErrorMessage(const std::string& reason) override;

private:
    MinecraftServer* mcServer_;
    std::unique_ptr<NetworkManager> netManager_;
    EntityPlayerMP* player_;
    int tickCounter_ = 0;
    ItemStack* heldItem_ = nullptr; // mirrors Java's field_10_k

    // Anti-cheat: track last known position
    double lastX_ = 0.0, lastY_ = 0.0, lastZ_ = 0.0;
    bool hasMoved_ = false;

    // Chunk generation tracking
    int lastChunkX_ = -999999;
    int lastChunkZ_ = -999999;

    // Chunks queued (ordered, nearest first)
    std::vector<std::pair<int,int>> chunksToLoad_;

    static int64_t chunkKey(int x, int z) {
        return ((int64_t)(uint32_t)x) | (((int64_t)(uint32_t)z) << 32);
    }
};

