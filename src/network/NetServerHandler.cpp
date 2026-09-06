#include "../world/World.h"
#include "NetServerHandler.h"
#include "../MinecraftServer.h"
#include "../entity/EntityPlayerMP.h"
#include "../entity/EntityBoat.h"
#include "../entity/EntityTracker.h"
#include "../world/Chunk.h"
#include "../world/TileEntity.h"
#include "../world/TileEntityChest.h"
#include "../world/TileEntitySign.h"
#include "../block/Block.h"
#include "../entity/EntityItem.h"
#include "../core/Item.h"
#include "../core/Material.h"
#include "../core/NBT.h"
#include "../core/ByteBuffer.h"
#include "../core/Logger.h"
#include "../core/RustBridge.h"

#include <iomanip>
#include <sstream>
#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <functional>
#include <charconv>
namespace {

constexpr double kMaxAttackReach = 5.0;
constexpr double kMaxAttackReachSq = kMaxAttackReach * kMaxAttackReach;
constexpr double kSoftMovementRejectSq = 225.0;
constexpr double kHardMovementRejectSq = 900.0;

double clampToRange(double value, double minValue, double maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

double distanceSqToBoundingBox(const Vec3D& point, const AxisAlignedBB& box) {
    const double closestX = clampToRange(point.xCoord, box.minX, box.maxX);
    const double closestY = clampToRange(point.yCoord, box.minY, box.maxY);
    const double closestZ = clampToRange(point.zCoord, box.minZ, box.maxZ);
    return point.squareDistanceTo(closestX, closestY, closestZ);
}

bool rayHitsSolidBlock(World* world, const Vec3D& from, const Vec3D& to) {
    if (!world) {
        return false;
    }

    const int minX = MathHelper::floor_double(std::min(from.xCoord, to.xCoord));
    const int minY = MathHelper::floor_double(std::min(from.yCoord, to.yCoord));
    const int minZ = MathHelper::floor_double(std::min(from.zCoord, to.zCoord));
    const int maxX = MathHelper::floor_double(std::max(from.xCoord, to.xCoord));
    const int maxY = MathHelper::floor_double(std::max(from.yCoord, to.yCoord));
    const int maxZ = MathHelper::floor_double(std::max(from.zCoord, to.zCoord));
    const double targetDistSq = from.squareDistanceTo(to);

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                const int blockId = world->getBlockIdNoChunkLoad(x, y, z);
                if (blockId <= 0 || blockId >= 256) {
                    continue;
                }

                Block* block = Block::blocksList[blockId];
                if (!block) {
                    continue;
                }

                auto collisionBox = block->getCollisionBoundingBoxFromPool(world, x, y, z);
                if (!collisionBox) {
                    continue;
                }

                auto clip = collisionBox->clip(from, to);
                if (!clip) {
                    continue;
                }

                if (from.squareDistanceTo(clip->hitVec) + 1.0E-6 < targetDistSq) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool hasLineOfSight(const Entity& attacker, const Entity& target) {
    if (!attacker.worldObj) {
        return false;
    }

    const Vec3D eye(attacker.posX, attacker.posY + attacker.getEyeHeight(), attacker.posZ);
    const std::array<Vec3D, 3> targetSamples{
        Vec3D(target.posX, target.boundingBox.minY + 0.1, target.posZ),
        Vec3D(target.posX, target.posY + target.height * 0.5, target.posZ),
        Vec3D(target.posX, target.posY + target.getEyeHeight(), target.posZ),
    };

    for (const Vec3D& sample : targetSamples) {
        if (!rayHitsSolidBlock(attacker.worldObj, eye, sample)) {
            return true;
        }
    }

    return false;
}

void broadcastVelocityIfNeeded(MinecraftServer* server, Entity& entity,
                               double beforeX, double beforeY, double beforeZ) {
    if (!server || !server->entityTracker) {
        return;
    }

    const double dx = entity.motionX - beforeX;
    const double dy = entity.motionY - beforeY;
    const double dz = entity.motionZ - beforeZ;
    if (dx * dx + dy * dy + dz * dz <= 1.0E-6) {
        return;
    }

    server->entityTracker->broadcastPacketIncludingSelf(
        &entity, RustPackets::velocity(
            entity.entityId, entity.motionX, entity.motionY, entity.motionZ));
}

} // namespace

NetServerHandler::NetServerHandler(MinecraftServer* server, std::unique_ptr<NetworkManager> netMgr, EntityPlayerMP* player)
    : mcServer_(server), netManager_(std::move(netMgr)), player_(player) {
    netManager_->setNetHandler(this);
    player_->netHandler = this;
    lastX_ = player_->posX;
    lastY_ = player_->posY;
    lastZ_ = player_->posZ;
}

NetServerHandler::~NetServerHandler() {
    // Ensure player is removed from playerEntities before deletion to prevent
    // dangling pointers in ServerConfigurationManager::playerEntities.
    if (player_ && !disconnected) {
        if (mcServer_ && mcServer_->configManager) {
            mcServer_->configManager->playerLoggedOut(player_);
        }
    }
    delete player_;
}

void NetServerHandler::tick() {
    netManager_->processReadPackets();
    if (disconnected) return;
    if (++tickCounter_ % 20 == 0) {
        sendPacket(RustPackets::keepAlive());
    }

    int cx = static_cast<int>(std::floor(player_->posX)) >> 4;
    int cz = static_cast<int>(std::floor(player_->posZ)) >> 4;

    // Every time the player enters a new chunk, rebuild what needs loading
    if (lastChunkX_ != cx || lastChunkZ_ != cz) {
        lastChunkX_ = cx;
        lastChunkZ_ = cz;

        int r = mcServer_->getViewDistance();
        int genR = r + 3; // extra ring: +1 for population neighbors, +2 buffer

        // Unload chunks that are now outside the view distance.
        // The client will render garbage/stale data if we don't tell it to drop these.
        std::vector<int64_t> toRemove;
        for (int64_t key : sentChunks_) {
            int sx = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFF));
            int sz = static_cast<int>(static_cast<uint32_t>((key >> 32) & 0xFFFFFFFF));
            if (std::abs(sx - cx) > r || std::abs(sz - cz) > r) {
                toRemove.push_back(key);
            }
        }
        for (int64_t key : toRemove) {
            int sx = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFF));
            int sz = static_cast<int>(static_cast<uint32_t>((key >> 32) & 0xFFFFFFFF));
            sendPacket(RustPackets::preChunk(sx, sz, false));
            if (sentChunks_.erase(key) > 0 && mcServer_ && mcServer_->configManager) {
                mcServer_->configManager->removePlayerFromChunk(player_, key);
            }
        }

        // Build sorted list of chunks to load (nearest first), skip already sent
        std::vector<std::pair<int,int>> needed;
        std::vector<std::pair<int,int>> padding;
        for (int i = cx - genR; i <= cx + genR; ++i) {
            for (int j = cz - genR; j <= cz + genR; ++j) {
                bool isPadding = (std::abs(i - cx) > r || std::abs(j - cz) > r);
                if (isPadding) {
                    padding.push_back({i, j});
                    continue;
                }
                // Only queue visible chunks that haven't been sent yet
                if (!sentChunks_.contains(chunkKey(i, j))) {
                    needed.push_back({i, j});
                }
            }
        }

        auto distanceSq = [cx, cz](const std::pair<int,int>& p) {
            return (p.first - cx)*(p.first - cx) + (p.second - cz)*(p.second - cz);
        };
        std::ranges::sort(needed, std::less{}, distanceSq);
        std::ranges::sort(padding, std::less{}, distanceSq);

        for (const auto& c : needed) {
            mcServer_->worldMngr->requestChunkAsync(c.first, c.second, 0);
        }
        for (const auto& c : padding) {
            mcServer_->worldMngr->requestChunkAsync(c.first, c.second, 2);
        }

        // Prepend to queue (new position = higher priority), avoid duplicates with a quick erase
        // Simple approach: replace queue with newly sorted list merged with existing
        // Keep existing items that are still in view radius
        std::vector<std::pair<int,int>> merged;
        merged.reserve(needed.size() + chunksToLoad_.size());
        std::unordered_set<int64_t> queuedKeys;
        queuedKeys.reserve(needed.size() + chunksToLoad_.size());

        // Add new ones first (higher priority - closet to player)
        for (auto& c : needed) {
            queuedKeys.insert(chunkKey(c.first, c.second));
            merged.push_back(c);
        }
        // Re-add old queued items that weren't in needed and still within view
        for (auto& c : chunksToLoad_) {
            const auto key = chunkKey(c.first, c.second);
            if (!queuedKeys.contains(key) && !sentChunks_.contains(key)) {
                queuedKeys.insert(key);
                merged.push_back(c);
            }
        }
        chunksToLoad_ = std::move(merged);
    }

    // Process up to 15 chunks per tick: generate + send populated ones
    int sent = 0;
    for (auto it = chunksToLoad_.begin(); it != chunksToLoad_.end() && sent < 15; ) {
        int px = it->first;
        int pz = it->second;

        // Generate 3x3 grid around this chunk to ensure all cross-chunk decorations are complete
        // Population places objects with +8 offset which can affect neighboring chunks.
        // Requesting them asynchronously keeps the main network tick responsive.
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                mcServer_->worldMngr->requestChunkAsync(px + dx, pz + dz, 0);
            }
        }
        
        // Ensure all chunks in 3x3 grid are populated
        // ensureChunkPopulated() returns immediately if already populated
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                mcServer_->worldMngr->ensureChunkPopulated(px + dx, pz + dz);
            }
        }
        
        // Get the center chunk (no generation, just lookup)
        auto* chunk = mcServer_->worldMngr->getChunk(px, pz, false);

        if (chunk && chunk->isTerrainPopulated) {
            sendPacket(RustPackets::preChunk(px, pz, true));
            sendMapChunk(px * 16, 0, pz * 16, 16, 128, 16, chunk->getChunkData());

            // Debug: verify container blocks exist at TileEntity positions
            for (const auto& [key, te] : chunk->getTileEntities()) {
                if (!te) continue;
                int lx = te->xCoord - px * 16;
                int lz = te->zCoord - pz * 16;
                (void)chunk->getBlockID(lx, te->yCoord, lz);
            }
            for (const auto& [key, te] : chunk->getTileEntities()) {
                if (!te) continue;
                sendTileEntityPacket(te);
            }
            
            if (sentChunks_.insert(chunkKey(px, pz)).second && mcServer_ && mcServer_->configManager) {
                mcServer_->configManager->addPlayerToChunk(player_, chunkKey(px, pz));
            }
            it = chunksToLoad_.erase(it);
            ++sent;
        } else {
            ++it;
        }
    }

    // Re-check tracked entities after new chunks become visible to this player.
    // This avoids relog races where boat spawn packets are missed until much later.
    if (sent > 0 && mcServer_ && mcServer_->entityTracker) {
        mcServer_->entityTracker->sendAllToPlayer(player_);
    }
}

bool NetServerHandler::shouldBypassReadTimeout() const {
    // While player is on the death screen, clients may stop sending movement packets
    // for a long time; don't disconnect them for inactivity until they respawn/quit.
    return player_ && (player_->isDead || player_->health <= 0);
}

void NetServerHandler::handleRespawn() {
    if (player_->health > 0) {
        return;
    }

    auto* tracker = (mcServer_ && mcServer_->entityTracker) ? mcServer_->entityTracker.get() : nullptr;
    if (tracker) {
        // Remove the dead tracked player first so other clients destroy the corpse entity.
        tracker->removeEntity(player_);
    }

    player_->isDead = false;
    player_->health = player_->maxHealth;
    player_->hurtTime = 0;
    player_->deathTime = 0;
    player_->fire = 0;
    player_->air = 300;
    player_->fallDistance = 0.0f;
    player_->motionX = 0.0;
    player_->motionY = 0.0;
    player_->motionZ = 0.0;
    player_->resetCombatState();

    const double spawnX = mcServer_->worldMngr->spawnX + 0.5;
    const double spawnY = mcServer_->worldMngr->spawnY;
    const double spawnZ = mcServer_->worldMngr->spawnZ + 0.5;
    player_->setPositionAndRotation(spawnX, spawnY, spawnZ, 0.0f, 0.0f);

    if (tracker) {
        tracker->addEntity(player_);
    }

    sendPacket(RustPackets::respawn());
    sendPacket(RustPackets::updateHealth(player_->health));
    teleport(spawnX, spawnY, spawnZ, 0.0f, 0.0f);
    sendInventory();
}

void NetServerHandler::handleUseEntity(const RustPacket7UseEntity& pkt) {
    if (!mcServer_ || !mcServer_->entityTracker) {
        return;
    }

    if (pkt.player_entity_id != player_->entityId) {
        return;
    }

    syncHeldItemSelection();

    Entity* target = mcServer_->entityTracker->getEntityById(pkt.target_entity_id);
    if (!target || target == player_ || target->isDead || !target->canBeCollidedWith()) {
        return;
    }

    if (dynamic_cast<EntityPlayerMP*>(target) && !mcServer_->isPvpEnabled()) {
        return;
    }

    const Vec3D attackerEye(player_->posX, player_->posY + player_->getEyeHeight(), player_->posZ);
    const double reachSq = distanceSqToBoundingBox(attackerEye, target->boundingBox);
    if (reachSq > kMaxAttackReachSq) {
        return;
    }

    if (!hasLineOfSight(*player_, *target)) {
        return;
    }

    if (!pkt.is_left_click) {
        // Try entity interaction first (e.g. cow milking, pig saddling)
        if (target->interact(player_)) {
            return;
        }

        if (auto* boat = dynamic_cast<EntityBoat*>(target)) {
            if (Entity* boatRider = boat->getRiddenByEntity(); boatRider && boatRider != player_) {
                if (dynamic_cast<EntityPlayerMP*>(boatRider)) {
                    return;
                }
                boatRider->mountEntity(nullptr);
            }
            player_->mountEntity(player_->getRidingEntity() == boat ? nullptr : boat);
            if (Entity* myVehicle = player_->getRidingEntity()) {
                myVehicle->updateRiderPosition();
            }
        }
        return;
    }

    if (!player_->canAttackNow()) {
        return;
    }

    ItemStack* held = getSelectedItemStack();
    int damage = held ? std::max(1, held->getDamageVsEntity(target)) : 1;

    if (damage <= 0) {
        return;
    }

    const double oldMotionX = target->motionX;
    const double oldMotionY = target->motionY;
    const double oldMotionZ = target->motionZ;
    target->attackEntityFrom(player_, damage);
    player_->markAttackPerformed();
    broadcastVelocityIfNeeded(mcServer_, *target, oldMotionX, oldMotionY, oldMotionZ);

    if (held) {
        if (auto* living = dynamic_cast<EntityLiving*>(target)) {
            held->hitEntity(living);
            if (held->stackSize <= 0) {
                if (held == heldItem_.get()) {
                    heldItem_.reset();
                    heldItemId_ = 0;
                    player_->inventory.currentItem = 0;
                } else {
                    player_->destroyCurrentEquippedItem();
                }
                sendInventory();
            }
        }
    }
}


void NetServerHandler::kick(const std::string& reason) {
    if (disconnected) return;
    Logger::info("Disconnecting {}: {}", player_->username, reason);
    player_->savedHeldItemId = heldItemId_;
    sendPacket(RustPackets::kickDisconnect(reason));
    netManager_->serverShutdown();
    disconnected = true;

    mcServer_->configManager->broadcastPacket(
        RustPackets::chat("\u00a7e" + player_->username + " left the game."));
    mcServer_->configManager->playerLoggedOut(player_);
}

void NetServerHandler::sendPacket(const RustPacket& pkt) {
    netManager_->sendPacket(pkt);
}

void NetServerHandler::sendMapChunk(int x, int y, int z, int sizeX, int sizeY, int sizeZ, const std::vector<uint8_t>& compressedData) {
    ByteBuffer buf;
    buf.writeUByte(51);
    buf.writeInt(x);
    buf.writeShort(static_cast<int16_t>(y));
    buf.writeInt(z);
    buf.writeByte(static_cast<int8_t>(sizeX - 1));
    buf.writeByte(static_cast<int8_t>(sizeY - 1));
    buf.writeByte(static_cast<int8_t>(sizeZ - 1));
    buf.writeInt(static_cast<int32_t>(compressedData.size()));
    buf.writeBytes(compressedData);
    netManager_->sendRaw(buf.data.data(), buf.data.size(), true);
}

void NetServerHandler::sendTileEntityPacket(TileEntity* te) {
    if (!te) return;
    
    NBTCompound nbt;
    te->writeToNBT(nbt);
    ByteBuffer rawBuf;
    nbt.writeRoot(rawBuf, "");
    std::vector<uint8_t> compressed = RustBridge::gzipCompress(rawBuf.data);
    if (compressed.empty()) return;

    sendPacket(RustPackets::complexEntity(te->xCoord, static_cast<int16_t>(te->yCoord), te->zCoord, compressed.data(), compressed.size()));
}

void NetServerHandler::teleport(double x, double y, double z, float yaw, float pitch) {
    lastX_ = x;
    lastY_ = y;
    lastZ_ = z;
    hasMoved_ = false;
    player_->setPosition(x, y, z);
    player_->rotationYaw = yaw;
    player_->rotationPitch = pitch;
    sendPacket(RustPackets::playerLookMove(x, y + 1.6200000047683716, y, z, yaw, pitch, false));
}

void NetServerHandler::sendInventory() {
    auto sendSection = [this](int type, const std::vector<std::unique_ptr<ItemStack>>& stacks) {
        std::vector<FfiSlotData> ffiSlots(stacks.size());
        for (size_t i = 0; i < stacks.size(); i++) {
            if (stacks[i] && stacks[i]->stackSize > 0) {
                ffiSlots[i].item_id = static_cast<int16_t>(stacks[i]->itemID);
                ffiSlots[i].count = static_cast<int8_t>(stacks[i]->stackSize);
                ffiSlots[i].damage = static_cast<int16_t>(stacks[i]->itemDamage);
            } else {
                ffiSlots[i].item_id = -1;
                ffiSlots[i].count = 0;
                ffiSlots[i].damage = 0;
            }
        }
        sendPacket(RustPackets::playerInventory(type, static_cast<int16_t>(ffiSlots.size()), ffiSlots.data()));
    };
    
    sendSection(-1, player_->inventory.mainInventory);
    sendSection(-2, player_->inventory.craftingInventory);
    sendSection(-3, player_->inventory.armorInventory);
}

int NetServerHandler::getHeldItemId() const {
    return heldItemId_;
}

void NetServerHandler::syncHeldItemSelection() {
    if (heldItemId_ <= 0) {
        heldItem_.reset();
        return;
    }

    int lastSlot = static_cast<int>(player_->inventory.mainInventory.size()) - 1;
    for (int i = 0; i < lastSlot; ++i) {
        auto* stack = player_->inventory.mainInventory[i].get();
        if (stack && stack->itemID == heldItemId_) {
            player_->inventory.currentItem = i;
            heldItem_.reset();
            return;
        }
    }

    if (!heldItem_ || heldItem_->itemID != heldItemId_) {
        heldItem_ = std::make_unique<ItemStack>(heldItemId_, 1, 0);
    }
    player_->inventory.mainInventory[lastSlot].reset();
    player_->inventory.currentItem = lastSlot;
}

ItemStack* NetServerHandler::getSelectedItemStack() {
    ItemStack* selected = player_->getCurrentEquippedItem();
    if (selected) {
        return selected;
    }

    if (heldItem_ && heldItemId_ > 0 && heldItem_->itemID == heldItemId_) {
        return heldItem_.get();
    }

    return nullptr;
}

void NetServerHandler::restoreHeldItem(int itemId) {
    heldItemId_ = itemId;
    int lastSlot = static_cast<int>(player_->inventory.mainInventory.size()) - 1;
    player_->inventory.mainInventory[lastSlot].reset();
    if (itemId <= 0) { player_->inventory.currentItem = 0; return; }
    for (int i = 0; i < lastSlot; ++i) {
        auto* s = player_->inventory.mainInventory[i].get();
        if (s && s->itemID == itemId) {
            heldItem_.reset();
            player_->inventory.currentItem = i;
            return;
        }
    }
    // Not in inventory yet
    heldItem_ = std::make_unique<ItemStack>(itemId, 1, 0);
    player_->inventory.currentItem = lastSlot;
    player_->inventory.mainInventory[lastSlot].reset(); // non-owning slot, managed separately
}

void NetServerHandler::sendChunks() {
    int chunkX = static_cast<int>(player_->posX) >> 4;
    int chunkZ = static_cast<int>(player_->posZ) >> 4;
    int r = mcServer_->getViewDistance();

    std::vector<std::pair<int, int>> chunksToLoad;
    int genR = r + 3; // Generate 3 extra rings for full population guarantee
    for (int cx = chunkX - genR; cx <= chunkX + genR; ++cx) {
        for (int cz = chunkZ - genR; cz <= chunkZ + genR; ++cz) {
            mcServer_->worldMngr->requestChunkAsync(cx, cz);
            chunksToLoad.push_back({cx, cz});
        }
    }
    
    // Sort chunks by distance to player so they load center-outwards
    std::ranges::sort(chunksToLoad, std::less{}, [chunkX, chunkZ](const std::pair<int, int>& p) {
        return (p.first - chunkX) * (p.first - chunkX) + (p.second - chunkZ) * (p.second - chunkZ);
    });

    for (const auto& [cx, cz] : chunksToLoad) {
        const bool isVisible = std::abs(cx - chunkX) <= r && std::abs(cz - chunkZ) <= r;
        mcServer_->worldMngr->requestChunkAsync(cx, cz, isVisible ? 0 : 2);
    }

    // Replace the queue entirely
    chunksToLoad_ = chunksToLoad;
    lastChunkX_ = chunkX;
    lastChunkZ_ = chunkZ;
}

// ======= Packet handlers =======

void NetServerHandler::handleChat(const RustPacket3Chat& pkt) {
    std::string msg = pkt.message ? pkt.message : "";
    if (msg.size() > 100) msg = msg.substr(0, 100);

    if (msg.starts_with("/")) {
        Logger::info("{} issued command: {}", player_->username, msg);
        handleCommand(msg);
    } else {
        std::string fullMsg = "<" + player_->username + "> " + msg;
        Logger::info("[CHAT] {}", fullMsg);
        mcServer_->configManager->broadcastPacket(RustPackets::chat(fullMsg));
    }
}

void NetServerHandler::handleCommand(const std::string& msg) {
    std::vector<std::string> args;
    std::istringstream ss(msg.substr(1));
    for (std::string tok; ss >> tok;) args.push_back(tok);
    if (args.empty()) return;

    const std::string& cmd = args[0];

    auto toInt = [&](const std::string& s) {
        int v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc()) throw std::invalid_argument("not a number");
        return v;
    };
    auto toDouble = [&](const std::string& s) {
        double v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc()) throw std::invalid_argument("not a number");
        return v;
    };

    const bool isOp = mcServer_->configManager->isOp(player_->username);

    try {
        if (!isOp && (cmd == "give" || cmd == "tp")) {
            sendPacket(RustPackets::chat("You do not have permission to use this command"));
            return;
        }
        if (cmd == "give") {
            if (args.size() < 2) {
                sendPacket(RustPackets::chat("Usage: /give <itemId> [count] [damage]"));
                return;
            }
            int itemId = toInt(args[1]);
            int count  = args.size() >= 3 ? toInt(args[2]) : 1;
            int damage = args.size() >= 4 ? toInt(args[3]) : 0;
        count = std::clamp(count, 1, 64);

        // Validate: blocks must exist in blocksList, items must be < 32000
        if (itemId <= 0 || itemId >= 32000) {
            sendPacket(RustPackets::chat("Invalid item id"));
            return;
        }
        if (itemId < 256 && Block::blocksList[itemId] == nullptr) {
            sendPacket(RustPackets::chat("Unknown block id: " + std::to_string(itemId)));
            return;
        }

        auto entity = std::make_unique<EntityItem>(itemId, count, damage);
        entity->setPosition(player_->posX, player_->posY, player_->posZ);
        entity->motionX = entity->motionY = entity->motionZ = 0.0;
        mcServer_->worldMngr->spawnEntityInWorld(std::move(entity));
        sendPacket(RustPackets::chat("Gave " + std::to_string(count) + "x " + std::to_string(itemId)));
    } else if (cmd == "tp") {
        if (args.size() < 4) {
            sendPacket(RustPackets::chat("Usage: /tp <x> <y> <z>"));
            return;
        }
        double tx = toDouble(args[1]);
        double ty = toDouble(args[2]);
        double tz = toDouble(args[3]);
        teleport(tx, ty, tz, player_->rotationYaw, player_->rotationPitch);
        sendPacket(RustPackets::chat("Teleported to " + std::to_string(tx) + ", " + std::to_string(ty) + ", " + std::to_string(tz)));
    } else {
        sendPacket(RustPackets::chat("Unknown command: " + cmd));
    }
    } catch (const std::exception&) {
        sendPacket(RustPackets::chat("Invalid command arguments"));
    }
}

void NetServerHandler::processMovement(double x, double y, double stance, double z, float yaw, float pitch, bool moving, bool rotating, bool onGround) {
    if (!hasMoved_) {
        lastX_ = player_->posX;
        lastY_ = player_->posY;
        lastZ_ = player_->posZ;
        hasMoved_ = true;
    }

    float finalYaw = rotating ? yaw : player_->rotationYaw;
    float finalPitch = rotating ? pitch : player_->rotationPitch;

    if (Entity* vehicle = player_->getRidingEntity()) {
        player_->rotationYaw = finalYaw;
        player_->rotationPitch = finalPitch;
        player_->onGround = onGround;
        player_->motionX = 0.0;
        player_->motionY = 0.0;
        player_->motionZ = 0.0;
        if (moving && y == -999.0 && stance == -999.0) {
            player_->motionX = x;
            player_->motionZ = z;
        }
        vehicle->updateRiderPosition();
        lastX_ = player_->posX;
        lastY_ = player_->posY;
        lastZ_ = player_->posZ;
        return;
    }

    if (moving && y == -999.0 && stance == -999.0) {
        moving = false;
    }

    const double baseX = lastX_;
    const double baseY = lastY_;
    const double baseZ = lastZ_;

    player_->setPositionAndRotation(baseX, baseY, baseZ, finalYaw, finalPitch);
    player_->motionX = 0.0;
    player_->motionY = 0.0;
    player_->motionZ = 0.0;

    if (!moving) {
        player_->rotationYaw = finalYaw;
        player_->rotationPitch = finalPitch;
        player_->onGround = onGround;
        lastX_ = player_->posX;
        lastY_ = player_->posY;
        lastZ_ = player_->posZ;
        return;
    }

    const RustBridge::FfiMovementInput moveCheck{
        .from_x = baseX,
        .from_y = baseY,
        .from_z = baseZ,
        .to_x = x,
        .to_y = y,
        .to_z = z,
        .stance = stance,
        .on_ground = onGround,
        .is_in_water = (player_->isInWater != 0),
        .fall_distance = player_->fallDistance,
    };

    const RustBridge::FfiMovementResult checkRes = RustBridge::validateMovement(moveCheck);
    if (checkRes.status == 1) {
        kick("Illegal stance");
        return;
    }
    if (checkRes.status == 2) {
        kick("Illegal position");
        return;
    }
    if (checkRes.status == 3) {
        kick("Moved too quickly");
        return;
    }
    if (checkRes.status == 4) {
        teleport(baseX, baseY, baseZ, finalYaw, finalPitch);
        return;
    }

    const double moveX = x - baseX;
    const double moveY = y - baseY;
    const double moveZ = z - baseZ;

    player_->suppressMoveFallState = true;
    player_->moveEntity(moveX, moveY, moveZ);
    player_->suppressMoveFallState = false;
    player_->rotationYaw = finalYaw;
    player_->rotationPitch = finalPitch;

    const RustBridge::FfiMovementInput fallCheck{
        .from_x = baseX,
        .from_y = baseY,
        .from_z = baseZ,
        .to_x = player_->posX,
        .to_y = player_->posY,
        .to_z = player_->posZ,
        .stance = stance,
        .on_ground = onGround,
        .is_in_water = (player_->isInWater != 0),
        .fall_distance = player_->fallDistance,
    };
    const RustBridge::FfiMovementResult fallRes = RustBridge::validateMovement(fallCheck);
    if (fallRes.fall_damage > 0) {
        player_->attackEntityFrom(nullptr, fallRes.fall_damage);
    }
    player_->fallDistance = fallRes.new_fall_distance;

    player_->onGround = onGround;
    lastX_ = player_->posX;
    lastY_ = player_->posY;
    lastZ_ = player_->posZ;
}

void NetServerHandler::handleFlying(const RustPacket10Flying& pkt) {
    processMovement(0.0, 0.0, 0.0, 0.0, 0.0f, 0.0f, false, false, pkt.on_ground);
}

void NetServerHandler::handlePlayerPosition(const RustPacket11PlayerPosition& pkt) {
    processMovement(pkt.x, pkt.y, pkt.stance, pkt.z, 0.0f, 0.0f, true, false, pkt.on_ground);
}

void NetServerHandler::handlePlayerLook(const RustPacket12PlayerLook& pkt) {
    processMovement(0.0, 0.0, 0.0, 0.0, pkt.yaw, pkt.pitch, false, true, pkt.on_ground);
}

void NetServerHandler::handlePlayerLookMove(const RustPacket13PlayerLookMove& pkt) {
    processMovement(pkt.x, pkt.y, pkt.stance, pkt.z, pkt.yaw, pkt.pitch, true, true, pkt.on_ground);
}

void NetServerHandler::handleBlockDig(const RustPacket14BlockDig& pkt) {
    syncHeldItemSelection();

    bool isOp = mcServer_->configManager->isOp(player_->username);
    bool checkDist = false;
    
    if (pkt.status == 0) checkDist = true;
    if (pkt.status == 1) checkDist = true;
    
    int x = pkt.x;
    // y arrives as a signed byte; Alpha protocol treats it as unsigned
    int y = static_cast<uint8_t>(pkt.y);
    if (y < 0 || y >= 128) return;
    int z = pkt.z;
    int face = pkt.face;

    if (checkDist) {
        double dX = player_->posX - (x + 0.5);
        double dY = player_->posY - (y + 0.5);
        double dZ = player_->posZ - (z + 0.5);
        if (dX * dX + dY * dY + dZ * dZ > 36.0) {
            return;
        }
    }
    
    int spawnProtectionRadius = mcServer_->getSpawnProtectionRadius();
    int distFromSpawnX = std::abs(x - mcServer_->getSpawnX());
    int distFromSpawnZ = std::abs(z - mcServer_->getSpawnZ());
    int distFromSpawn = std::max(distFromSpawnX, distFromSpawnZ);
    
    if (pkt.status == 0) {
        if (distFromSpawn > spawnProtectionRadius || isOp) {
            // If digging a chest, immediately send empty chest NBT to the client.
            // Client-side prediction breaks the block locally before the server
            // responds, so we must clear the client's TileEntityChest NOW —
            // before PlayerControllerMP calls onBlockRemoval and spawns ghost items.
            int blockId = mcServer_->worldMngr->getBlockId(x, y, z);
            if (blockId == 54) { // chest
                TileEntity* te = mcServer_->worldMngr->getTileEntity(x, y, z);
                if (te && dynamic_cast<TileEntityChest*>(te)) {
                    TileEntityChest emptyChest;
                    emptyChest.xCoord = x;
                    emptyChest.yCoord = y;
                    emptyChest.zCoord = z;
                    sendTileEntityPacket(&emptyChest);
                }
            }
            player_->itemInWorldManager->onBlockClicked(x, y, z, face);
        }
    } else if (pkt.status == 2) {
        player_->itemInWorldManager->cancelRemoving();
    } else if (pkt.status == 1) {
        if (distFromSpawn > spawnProtectionRadius || isOp) {
            player_->itemInWorldManager->blockRemoving(x, y, z, face);
        }
    } else if (pkt.status == 3) {
        double dX = player_->posX - (x + 0.5);
        double dY = player_->posY - (y + 0.5);
        double dZ = player_->posZ - (z + 0.5);
        if (dX * dX + dY * dY + dZ * dZ < 256.0) {
            sendPacket(RustPackets::blockChange(x, y, z, 
                mcServer_->worldMngr->getBlockId(x, y, z), 
                mcServer_->worldMngr->getBlockMetadata(x, y, z)));
        }
    }
}

void NetServerHandler::handlePlace(const RustPacket15Place& pkt) {
    bool isOp = mcServer_->configManager->isOp(player_->username);
    
    if (pkt.direction == -1) {
        // direction == 255 unsigned = -1 signed: right-click in air, use item
        ItemStack* itemstack = player_->inventory.getCurrentItem();
        if (itemstack) {
            player_->itemInWorldManager->useItem(player_, mcServer_->worldMngr.get(), itemstack);
        }
    } else {
        int x = pkt.x;
        int y = pkt.y & 0xFF;  // treat as unsigned byte: 0-255
        int z = pkt.z;
        int direction = pkt.direction & 0xFF;
        
        int spawnProtectionRadius = mcServer_->getSpawnProtectionRadius();
        int distX = std::abs(x - mcServer_->worldMngr->spawnX);
        int distZ = std::abs(z - mcServer_->worldMngr->spawnZ);
        int dist = std::max(distX, distZ);
        
        if (dist > spawnProtectionRadius || isOp) {
            // If right-clicking a chest or furnace, send its contents BEFORE activeBlockOrUseItem.
            // The client opens the GUI locally before our response arrives, so we must
            // send Packet59 as early as possible to populate the GUI correctly.
            int clickedId = mcServer_->worldMngr->getBlockId(x, y, z);
            if (clickedId == 54 || clickedId == 61 || clickedId == 62) {
                TileEntity* te = mcServer_->worldMngr->getTileEntity(x, y, z);
                if (te) sendTileEntityPacket(te);
            }

            ItemStack* itemstack = nullptr;
            if (pkt.item_id >= 0) {
                ItemStack* held = player_->inventory.getCurrentItem();
                if (held && held->itemID == pkt.item_id && held->stackSize > 0) {
                    itemstack = held;
                } else {
                    for (auto& s : player_->inventory.mainInventory) {
                        if (s && s->itemID == pkt.item_id && s->stackSize > 0) { itemstack = s.get(); break; }
                    }
                }
            }

            bool used = false;
            if (itemstack) {
                const bool isBoatItem = Item::boat && itemstack->itemID == Item::boat->itemID;
                const bool clickedWater = mcServer_->worldMngr->getBlockMaterial(x, y, z) == &Material::water;
                const int clickedId = mcServer_->worldMngr->getBlockId(x, y, z);
                const bool clickedInteractiveBlock =
                    clickedId == 54 || // chest
                    clickedId == 58 || // crafting table
                    clickedId == 61 || // furnace idle
                    clickedId == 62;   // furnace active

                if (isBoatItem && clickedWater) {
                    auto boatEntity = std::make_unique<EntityBoat>(mcServer_->worldMngr.get(),
                        static_cast<double>(x) + 0.5,
                        static_cast<double>(y) + 1.5,
                        static_cast<double>(z) + 0.5);
                    mcServer_->worldMngr->spawnEntityInWorld(std::move(boatEntity));
                    if (itemstack->stackSize > 0) {
                        --itemstack->stackSize;
                    }
                    used = true;
                } else if (clickedInteractiveBlock) {
                    // Interaction with GUI blocks must not also place held blocks.
                    // Chest/furnace activation is handled by blockActivated();
                    // workbench GUI is client-driven in this protocol, so consume click.
                    Block* clickedBlock = (clickedId > 0 && clickedId < 256) ? Block::blocksList[clickedId] : nullptr;
                    if (clickedBlock) {
                        used = clickedBlock->blockActivated(mcServer_->worldMngr.get(), x, y, z, player_);
                    }
                    if (clickedId == 58) {
                        used = true;
                    }
                } else {
                    used = player_->itemInWorldManager->activeBlockOrUseItem(
                        player_, mcServer_->worldMngr.get(), itemstack, x, y, z, direction);

                    // Alpha boats are right-click items; when client sends a place-on-block packet,
                    // fall back to onItemRightClick so boat placement still works.
                    if (!used && isBoatItem) {
                        player_->itemInWorldManager->useItem(player_, mcServer_->worldMngr.get(), itemstack);
                    }
                }
            }

            for (auto& s : player_->inventory.mainInventory) {
                if (s && s->stackSize <= 0) {
                    s.reset();
                }
            }

            // Always sync inventory after placement to ensure client has correct state
            // This prevents desync issues and dupe bugs
            sendInventory();
        }
        
        // Always send block update at clicked position (rollback for client if rejected)
        sendPacket(RustPackets::blockChange(x, y, z,
            mcServer_->worldMngr->getBlockId(x, y, z),
            mcServer_->worldMngr->getBlockMetadata(x, y, z)));
        
        // Also send block update at adjacen face (where new block would be placed)
        int nx = x, ny = y, nz = z;
        if (direction == 0) --ny;
        else if (direction == 1) ++ny;
        else if (direction == 2) --nz;
        else if (direction == 3) ++nz;
        else if (direction == 4) --nx;
        else if (direction == 5) ++nx;
        
        sendPacket(RustPackets::blockChange(nx, ny, nz,
            mcServer_->worldMngr->getBlockId(nx, ny, nz),
            mcServer_->worldMngr->getBlockMetadata(nx, ny, nz)));
    }
}

void NetServerHandler::handleBlockItemSwitch(const RustPacket16BlockItemSwitch& pkt) {
    int lastSlot = static_cast<int>(player_->inventory.mainInventory.size()) - 1;
    player_->inventory.mainInventory[lastSlot].reset();

    heldItemId_ = pkt.item_id;

    if (pkt.item_id == 0) {
        heldItem_.reset();
        player_->inventory.currentItem = 0;
        return;
    }

    // Point currentItem at the real slot
    for (int i = 0; i < lastSlot; ++i) {
        auto* s = player_->inventory.mainInventory[i].get();
        if (s && s->itemID == pkt.item_id) {
            heldItem_.reset();
            player_->inventory.currentItem = i;
            return;
        }
    }

    // Not in inventory yet — keep a fallback copy so attacking/mining still uses the selected item.
    if (!heldItem_ || heldItem_->itemID != pkt.item_id) {
        heldItem_ = std::make_unique<ItemStack>(pkt.item_id, 1, 0);
    }
    player_->inventory.currentItem = lastSlot;
    player_->inventory.mainInventory[lastSlot].reset(); // non-owning, managed by heldItem_
}

void NetServerHandler::handleArmAnimation(const RustPacket18ArmAnimation& pkt) {
    if (pkt.animate == 1) {
        player_->swingItem();
    } else if (pkt.animate == 104) {
        player_->isSneaking = true;
    } else if (pkt.animate == 105) {
        player_->isSneaking = false;
    }
}

void NetServerHandler::handleKickDisconnect(const RustPacket255KickDisconnect& /*pkt*/) {
    netManager_->shutdown("Quitting");
}

void NetServerHandler::handlePlayerInventory(const RustPacket5PlayerInventory& pkt) {
    auto applySlots = [](std::vector<std::unique_ptr<ItemStack>>& inv,
                         const FfiSlotData* slots, int16_t itemCount, int skipSlot) {
        if (!slots || itemCount <= 0) return;
        size_t count = std::min(static_cast<size_t>(itemCount), inv.size());
        for (size_t i = 0; i < count; i++) {
            if ((int)i == skipSlot) continue;
            int16_t id = slots[i].item_id;
            if (id >= 0 && id < 32000) {
                Item* item = Item::itemsList[id];
                int16_t dmg = (item && item->maxDamage > 0) ? slots[i].damage : 0;
                inv[i] = std::make_unique<ItemStack>(id, slots[i].count, dmg);
            } else {
                inv[i].reset();
            }
        }
    };

    if (pkt.type == -1) {
        int lastSlot = static_cast<int>(player_->inventory.mainInventory.size()) - 1;
        applySlots(player_->inventory.mainInventory, pkt.slots, pkt.item_count, lastSlot);
        if (heldItemId_ > 0) {
            bool found = false;
            for (int i = 0; i < lastSlot; ++i) {
                auto* s = player_->inventory.mainInventory[i].get();
                if (s && s->itemID == heldItemId_) {
                    player_->inventory.currentItem = i;
                    player_->inventory.mainInventory[lastSlot].reset();
                    found = true;
                    break;
                }
            }
            if (!found) {
                heldItemId_ = 0;
                heldItem_.reset();
                player_->inventory.mainInventory[lastSlot].reset();
            }
        }
    } else if (pkt.type == -2)
        applySlots(player_->inventory.craftingInventory, pkt.slots, pkt.item_count, -1);
    else if (pkt.type == -3)
        applySlots(player_->inventory.armorInventory, pkt.slots, pkt.item_count, -1);
}

void NetServerHandler::handlePickupSpawn(const RustPacket21PickupSpawn& pkt) {
    if (pkt.item_id <= 0 || pkt.count <= 0) return;

    // The client sends Packet21 when dropping an item.
    // If the GUI is closed, Q drops the currently held item.
    // We should first try to consume it from the held item to avoid out-of-sync deletes.
    ItemStack* held = player_->inventory.getCurrentItem();
    bool consumed = false;

    if (held && held->itemID == pkt.item_id && held->stackSize > 0) {
        held->stackSize -= pkt.count;
        if (held->stackSize <= 0) {
            player_->inventory.mainInventory[player_->inventory.currentItem].reset();
        }
        consumed = true;
    }

    if (!consumed) {
        for (auto& s : player_->inventory.mainInventory) {
            if (s && s->itemID == pkt.item_id && s->stackSize > 0) {
                s->stackSize -= pkt.count;
                if (s->stackSize <= 0) {
                    s.reset();
                }
                break;
            }
        }
    }

    double wx = pkt.x / 32.0;
    double wy = pkt.y / 32.0;
    double wz = pkt.z / 32.0;

    auto item = std::make_unique<EntityItem>(pkt.item_id, pkt.count, 0);
    item->setPosition(wx, wy, wz);
    item->motionX    = pkt.rotation / 128.0;
    item->motionY    = pkt.pitch    / 128.0;
    item->motionZ    = pkt.roll     / 128.0;
    item->pickupDelay = 10;
    mcServer_->worldMngr->spawnEntityInWorld(std::move(item));
}

void NetServerHandler::handleComplexEntity(const RustPacket59ComplexEntity& pkt) {
    if (pkt.nbt_len == 0 || pkt.nbt_len > 65536 || !pkt.nbt_data) {
        return;
    }

    std::vector<uint8_t> decompressed = RustBridge::gzipDecompress(pkt.nbt_data, pkt.nbt_len);
    if (decompressed.size() > 524288) return; // 512 KiB limit on decompressed NBT
    if (decompressed.empty()) {
        return;
    }

    // Parse NBT
    ByteBuffer buf;
    buf.data = std::move(decompressed);
    buf.readPos = 0;
    auto nbt = NBTCompound::readRoot(buf);
    if (!nbt) {
        return;
    }

    // Validate coords (anti-cheat, same as Java)
    int nx = nbt->getInt("x"), ny = nbt->getInt("y"), nz = nbt->getInt("z");
    if (nx != pkt.x || ny != static_cast<int>(pkt.y) || nz != pkt.z) {
        return;
    }

    TileEntity* te = mcServer_->worldMngr->getTileEntity(pkt.x, pkt.y, pkt.z);
    if (!te) {
        Logger::warning("handleComplexEntity: no TileEntity at ({}, {}, {})", pkt.x, (int)pkt.y, pkt.z);
        return;
    }

    Logger::info("handleComplexEntity: updating {} at ({}, {}, {})", te->getEntityId(), pkt.x, (int)pkt.y, pkt.z);
    te->readFromNBT(*nbt);

    if (auto* sign = dynamic_cast<TileEntitySign*>(te)) {
        Logger::debug("Sign text: '{}' '{}' '{}' '{}'", sign->getLine(0), sign->getLine(1), sign->getLine(2), sign->getLine(3));
    }

    // Mark dirty: saves chunk + broadcasts Packet59 to nearby players
    mcServer_->worldMngr->markTileEntityChanged(pkt.x, pkt.y, pkt.z, te);
}

void NetServerHandler::handleErrorMessage(const std::string& reason) {
    Logger::info("{} lost connection: {}", player_->username, reason);
    player_->savedHeldItemId = heldItemId_;
    disconnected = true;
    mcServer_->configManager->broadcastPacket(
        RustPackets::chat("\u00a7e" + player_->username + " left the game."));
    mcServer_->configManager->playerLoggedOut(player_);
}
