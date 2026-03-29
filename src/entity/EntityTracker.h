#pragma once

#include "../entity/Entity.h"
#include "../entity/EntityArrow.h"
#include "../entity/EntityPlayerMP.h"
#include "../network/packets/AllPackets.h"

#include <unordered_map>
#include <unordered_set>
#include <memory>

class MinecraftServer;

// Mirrors Java's EntityTrackerEntry
struct TrackerEntry {
    Entity* entity;
    int trackingRange;   // max distance in blocks
    int updateRate;      // ticks between updates
    bool sendVelocity;

    // Last sent fixed-point position (posX * 32)
    int lastFixedX, lastFixedY, lastFixedZ;
    int8_t lastYawByte, lastPitchByte;

    // Last sent held item (players only)
    int lastHeldItemId = 0;
    bool lastSneaking = false;
    bool lastBurning = false;
    int32_t lastMountedEntityId = -1;
    double lastMotionX = 0.0, lastMotionY = 0.0, lastMotionZ = 0.0;

    int tickCounter = 0;

    // Players currently receiving updates for this entity
    std::unordered_set<EntityPlayerMP*> trackingPlayers;

    TrackerEntry(Entity* e, int range, int rate, bool vel)
        : entity(e), trackingRange(range), updateRate(rate), sendVelocity(vel) {
        lastFixedX = (int)(e->posX * 32.0);
        lastFixedY = (int)(e->posY * 32.0);
        lastFixedZ = (int)(e->posZ * 32.0);
        lastYawByte   = static_cast<int8_t>(static_cast<int>(std::floor(e->rotationYaw   * 256.0f / 360.0f)) & 0xFF);
        lastPitchByte = static_cast<int8_t>(static_cast<int>(std::floor(e->rotationPitch * 256.0f / 360.0f)) & 0xFF);
        lastMountedEntityId = e->ridingEntity ? e->ridingEntity->entityId : -1;
    }

    // Build the initial spawn packet for this entity
    std::unique_ptr<Packet> makeSpawnPacket() const;

    // Send this entry's spawn packet + initial state to one player
    void sendSpawnTo(EntityPlayerMP* player);

    // Broadcast a packet to all tracking players
    void broadcast(std::unique_ptr<Packet> pkt) const;

    // Broadcast to all tracking players AND to the entity itself if it's a player
    void broadcastIncludingSelf(std::unique_ptr<Packet> pkt) const;

    // Check/update which players should track this entry
    void updateTracking(const std::vector<EntityPlayerMP*>& allPlayers);

    // Send movement/look/held-item updates to all tracking players
    void sendUpdates();
};

class EntityTracker {
public:
    explicit EntityTracker(MinecraftServer* server);

    void addEntity(Entity* entity);
    void removeEntity(Entity* entity);

    // Called each server tick
    void tick();
    Entity* getEntityById(int entityId) const;

    // Broadcast a packet from an entity to all players tracking it
    void broadcastPacket(Entity* entity, std::unique_ptr<Packet> pkt);

    // Broadcast including the entity's own connection (e.g. arm animation)
    void broadcastPacketIncludingSelf(Entity* entity, std::unique_ptr<Packet> pkt);

    // When a new player logs in, send them all currently tracked entities
    void sendAllToPlayer(EntityPlayerMP* player);

private:
    MinecraftServer* mcServer_;
    std::unordered_map<int, std::unique_ptr<TrackerEntry>> entries_; // entityId -> entry
};
