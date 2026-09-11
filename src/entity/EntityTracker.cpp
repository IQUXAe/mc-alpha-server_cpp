#include "EntityTracker.h"
#include "EntityArrow.h"
#include "EntityBoat.h"
#include "EntityItem.h"
#include "EntityAnimals.h"
#include "EntityFallingSand.h"
#include "EntityLiving.h"
#include "../core/RustBridge.h"
#include "../MinecraftServer.h"
#include <cmath>
#include <algorithm>

namespace {

bool observerHasEntityChunkLoaded(Entity* entity, EntityPlayerMP* observer) {
    if (!entity || !observer || !observer->netHandler) {
        return false;
    }

    const int chunkX = static_cast<int>(std::floor(entity->posX)) >> 4;
    const int chunkZ = static_cast<int>(std::floor(entity->posZ)) >> 4;
    return observer->netHandler->hasChunkLoaded(NetServerHandler::chunkKey(chunkX, chunkZ));
}

}

// ─── TrackerEntry ────────────────────────────────────────────────────────────

TrackerEntry::TrackerEntry(Entity* e, int range, int rate, bool vel)
    : entityId(e->entityId), trackingRange(range), updateRate(rate), sendVelocity(vel) {
    // Fixed-point/angle encoding owned by Rust (tracker_math.rs).
    lastFixedX = RustBridge::trackerEncodePos(e->posX);
    lastFixedY = RustBridge::trackerEncodePos(e->posY);
    lastFixedZ = RustBridge::trackerEncodePos(e->posZ);
    lastYawByte   = RustBridge::trackerEncodeRot(e->rotationYaw);
    lastPitchByte = RustBridge::trackerEncodeRot(e->rotationPitch);
    lastMountedEntityId = e->getRidingEntity() ? e->getRidingEntity()->entityId : -1;
    if (auto* living = dynamic_cast<EntityLiving*>(e)) {
        lastHealth = living->health;
    }
}

std::optional<RustPacket> TrackerEntry::makeSpawnPacket(const Entity* entity) const {
    int fx = RustBridge::trackerEncodePos(entity->posX);
    int fy = RustBridge::trackerEncodePos(entity->posY);
    int fz = RustBridge::trackerEncodePos(entity->posZ);
    int8_t yaw   = RustBridge::trackerEncodeRot(entity->rotationYaw);
    int8_t pitch = RustBridge::trackerEncodeRot(entity->rotationPitch);

    if (auto* p = dynamic_cast<const EntityPlayerMP*>(entity)) {
        return RustPackets::namedEntitySpawn(
            entity->entityId,
            p->username,
            fx, fy, fz,
            yaw,
            pitch,
            lastHeldItemId
        );
    }

    if (auto* item = dynamic_cast<const EntityItem*>(entity)) {
        return RustPackets::pickupSpawn(
            entity->entityId,
            item->itemID,
            item->count,
            fx, fy, fz,
            0, 0, 0
        );
    }

    if (auto* arrow = dynamic_cast<const EntityArrow*>(entity)) {
        return RustPackets::vehicleSpawn(arrow->entityId, 60, fx, fy, fz);
    }

    if (auto* boat = dynamic_cast<const EntityBoat*>(entity)) {
        return RustPackets::vehicleSpawn(boat->entityId, 1, fx, fy, fz);
    }

    if (auto* living = dynamic_cast<const EntityLiving*>(entity)) {
        return RustPackets::mobSpawn(
            entity->entityId,
            static_cast<uint8_t>(living->getMobTypeId()),
            fx, fy, fz,
            yaw,
            pitch
        );
    }

    // Fallback: generic pig spawn so the client stays stable for unknown entities.
    return RustPackets::mobSpawn(
        entity->entityId,
        90, // pig
        fx, fy, fz,
        yaw,
        pitch
    );
}

void TrackerEntry::sendSpawnTo(EntityPlayerMP* player, const Entity* entity) {
    if (!player || !player->netHandler) return;
    auto spawnPkt = makeSpawnPacket(entity);
    if (spawnPkt) player->netHandler->sendPacket(*spawnPkt);
    if (Entity* vehicle = entity->getRidingEntity()) {
        player->netHandler->sendPacket(RustPackets::attachEntity(entity->entityId, vehicle->entityId));
    }

    // Send velocity for items/projectiles
    if (sendVelocity && (entity->motionX != 0 || entity->motionY != 0 || entity->motionZ != 0)) {
        player->netHandler->sendPacket(RustPackets::velocity(
            entity->entityId, entity->motionX, entity->motionY, entity->motionZ));
    }

    // If already sneaking, tell the new observer
    if (auto* living = dynamic_cast<const EntityLiving*>(entity)) {
        if (living->isSneaking)
            player->netHandler->sendPacket(
                RustPackets::armAnimation(entity->entityId, 104));
        if (living->fire > 0)
            player->netHandler->sendPacket(
                RustPackets::armAnimation(entity->entityId, 102));
        lastHealth = living->health;
    }

    // Sync lastFixed* to what we just sent so sendUpdates doesn't
    // immediately send a stale relative-move or look correction
    lastFixedX   = RustBridge::trackerEncodePos(entity->posX);
    lastFixedY   = RustBridge::trackerEncodePos(entity->posY);
    lastFixedZ   = RustBridge::trackerEncodePos(entity->posZ);
    lastYawByte  = RustBridge::trackerEncodeRot(entity->rotationYaw);
    lastPitchByte = RustBridge::trackerEncodeRot(entity->rotationPitch);
}

void TrackerEntry::broadcast(const RustPacket& pkt) const {
    for (auto* p : trackingPlayers) {
        if (p && p->netHandler) p->netHandler->sendPacket(pkt);
    }
}

void TrackerEntry::broadcastIncludingSelf(const Entity* entity, const RustPacket& pkt) const {
    broadcast(pkt);
    if (auto* p = dynamic_cast<const EntityPlayerMP*>(entity)) {
        if (p->netHandler) p->netHandler->sendPacket(pkt);
    }
}

void TrackerEntry::updateTracking(const Entity* entity, const std::vector<EntityPlayerMP*>& allPlayers) {
    for (auto* player : allPlayers) {
        if (!player || !player->netHandler || player->isDead) continue;

        // Range math owned by Rust (tracker_math.rs); Y intentionally ignored like vanilla.
        bool inRange = RustBridge::trackerInRange(
            player->posX, player->posZ, lastFixedX, lastFixedZ, trackingRange);
        bool chunkLoaded = observerHasEntityChunkLoaded(const_cast<Entity*>(entity), player);

        bool alreadyTracking = trackingPlayers.count(player) > 0;

        if (inRange && chunkLoaded && !alreadyTracking && player != entity) {
            trackingPlayers.insert(player);
            sendSpawnTo(player, entity);
        } else if ((!inRange || !chunkLoaded) && alreadyTracking) {
            trackingPlayers.erase(player);
            player->netHandler->sendPacket(
                RustPackets::destroyEntity(entity->entityId));
        }
    }
}

void TrackerEntry::sendUpdates(const Entity* entity) {
    if (tickCounter++ % updateRate != 0) return;

    int fx = RustBridge::trackerEncodePos(entity->posX);
    int fy = RustBridge::trackerEncodePos(entity->posY);
    int fz = RustBridge::trackerEncodePos(entity->posZ);
    int8_t yaw   = RustBridge::trackerEncodeRot(entity->rotationYaw);
    int8_t pitch = RustBridge::trackerEncodeRot(entity->rotationPitch);

    int dx = fx - lastFixedX;
    int dy = fy - lastFixedY;
    int dz = fz - lastFixedZ;
    bool moved  = dx != 0 || dy != 0 || dz != 0;
    bool turned = yaw != lastYawByte || pitch != lastPitchByte;

    // Velocity-dirty check owned by Rust (threshold 0.02 + stop packet).
    if (RustBridge::trackerVelocityChanged(
            entity->motionX, entity->motionY, entity->motionZ,
            lastMotionX, lastMotionY, lastMotionZ, sendVelocity)) {
        lastMotionX = entity->motionX;
        lastMotionY = entity->motionY;
        lastMotionZ = entity->motionZ;
        broadcast(RustPackets::velocity(
            entity->entityId, entity->motionX, entity->motionY, entity->motionZ));
    }

    // Packet-kind selection owned by Rust: 0=entity, 1=move, 2=look, 3=move+look, 4=teleport.
    RustPacket movePkt;
    switch (RustBridge::trackerMoveKind(dx, dy, dz, moved, turned)) {
        case 3:
            movePkt = RustPackets::relEntityMoveLook(
                entity->entityId, (int8_t)dx, (int8_t)dy, (int8_t)dz, yaw, pitch);
            break;
        case 1:
            movePkt = RustPackets::relEntityMove(
                entity->entityId, (int8_t)dx, (int8_t)dy, (int8_t)dz);
            break;
        case 2:
            movePkt = RustPackets::entityLook(entity->entityId, yaw, pitch);
            break;
        case 4:
            movePkt = RustPackets::entityTeleport(entity->entityId, fx, fy, fz, yaw, pitch);
            break;
        case 0:
        default:
            movePkt = RustPackets::entity(entity->entityId);
            break;
    }

    broadcast(movePkt);

    if (moved || turned) {
        lastFixedX = fx; lastFixedY = fy; lastFixedZ = fz;
        lastYawByte = yaw; lastPitchByte = pitch;
    }

    // Held item update for players (Packet16)
    if (auto* mp = dynamic_cast<const EntityPlayerMP*>(entity)) {
        int heldId = mp->netHandler ? mp->netHandler->getHeldItemId() : 0;
        if (heldId != lastHeldItemId) {
            lastHeldItemId = heldId;
            broadcast(RustPackets::blockItemSwitch(entity->entityId, heldId));
        }
    }

    // Sneak state update
    if (auto* living = dynamic_cast<const EntityLiving*>(entity)) {
        if (lastHealth >= 0 && living->health != lastHealth) {
            if (living->health < lastHealth && living->health > 0) {
                broadcast(RustPackets::entityStatus(entity->entityId, 2));
            }
            lastHealth = living->health;
        }

        if (living->isSneaking != lastSneaking) {
            lastSneaking = living->isSneaking;
            broadcastIncludingSelf(entity, RustPackets::armAnimation(
                entity->entityId, lastSneaking ? 104 : 105));
        }
        const bool isBurning = living->fire > 0;
        if (isBurning != lastBurning) {
            lastBurning = isBurning;
            broadcastIncludingSelf(entity, RustPackets::armAnimation(
                entity->entityId, isBurning ? 102 : 103));
        }
    }

    const int32_t mountedEntityId = entity->getRidingEntity() ? entity->getRidingEntity()->entityId : -1;
    if (mountedEntityId != lastMountedEntityId) {
        lastMountedEntityId = mountedEntityId;
        broadcastIncludingSelf(entity, RustPackets::attachEntity(entity->entityId, mountedEntityId));
    }
}

// ─── EntityTracker ───────────────────────────────────────────────────────────

EntityTracker::EntityTracker(MinecraftServer* server) : mcServer_(server) {}

Entity* EntityTracker::getEntityById(int entityId) const {
    auto it = entries_.find(entityId);
    if (it == entries_.end() || !it->second) {
        return nullptr;
    }
    return it->second->resolve(mcServer_->worldMngr.get());
}

void EntityTracker::addEntity(Entity* entity) {
    if (!entity || entries_.count(entity->entityId)) return;

    int range, rate;
    bool vel = false;

    if (dynamic_cast<EntityPlayerMP*>(entity)) {
        range = 512; rate = 1;
    } else if (dynamic_cast<EntityItem*>(entity)) {
        range = 64; rate = 20; vel = true;
    } else if (auto* arrow = dynamic_cast<EntityArrow*>(entity)) {
        range = arrow->getTrackingRange();
        rate = arrow->getTrackingRate();
        vel = arrow->shouldSendVelocity();
    } else if (dynamic_cast<EntityBoat*>(entity)) {
        range = 160;
        rate = 5;
        vel = true;
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
    World* world = mcServer_->worldMngr.get();

    if (auto* newPlayer = dynamic_cast<EntityPlayerMP*>(entity)) {
        const std::vector<EntityPlayerMP*> onlyNewPlayer{newPlayer};
        for (auto& [id, e] : entries_) {
            if (e->entityId != entity->entityId) {
                if (Entity* tracked = e->resolve(world)) {
                    e->updateTracking(tracked, onlyNewPlayer);
                }
            }
        }
    }

    // Send this new entity to all players already in range
    const auto& players = mcServer_->configManager->playerEntities;
    entry->updateTracking(entity, players);

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
    World* world = mcServer_->worldMngr.get();
    if (Entity* tracked = it->second->resolve(world)) {
        it->second->broadcast(
            RustPackets::destroyEntity(tracked->entityId));
    }

    entries_.erase(it);
}

void EntityTracker::tick() {
    if (!mcServer_ || !mcServer_->configManager) return;
    const auto& players = mcServer_->configManager->playerEntities;
    World* world = mcServer_->worldMngr.get();

    for (auto it = entries_.begin(); it != entries_.end(); ) {
        Entity* entity = it->second->resolve(world);
        if (!entity || entity->isDead) {
            it = entries_.erase(it);
            continue;
        }

        // Remove stale player pointers from tracking sets
        for (auto pit = it->second->trackingPlayers.begin(); pit != it->second->trackingPlayers.end(); ) {
            if (!*pit || (*pit)->isDead) {
                pit = it->second->trackingPlayers.erase(pit);
            } else {
                ++pit;
            }
        }

        it->second->updateTracking(entity, players);
        it->second->sendUpdates(entity);
        ++it;
    }
}

void EntityTracker::broadcastPacket(Entity* entity, const RustPacket& pkt) {
    auto it = entries_.find(entity->entityId);
    if (it != entries_.end()) it->second->broadcast(pkt);
}

void EntityTracker::broadcastPacketIncludingSelf(Entity* entity, const RustPacket& pkt) {
    auto it = entries_.find(entity->entityId);
    if (it != entries_.end()) it->second->broadcastIncludingSelf(entity, pkt);
}

void EntityTracker::sendAllToPlayer(EntityPlayerMP* player) {
    if (!player || !player->netHandler || player->isDead) {
        return;
    }

    World* world = mcServer_->worldMngr.get();
    const std::vector<EntityPlayerMP*> onlyPlayer{player};
    for (auto& [id, entry] : entries_) {
        if (Entity* entity = entry->resolve(world); entity && entity != player) {
            entry->updateTracking(entity, onlyPlayer);
        }
    }
}
