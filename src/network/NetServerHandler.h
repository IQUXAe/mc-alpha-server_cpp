#pragma once

#include "NetHandler.h"
#include "NetworkManager.h"
#include "RustPackets.h"
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
    void sendPacket(const RustPacket& pkt);
    void sendMapChunk(int x, int y, int z, int sizeX, int sizeY, int sizeZ, const std::vector<uint8_t>& compressedData);
    void sendTileEntityPacket(TileEntity* te);  // Send Packet59 for a TileEntity
    void teleport(double x, double y, double z, float yaw, float pitch);
    void sendChunks();
    void sendInventory();
    void restoreHeldItem(int itemId);  // called on login to restore saved held item
    int getHeldItemId() const;
    bool hasChunkLoaded(int64_t key) const { return sentChunks_.contains(key); }
    static int64_t chunkKey(int x, int z) {
        return ((int64_t)(uint32_t)x) | (((int64_t)(uint32_t)z) << 32);
    }
    std::unordered_set<int64_t> sentChunks_;

    // NetHandler overrides
    void handleChat(const RustPacket3Chat& pkt) override;
    void handleRespawn() override;
    void handleUseEntity(const RustPacket7UseEntity& pkt) override;
    void handleFlying(const RustPacket10Flying& pkt) override;
    void handlePlayerPosition(const RustPacket11PlayerPosition& pkt) override;
    void handlePlayerLook(const RustPacket12PlayerLook& pkt) override;
    void handlePlayerLookMove(const RustPacket13PlayerLookMove& pkt) override;
    void handleBlockDig(const RustPacket14BlockDig& pkt) override;
    void handlePlace(const RustPacket15Place& pkt) override;
    void handleBlockItemSwitch(const RustPacket16BlockItemSwitch& pkt) override;
    void handleArmAnimation(const RustPacket18ArmAnimation& pkt) override;
    void handleKickDisconnect(const RustPacket255KickDisconnect& pkt) override;
    void handlePlayerInventory(const RustPacket5PlayerInventory& pkt) override;
    void handlePickupSpawn(const RustPacket21PickupSpawn& pkt) override;
    void handleComplexEntity(const RustPacket59ComplexEntity& pkt) override;
    void handleErrorMessage(const std::string& reason) override;
    bool shouldBypassReadTimeout() const override;

private:
    void processMovement(double x, double y, double stance, double z, float yaw, float pitch, bool moving, bool rotating, bool onGround);
    void handleCommand(const std::string& msg);
    void syncHeldItemSelection();
    ItemStack* getSelectedItemStack();
    MinecraftServer* mcServer_;
    std::unique_ptr<NetworkManager> netManager_;
    EntityPlayerMP* player_;
    int tickCounter_ = 0;
    std::unique_ptr<ItemStack> heldItem_; // fallback placeholder when item not yet in inventory
    int heldItemId_ = 0;             // itemID of what the player is holding

    // Anti-cheat: track last known position
    double lastX_ = 0.0, lastY_ = 0.0, lastZ_ = 0.0;
    bool hasMoved_ = false;

    // Chunk generation tracking
    int lastChunkX_ = -999999;
    int lastChunkZ_ = -999999;

    // Chunks queued (ordered, nearest first)
    std::vector<std::pair<int,int>> chunksToLoad_;
};
