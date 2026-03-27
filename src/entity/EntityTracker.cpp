#include "EntityTracker.h"
#include "EntityItem.h"
#include "EntityAnimals.h"
#include "EntityFallingSand.h"
#include "EntityLiving.h"
#include "../MinecraftServer.h"
#include <cmath>
#include <algorithm>

// ─── TrackerEntry helpers ────────────────────────────────────────────────────

std::unique_ptr<Packet> TrackerEntry::makeSpawnPacket() const {
    int fx = (int)(entity->posX * 32.0);
    int fy = (int)(entity->posY * 32.0);
    int fz = (int)(entity->posZ * 32.0);
    int yaw   = static_cast<int>(std::floor(entity->rotationYaw   * 256.0f / 360.0f)) & 0xFF;
    int pitch = static_cast<int>(std::floor(entity->rotationPitch * 256.0f / 360.0f)) & 0xFF;

    if (auto* p = dynamic_cast<EntityPlayerMP*>(entity)) {
        auto pkt = std::make_unique<Packet20NamedEntitySpawn>();
        pkt->entityId    = entity->entityId;
        pkt->name        = p->username;
        pkt->x           = fx;
        pkt->y           = fy;
        pkt->z           = fz;
        pkt->rotation    = (int8_t)(yaw   & 0xFF);
        pkt->pitch       = (int8_t)(pitch & 0xFF);
        pkt->currentItem = (int16_t)lastHeldItemId;
        return pkt;
    }

    if (auto* item = dynamic_cast<EntityItem*>(entity)) {
        auto pkt = std::make_unique<Packet21PickupSpawn>();
        pkt->entityId = entity->entityId;
        pkt->itemId   = (int16_t)item->itemID;
        pkt->count    = (int8_t)item->count;
        pkt->x        = fx;
        pkt->y        = fy;
        pkt->z        = fz;
        pkt->rotation = 0;
        pkt->pitch    = 0;
        pkt->roll     = 0;
        return pkt;
    }

    if (auto* living = dynamic_cast<EntityLiving*>(entity)) {
        auto pkt = std::make_unique<Packet24MobSpawn>();
        pkt->entityId = entity->entityId;
        pkt->type     = static_cast<int8_t>(living->getMobTypeId());
        pkt->x        = fx;
        pkt->y        = fy;
        pkt->z        = fz;
        pkt->yaw      = static_cast<int8_t>(yaw & 0xFF);
        pkt->pitch    = static_cast<int8_t>(pitch & 0xFF);
        return pkt;
    }

    // Fallback: generic pig spawn so the client stays stable for unknown entities.
    auto pkt = std::make_unique<Packet24MobSpawn>();
    pkt->entityId = entity->entityId;
    pkt->type     = 90; // pig
    pkt->x        = fx;
    pkt->y        = fy;
    pkt->z        = fz;
    pkt->yaw      = (int8_t)(yaw   & 0xFF);
    pkt->pitch    = (int8_t)(pitch & 0xFF);
    return pkt;
}

void TrackerEntry::sendSpawnTo(EntityPlayerMP* player) {
    if (!player || !player->netHandler) return;
    auto spawnPkt = makeSpawnPacket();
    if (spawnPkt) player->netHandler->sendPacket(std::move(spawnPkt));

    // Send velocity for items/projectiles
    if (sendVelocity && (entity->motionX != 0 || entity->motionY != 0 || entity->motionZ != 0)) {
        player->netHandler->sendPacket(std::make_unique<Packet28EntityVelocity>(
            entity->entityId, entity->motionX, entity->motionY, entity->motionZ));
    }

    // If already sneaking, tell the new observer
    if (auto* living = dynamic_cast<EntityLiving*>(entity)) {
        if (living->isSneaking)
            player->netHandler->sendPacket(
                std::make_unique<Packet18ArmAnimation>(entity->entityId, 104));
    }

    // Sync lastFixed* to what we just sent so sendUpdates doesn't
    // immediately send a stale relative-move or look correction
    lastFixedX   = (int)(entity->posX * 32.0);
    lastFixedY   = (int)(entity->posY * 32.0);
    lastFixedZ   = (int)(entity->posZ * 32.0);
    lastYawByte  = static_cast<int8_t>(static_cast<int>(std::floor(entity->rotationYaw   * 256.0f / 360.0f)) & 0xFF);
    lastPitchByte = static_cast<int8_t>(static_cast<int>(std::floor(entity->rotationPitch * 256.0f / 360.0f)) & 0xFF);
}

void TrackerEntry::broadcast(std::unique_ptr<Packet> pkt) const {
    for (auto* p : trackingPlayers) {
        if (p && p->netHandler) p->netHandler->sendPacket(pkt->clone());
    }
}

void TrackerEntry::broadcastIncludingSelf(std::unique_ptr<Packet> pkt) const {
    broadcast(pkt->clone());
    if (auto* p = dynamic_cast<EntityPlayerMP*>(entity)) {
        if (p->netHandler) p->netHandler->sendPacket(std::move(pkt));
    }
}

void TrackerEntry::updateTracking(const std::vector<EntityPlayerMP*>& allPlayers) {
    for (auto* player : allPlayers) {
        if (!player) continue;

        double dx = player->posX - (double)(lastFixedX / 32);
        double dz = player->posZ - (double)(lastFixedZ / 32);
        bool inRange = dx >= -trackingRange && dx <= trackingRange
                    && dz >= -trackingRange && dz <= trackingRange;

        bool alreadyTracking = trackingPlayers.count(player) > 0;

        if (inRange && !alreadyTracking && player != entity) {
            trackingPlayers.insert(player);
            sendSpawnTo(player);
        } else if (!inRange && alreadyTracking) {
            trackingPlayers.erase(player);
            player->netHandler->sendPacket(
                std::make_unique<Packet29DestroyEntity>(entity->entityId));
        }
    }
}

void TrackerEntry::sendUpdates() {
    if (tickCounter++ % updateRate != 0) return;

    int fx = (int)(entity->posX * 32.0);
    int fy = (int)(entity->posY * 32.0);
    int fz = (int)(entity->posZ * 32.0);
    int8_t yaw   = static_cast<int8_t>(static_cast<int>(std::floor(entity->rotationYaw   * 256.0f / 360.0f)) & 0xFF);
    int8_t pitch = static_cast<int8_t>(static_cast<int>(std::floor(entity->rotationPitch * 256.0f / 360.0f)) & 0xFF);

    int dx = fx - lastFixedX;
    int dy = fy - lastFixedY;
    int dz = fz - lastFixedZ;
    bool moved  = dx != 0 || dy != 0 || dz != 0;
    bool turned = yaw != lastYawByte || pitch != lastPitchByte;

    // Send velocity update if changed (Java: Packet28, threshold 0.02)
    if (sendVelocity) {
        double dvx = entity->motionX - lastMotionX;
        double dvy = entity->motionY - lastMotionY;
        double dvz = entity->motionZ - lastMotionZ;
        double dvSq = dvx*dvx + dvy*dvy + dvz*dvz;
        bool velStopped = (entity->motionX == 0.0 && entity->motionY == 0.0 && entity->motionZ == 0.0
                           && (lastMotionX != 0.0 || lastMotionY != 0.0 || lastMotionZ != 0.0));
        if (dvSq > 0.02*0.02 || velStopped) {
            lastMotionX = entity->motionX;
            lastMotionY = entity->motionY;
            lastMotionZ = entity->motionZ;
            broadcast(std::make_unique<Packet28EntityVelocity>(
                entity->entityId, entity->motionX, entity->motionY, entity->motionZ));
        }
    }

    std::unique_ptr<Packet> movePkt;
    if (dx >= -128 && dx < 128 && dy >= -128 && dy < 128 && dz >= -128 && dz < 128) {
        if (moved && turned)
            movePkt = std::make_unique<Packet33RelEntityMoveLook>(
                entity->entityId, (int8_t)dx, (int8_t)dy, (int8_t)dz,
                (int8_t)yaw, (int8_t)pitch);
        else if (moved)
            movePkt = std::make_unique<Packet31RelEntityMove>(
                entity->entityId, (int8_t)dx, (int8_t)dy, (int8_t)dz);
        else if (turned)
            movePkt = std::make_unique<Packet32EntityLook>(
                entity->entityId, (int8_t)yaw, (int8_t)pitch);
        else
            movePkt = std::make_unique<Packet30Entity>(entity->entityId);
    } else {
        movePkt = std::make_unique<Packet34EntityTeleport>(
            entity->entityId, fx, fy, fz, (int8_t)yaw, (int8_t)pitch);
    }

    broadcast(std::move(movePkt));

    if (moved || turned) {
        lastFixedX = fx; lastFixedY = fy; lastFixedZ = fz;
        lastYawByte = yaw; lastPitchByte = pitch;
    }

    // Held item update for players (Packet16)
    if (auto* mp = dynamic_cast<EntityPlayerMP*>(entity)) {
        int heldId = mp->netHandler ? mp->netHandler->getHeldItemId() : 0;
        if (heldId != lastHeldItemId) {
            lastHeldItemId = heldId;
            auto pkt = std::make_unique<Packet16BlockItemSwitch>();
            pkt->entityId = entity->entityId;
            pkt->itemId   = (int16_t)heldId;
            broadcast(std::move(pkt));
        }
    }

    // Sneak state update
    if (auto* living = dynamic_cast<EntityLiving*>(entity)) {
        if (living->isSneaking != lastSneaking) {
            lastSneaking = living->isSneaking;
            broadcast(std::make_unique<Packet18ArmAnimation>(
                entity->entityId, lastSneaking ? 104 : 105));
        }
    }
}

// ─── EntityTracker ───────────────────────────────────────────────────────────

EntityTracker::EntityTracker(MinecraftServer* server) : mcServer_(server) {}

void EntityTracker::addEntity(Entity* entity) {
    if (!entity || entries_.count(entity->entityId)) return;

    int range, rate;
    bool vel = false;

    if (dynamic_cast<EntityPlayerMP*>(entity)) {
        range = 512; rate = 1;
    } else if (dynamic_cast<EntityItem*>(entity)) {
        range = 64; rate = 20; vel = true;
    } else if (auto* living = dynamic_cast<EntityLiving*>(entity); living && living->getMobTypeId() != 0) {
        range = living->getTrackingRange();
        rate = living->getTrackingRate();
        vel = living->shouldSendVelocity();
    } else {
        // EntityFallingSand and other non-player, non-item entities:
        // don't track them (Alpha client doesn't render them server-side)
        return;
    }

    auto entry = std::make_unique<TrackerEntry>(entity, range, rate, vel);

    // If it's a player, send them all existing entities
    if (auto* newPlayer = dynamic_cast<EntityPlayerMP*>(entity)) {
        for (auto& [id, e] : entries_) {
            if (e->entity != entity) {
                e->sendSpawnTo(newPlayer);
                e->trackingPlayers.insert(newPlayer);
            }
        }
    }

    // Send this new entity to all players already in range
    const auto& players = mcServer_->configManager->playerEntities;
    entry->updateTracking(players);

    entries_[entity->entityId] = std::move(entry);
}

void EntityTracker::removeEntity(Entity* entity) {
    if (!entity) return;
    auto it = entries_.find(entity->entityId);
    if (it == entries_.end()) return;

    // If it's a player, remove them from all other entries' tracking sets
    if (auto* leavingPlayer = dynamic_cast<EntityPlayerMP*>(entity)) {
        for (auto& [id, e] : entries_) {
            if (e->trackingPlayers.erase(leavingPlayer)) {
                // No need to send Packet29 to a disconnecting player
            }
        }
    }

    // Notify all players tracking this entity that it's gone
    it->second->broadcast(
        std::make_unique<Packet29DestroyEntity>(entity->entityId));

    entries_.erase(it);
}

void EntityTracker::tick() {
    if (!mcServer_ || !mcServer_->configManager) return;
    const auto& players = mcServer_->configManager->playerEntities;

    for (auto& [id, entry] : entries_) {
        if (!entry->entity || entry->entity->isDead) continue;
        entry->updateTracking(players);
        entry->sendUpdates();
    }
}

void EntityTracker::broadcastPacket(Entity* entity, std::unique_ptr<Packet> pkt) {
    auto it = entries_.find(entity->entityId);
    if (it != entries_.end()) it->second->broadcast(std::move(pkt));
}

void EntityTracker::broadcastPacketIncludingSelf(Entity* entity, std::unique_ptr<Packet> pkt) {
    auto it = entries_.find(entity->entityId);
    if (it != entries_.end()) it->second->broadcastIncludingSelf(std::move(pkt));
}

void EntityTracker::sendAllToPlayer(EntityPlayerMP* player) {
    for (auto& [id, entry] : entries_) {
        if (entry->entity != player) {
            entry->sendSpawnTo(player);
            entry->trackingPlayers.insert(player);
        }
    }
}
