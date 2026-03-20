#include "EntityTracker.h"
#include "../MinecraftServer.h"
#include "../core/Logger.h"
#include <cmath>
#include <algorithm>

EntityTracker::EntityTracker(MinecraftServer* server)
    : mcServer_(server) {
}

void EntityTracker::addEntity(Entity* entity) {
    if (!entity) return;
    
    std::lock_guard<std::mutex> lock(trackerMutex_);
    entities_[entity->entityId] = entity;
    Logger::debug("EntityTracker: Added entity {} at ({}, {}, {})", 
                  entity->entityId, entity->posX, entity->posY, entity->posZ);
}

void EntityTracker::removeEntity(Entity* entity) {
    if (!entity) return;
    
    std::lock_guard<std::mutex> lock(trackerMutex_);
    
    auto it = entities_.find(entity->entityId);
    if (it != entities_.end()) {
        // Notify all tracking players
        auto trackIt = trackedEntities_.find(entity->entityId);
        if (trackIt != trackedEntities_.end()) {
            for (EntityPlayerMP* player : trackIt->second) {
                if (player && player->netHandler) {
                    player->sendPacket(std::make_unique<Packet29DestroyEntity>(entity->entityId));
                }
            }
            trackedEntities_.erase(trackIt);
        }
        entities_.erase(it);
        Logger::debug("EntityTracker: Removed entity {}", entity->entityId);
    }
}

void EntityTracker::updateEntity(Entity* entity) {
    // Mark entity as needing update on next tick
    // Actual sending happens in tick() to batch updates
}

void EntityTracker::tick() {
    if (!mcServer_ || !mcServer_->configManager) return;
    
    std::lock_guard<std::mutex> lock(trackerMutex_);
    
    // Get all players
    const auto& players = mcServer_->configManager->playerEntities;
    
    // For each entity, check which players should track it
    for (auto& [entityId, entity] : entities_) {
        if (!entity || entity->isDead) continue;
        
        std::unordered_set<EntityPlayerMP*> shouldTrackPlayers;
        
        // Find all players that should track this entity
        for (EntityPlayerMP* player : players) {
            if (!player || player->isDead) continue;
            
            if (shouldTrack(entity, player)) {
                shouldTrackPlayers.insert(player);
            }
        }
        
        // Get current tracking players
        auto& currentPlayers = trackingPlayers_[entityId];
        
        // Find players to add (newly in range)
        std::vector<EntityPlayerMP*> toAdd;
        for (EntityPlayerMP* player : shouldTrackPlayers) {
            if (currentPlayers.find(player) == currentPlayers.end()) {
                toAdd.push_back(player);
            }
        }
        
        // Find players to remove (no longer in range)
        std::vector<EntityPlayerMP*> toRemove;
        for (EntityPlayerMP* player : currentPlayers) {
            if (shouldTrackPlayers.find(player) == shouldTrackPlayers.end()) {
                toRemove.push_back(player);
            }
        }
        
        // Add new trackers
        for (EntityPlayerMP* player : toAdd) {
            startTracking(entity, player);
            currentPlayers.insert(player);
        }
        
        // Remove old trackers
        for (EntityPlayerMP* player : toRemove) {
            stopTracking(entity, player);
            currentPlayers.erase(player);
        }
        
        // Send updates to all tracking players
        for (EntityPlayerMP* player : currentPlayers) {
            if (player && player->netHandler) {
                sendUpdatePackets(entity, player);
            }
        }
    }
    
    // Clean up dead entities from tracking
    for (auto it = trackingPlayers_.begin(); it != trackingPlayers_.end(); ) {
        if (entities_.find(it->first) == entities_.end()) {
            it = trackingPlayers_.erase(it);
        } else {
            ++it;
        }
    }
}

void EntityTracker::startTracking(Entity* entity, EntityPlayerMP* player) {
    if (!entity || !player || !player->netHandler) return;
    
    Logger::debug("EntityTracker: Player {} now tracking entity {}", 
                  player->username, entity->entityId);
    
    // Send spawn packet
    sendSpawnPacket(entity, player);
    
    // Add to tracked list
    trackedEntities_[entity->entityId].push_back(player);
}

void EntityTracker::stopTracking(Entity* entity, EntityPlayerMP* player) {
    if (!entity || !player || !player->netHandler) return;
    
    Logger::debug("EntityTracker: Player {} stopped tracking entity {}", 
                  player->username, entity->entityId);
    
    // Send destroy packet
    sendDestroyPacket(entity->entityId, player);
    
    // Remove from tracked list
    auto it = trackedEntities_.find(entity->entityId);
    if (it != trackedEntities_.end()) {
        it->second.erase(std::remove(it->second.begin(), it->second.end(), player), 
                         it->second.end());
        if (it->second.empty()) {
            trackedEntities_.erase(it);
        }
    }
}

bool EntityTracker::shouldTrack(Entity* entity, EntityPlayerMP* player) const {
    if (!entity || !player) return false;
    
    double dx = entity->posX - player->posX;
    double dy = entity->posY - player->posY;
    double dz = entity->posZ - player->posZ;
    double distSq = dx*dx + dy*dy + dz*dz;
    
    return distSq <= (ServerConstants::TRACKING_DISTANCE * ServerConstants::TRACKING_DISTANCE);
}

void EntityTracker::sendSpawnPacket(Entity* entity, EntityPlayerMP* player) {
    if (!entity || !player || !player->netHandler) return;
    
    // Convert position to fixed-point (like Minecraft does)
    int fixedX = static_cast<int>(entity->posX * 32.0);
    int fixedY = static_cast<int>(entity->posY * 32.0);
    int fixedZ = static_cast<int>(entity->posZ * 32.0);
    int yaw = static_cast<int>(entity->rotationYaw * 256.0f / 360.0f);
    int pitch = static_cast<int>(entity->rotationPitch * 256.0f / 360.0f);
    
    // Different spawn packets for different entity types
    if (auto* playerEnt = dynamic_cast<EntityPlayer*>(entity)) {
        // Named entity spawn
        auto pkt = std::make_unique<Packet20NamedEntitySpawn>();
        pkt->entityId = entity->entityId;
        pkt->name = playerEnt->username;
        pkt->x = fixedX;
        pkt->y = fixedY;
        pkt->z = fixedZ;
        pkt->rotation = static_cast<int8_t>(yaw & 0xFF);
        pkt->pitch = static_cast<int8_t>(pitch & 0xFF);
        pkt->currentItem = 0;  // TODO: get held item
        player->sendPacket(std::move(pkt));
    } else {
        // Check if it's a mob
        int mobType = 0;  // TODO: determine mob type
        
        auto pkt = std::make_unique<Packet24MobSpawn>();
        pkt->entityId = entity->entityId;
        pkt->type = static_cast<int8_t>(mobType);
        pkt->x = fixedX;
        pkt->y = fixedY;
        pkt->z = fixedZ;
        pkt->yaw = static_cast<int8_t>(yaw & 0xFF);
        pkt->pitch = static_cast<int8_t>(pitch & 0xFF);
        player->sendPacket(std::move(pkt));
    }
    
    // Send entity velocity if moving
    if (entity->motionX != 0 || entity->motionY != 0 || entity->motionZ != 0) {
        auto velPkt = std::make_unique<Packet28EntityVelocity>(
            entity->entityId, entity->motionX, entity->motionY, entity->motionZ);
        player->sendPacket(std::move(velPkt));
    }
}

void EntityTracker::sendDestroyPacket(int entityId, EntityPlayerMP* player) {
    if (!player || !player->netHandler) return;
    player->sendPacket(std::make_unique<Packet29DestroyEntity>(entityId));
}

void EntityTracker::sendUpdatePackets(Entity* entity, EntityPlayerMP* player) {
    if (!entity || !player || !player->netHandler) return;

    // Check if entity moved significantly
    double dx = entity->posX - entity->lastX;
    double dy = entity->posY - entity->lastY;
    double dz = entity->posZ - entity->lastZ;
    double distSq = dx*dx + dy*dy + dz*dz;

    bool hasMovement = distSq > 0.001;
    bool hasRotation = std::abs(entity->rotationYaw - entity->lastYaw) > 1.0f ||
                       std::abs(entity->rotationPitch - entity->lastPitch) > 1.0f;

    // Convert rotation to byte format
    int yaw = static_cast<int>(entity->rotationYaw * 256.0f / 360.0f);
    int pitch = static_cast<int>(entity->rotationPitch * 256.0f / 360.0f);

    if (hasMovement && hasRotation) {
        // Relative move + look
        int8_t relX = static_cast<int8_t>(std::clamp(dx * 32.0, -128.0, 127.0));
        int8_t relY = static_cast<int8_t>(std::clamp(dy * 32.0, -128.0, 127.0));
        int8_t relZ = static_cast<int8_t>(std::clamp(dz * 32.0, -128.0, 127.0));
        int8_t yawByte = static_cast<int8_t>(yaw & 0xFF);
        int8_t pitchByte = static_cast<int8_t>(pitch & 0xFF);

        player->sendPacket(std::make_unique<Packet33RelEntityMoveLook>(
            entity->entityId, relX, relY, relZ, yawByte, pitchByte));
    } else if (hasMovement) {
        // Relative move only
        int8_t relX = static_cast<int8_t>(std::clamp(dx * 32.0, -128.0, 127.0));
        int8_t relY = static_cast<int8_t>(std::clamp(dy * 32.0, -128.0, 127.0));
        int8_t relZ = static_cast<int8_t>(std::clamp(dz * 32.0, -128.0, 127.0));

        player->sendPacket(std::make_unique<Packet31RelEntityMove>(
            entity->entityId, relX, relY, relZ));
    } else if (hasRotation) {
        // Look only
        int8_t yawByte = static_cast<int8_t>(yaw & 0xFF);
        int8_t pitchByte = static_cast<int8_t>(pitch & 0xFF);

        player->sendPacket(std::make_unique<Packet32EntityLook>(
            entity->entityId, yawByte, pitchByte));
    }

    // Send velocity if moving
    if (entity->motionX != 0 || entity->motionY != 0 || entity->motionZ != 0) {
        player->sendPacket(std::make_unique<Packet28EntityVelocity>(
            entity->entityId, entity->motionX, entity->motionY, entity->motionZ));
    }
}
