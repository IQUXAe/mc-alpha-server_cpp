#pragma once

#include "../entity/Entity.h"
#include "../entity/EntityPlayerMP.h"
#include "../network/packets/AllPackets.h"
#include "core/ServerConstants.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <memory>
#include <cstdint>

class MinecraftServer;

// Tracks which entities are visible to which players
class EntityTracker {
public:
    explicit EntityTracker(MinecraftServer* server);
    ~EntityTracker() = default;

    // Add an entity to tracking
    void addEntity(Entity* entity);
    
    // Remove an entity from tracking (and notify players)
    void removeEntity(Entity* entity);

    // Called when an entity moves/changes state
    void updateEntity(Entity* entity);

    // Called each tick to sync entities with players
    void tick();

    // Start tracking an entity for a specific player
    void startTracking(Entity* entity, EntityPlayerMP* player);
    
    // Stop tracking an entity for a specific player
    void stopTracking(Entity* entity, EntityPlayerMP* player);

private:
    MinecraftServer* mcServer_;

    // Map: entityId -> list of players tracking this entity
    std::unordered_map<int, std::vector<EntityPlayerMP*>> trackedEntities_;

    // Map: entityId -> entity pointer
    std::unordered_map<int, Entity*> entities_;

    // Players currently tracking each entity (for dirty flag)
    std::unordered_map<int, std::unordered_set<EntityPlayerMP*>> trackingPlayers_;

    std::mutex trackerMutex_;

    // Check if a player should track an entity based on distance
    bool shouldTrack(Entity* entity, EntityPlayerMP* player) const;

    // Send spawn packet for entity
    void sendSpawnPacket(Entity* entity, EntityPlayerMP* player);
    
    // Send destroy packet for entity
    void sendDestroyPacket(int entityId, EntityPlayerMP* player);
    
    // Send movement/update packets
    void sendUpdatePackets(Entity* entity, EntityPlayerMP* player);
};
