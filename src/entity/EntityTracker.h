#pragma once

#include "../entity/Entity.h"
#include "../entity/EntityArrow.h"
#include "../entity/EntityPlayerMP.h"
#include "../network/packets/AllPackets.h"

#include <unordered_map>
#include <unordered_set>
#include <memory>

class MinecraftServer;
class World;

// Mirrors Java's EntityTrackerEntry
struct TrackerEntry {
    int32_t entityId;
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
    int16_t lastHealth = -1;

    int tickCounter = 0;

    // Players currently receiving updates for this entity
    std::unordered_set<EntityPlayerMP*> trackingPlayers;

    TrackerEntry(Entity* e, int range, int rate, bool vel);

    Entity* resolve(World* world) const { return world ? world->getEntityById(entityId) : nullptr; }

    // Build the initial spawn packet for this entity
    std::unique_ptr<Packet> makeSpawnPacket(const Entity* entity) const;

    // Send this entry's spawn packet + initial state to one player
    void sendSpawnTo(EntityPlayerMP* player, const Entity* entity);

    // Broadcast a packet to all tracking players
    void broadcast(std::unique_ptr<Packet> pkt) const;

    // Broadcast to all tracking players AND to the entity itself if it's a player
    void broadcastIncludingSelf(const Entity* entity, std::unique_ptr<Packet> pkt) const;

    // Check/update which players should track this entry
    void updateTracking(const Entity* entity, const std::vector<EntityPlayerMP*>& allPlayers);

    // Send movement/look/held-item updates to all tracking players
    void sendUpdates(const Entity* entity);
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
