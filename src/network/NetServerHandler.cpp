#include "../world/World.h"
#include "NetServerHandler.h"
#include "../MinecraftServer.h"
#include "../entity/EntityPlayerMP.h"
#include "../entity/EntityTracker.h"
#include "../world/Chunk.h"
#include "../world/TileEntity.h"
#include "../world/TileEntityChest.h"
#include "../world/TileEntitySign.h"
#include "../block/Block.h"
#include "../entity/EntityItem.h"
#include "../core/NBT.h"
#include "../core/Logger.h"

#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <functional>
#include <zlib.h>

NetServerHandler::NetServerHandler(MinecraftServer* server, std::unique_ptr<NetworkManager> netMgr, EntityPlayerMP* player)
    : mcServer_(server), netManager_(std::move(netMgr)), player_(player) {
    netManager_->setNetHandler(this);
    player_->netHandler = this;
    lastX_ = player_->posX;
    lastY_ = player_->posY;
    lastZ_ = player_->posZ;
}

NetServerHandler::~NetServerHandler() = default;

void NetServerHandler::tick() {
    netManager_->processReadPackets();
    if (++tickCounter_ % 20 == 0) {
        sendPacket(std::make_unique<Packet0KeepAlive>());
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
            sendPacket(std::make_unique<Packet50PreChunk>(sx, sz, false));
            sentChunks_.erase(key);
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
            sendPacket(std::make_unique<Packet50PreChunk>(px, pz, true));
            sendPacket(std::make_unique<Packet51MapChunk>(px * 16, 0, pz * 16, 16, 128, 16, chunk->getChunkData()));

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
            
            sentChunks_.insert(chunkKey(px, pz));
            it = chunksToLoad_.erase(it);
            ++sent;
        } else {
            ++it;
        }
    }
}

void NetServerHandler::handleRespawn(Packet9Respawn&) {
    if (player_->health > 0) {
        return;
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

    const double spawnX = mcServer_->worldMngr->spawnX + 0.5;
    const double spawnY = mcServer_->worldMngr->spawnY;
    const double spawnZ = mcServer_->worldMngr->spawnZ + 0.5;
    player_->setPositionAndRotation(spawnX, spawnY, spawnZ, 0.0f, 0.0f);

    sendPacket(std::make_unique<Packet9Respawn>());
    sendPacket(std::make_unique<Packet8UpdateHealth>(player_->health));
    teleport(spawnX, spawnY, spawnZ, 0.0f, 0.0f);
    sendInventory();
}

void NetServerHandler::handleUseEntity(Packet7UseEntity& pkt) {
    if (!pkt.isLeftClick || !mcServer_ || !mcServer_->entityTracker) {
        return;
    }

    Entity* target = mcServer_->entityTracker->getEntityById(pkt.targetEntityId);
    if (!target || target == player_ || target->isDead || !target->canBeCollidedWith()) {
        return;
    }

    const double reachSq = player_->getDistanceSqToEntity(*target);
    if (reachSq > 36.0) {
        return;
    }

    int damage = 1;
    ItemStack* held = player_->getCurrentEquippedItem();
    if (held) {
        damage = held->getDamageVsEntity(target);
    }

    target->attackEntityFrom(player_, damage);

    if (held) {
        if (auto* living = dynamic_cast<EntityLiving*>(target)) {
            held->hitEntity(living);
            if (held->stackSize <= 0) {
                player_->destroyCurrentEquippedItem();
                sendInventory();
            }
        }
    }
}


void NetServerHandler::kick(const std::string& reason) {
    if (disconnected) return;
    Logger::info("Disconnecting {}: {}", player_->username, reason);
    player_->savedHeldItemId = heldItemId_;
    sendPacket(std::make_unique<Packet255KickDisconnect>(reason));
    netManager_->serverShutdown();
    disconnected = true;

    mcServer_->configManager->broadcastPacket(
        std::make_unique<Packet3Chat>("\u00a7e" + player_->username + " left the game."));
    mcServer_->configManager->playerLoggedOut(player_);
}

void NetServerHandler::sendPacket(std::unique_ptr<Packet> pkt) {
    netManager_->addToSendQueue(std::move(pkt));
}

void NetServerHandler::sendTileEntityPacket(TileEntity* te) {
    if (!te) return;
    
    NBTCompound nbt;
    te->writeToNBT(nbt);
    ByteBuffer rawBuf;
    nbt.writeRoot(rawBuf, "");

    z_stream strm{};
    strm.zalloc = Z_NULL; strm.zfree = Z_NULL; strm.opaque = Z_NULL;
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return;

    std::vector<uint8_t> compressed(rawBuf.data.size() + 256);
    strm.next_in   = rawBuf.data.data();
    strm.avail_in  = static_cast<uInt>(rawBuf.data.size());
    strm.next_out  = compressed.data();
    strm.avail_out = static_cast<uInt>(compressed.size());
    deflate(&strm, Z_FINISH);
    compressed.resize(strm.total_out);
    deflateEnd(&strm);

    auto pkt59 = std::make_unique<Packet59ComplexEntity>();
    pkt59->x = te->xCoord;
    pkt59->y = static_cast<int16_t>(te->yCoord);
    pkt59->z = te->zCoord;
    pkt59->nbtData = std::move(compressed);

    sendPacket(std::move(pkt59));
}

void NetServerHandler::teleport(double x, double y, double z, float yaw, float pitch) {
    lastX_ = x;
    lastY_ = y;
    lastZ_ = z;
    player_->setPosition(x, y, z);
    player_->rotationYaw = yaw;
    player_->rotationPitch = pitch;
    sendPacket(std::make_unique<Packet13PlayerLookMove>(x, y + 1.6200000047683716, y, z, yaw, pitch, false));
}

void NetServerHandler::sendInventory() {
    // Exactly mirrors Java's NetServerHandler.func_40_d()
    // Sends Packet5 for each inventory section: type -1=main, -2=crafting, -3=armor
    
    auto buildPacket = [](int type, const std::vector<std::unique_ptr<ItemStack>>& stacks) {
        auto pkt = std::make_unique<Packet5PlayerInventory>();
        pkt->type = type;
        pkt->slots.resize(stacks.size());
        for (size_t i = 0; i < stacks.size(); i++) {
            if (stacks[i] && stacks[i]->stackSize > 0) {
                pkt->slots[i].itemId = static_cast<int16_t>(stacks[i]->itemID);
                pkt->slots[i].count = static_cast<int8_t>(stacks[i]->stackSize);
                pkt->slots[i].damage = static_cast<int16_t>(stacks[i]->itemDamage);
            } else {
                pkt->slots[i].itemId = -1;
                pkt->slots[i].count = 0;
                pkt->slots[i].damage = 0;
            }
        }
        return pkt;
    };
    
    sendPacket(buildPacket(-1, player_->inventory.mainInventory));
    sendPacket(buildPacket(-2, player_->inventory.craftingInventory));
    sendPacket(buildPacket(-3, player_->inventory.armorInventory));
}

int NetServerHandler::getHeldItemId() const {
    return heldItemId_;
}

void NetServerHandler::restoreHeldItem(int itemId) {
    heldItemId_ = itemId;
    int lastSlot = static_cast<int>(player_->inventory.mainInventory.size()) - 1;
    player_->inventory.mainInventory[lastSlot].reset();
    if (itemId <= 0) { player_->inventory.currentItem = 0; return; }
    for (int i = 0; i < lastSlot; ++i) {
        auto* s = player_->inventory.mainInventory[i].get();
        if (s && s->itemID == itemId) { player_->inventory.currentItem = i; return; }
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

void NetServerHandler::handleChat(Packet3Chat& pkt) {
    std::string msg = pkt.message;
    if (msg.size() > 100) msg = msg.substr(0, 100);

    if (msg.starts_with("/")) {
        Logger::info("{} issued command: {}", player_->username, msg);
        handleCommand(msg);
    } else {
        std::string fullMsg = "<" + player_->username + "> " + msg;
        Logger::info("[CHAT] {}", fullMsg);
        mcServer_->configManager->broadcastPacket(std::make_unique<Packet3Chat>(fullMsg));
    }
}

void NetServerHandler::handleCommand(const std::string& msg) {
    // Tokenize
    std::vector<std::string> args;
    std::istringstream ss(msg.substr(1));
    for (std::string tok; ss >> tok;) args.push_back(tok);
    if (args.empty()) return;

    const std::string& cmd = args[0];

    if (cmd == "give") {
        // /give <itemId> [count] [damage]
        if (args.size() < 2) {
            sendPacket(std::make_unique<Packet3Chat>("Usage: /give <itemId> [count] [damage]"));
            return;
        }
        int itemId = std::stoi(args[1]);
        int count  = args.size() >= 3 ? std::stoi(args[2]) : 1;
        int damage = args.size() >= 4 ? std::stoi(args[3]) : 0;
        count = std::clamp(count, 1, 64);

        // Validate: blocks must exist in blocksList, items must be < 32000
        if (itemId <= 0 || itemId >= 32000) {
            sendPacket(std::make_unique<Packet3Chat>("Invalid item id"));
            return;
        }
        if (itemId < 256 && Block::blocksList[itemId] == nullptr) {
            sendPacket(std::make_unique<Packet3Chat>("Unknown block id: " + std::to_string(itemId)));
            return;
        }

        auto entity = std::make_unique<EntityItem>(itemId, count, damage);
        entity->setPosition(player_->posX, player_->posY, player_->posZ);
        entity->motionX = entity->motionY = entity->motionZ = 0.0;
        mcServer_->worldMngr->spawnEntityInWorld(std::move(entity));
        sendPacket(std::make_unique<Packet3Chat>("Gave " + std::to_string(count) + "x " + std::to_string(itemId)));
    } else {
        sendPacket(std::make_unique<Packet3Chat>("Unknown command: " + cmd));
    }
}

void NetServerHandler::handleFlying(Packet10Flying& pkt) {
    const bool wasOnGround = player_->onGround;
    const double previousY = player_->posY;

    if (pkt.moving) {
        player_->setPosition(pkt.x, pkt.y, pkt.z);
        lastX_ = pkt.x;
        lastY_ = pkt.y;
        lastZ_ = pkt.z;

        if (!pkt.onGround && pkt.y < previousY) {
            player_->fallDistance += static_cast<float>(previousY - pkt.y);
        }
    }

    if (pkt.rotating) {
        player_->rotationYaw = pkt.yaw;
        player_->rotationPitch = pkt.pitch;
    }

    player_->onGround = pkt.onGround;
    if (pkt.onGround && !wasOnGround && player_->fallDistance > 0.0f) {
        player_->onFall(player_->fallDistance);
        player_->fallDistance = 0.0f;
    }
}

void NetServerHandler::handlePlayerPosition(Packet11PlayerPosition& pkt) {
    Packet10Flying flying;
    flying.x = pkt.x; flying.y = pkt.y; flying.z = pkt.z;
    flying.stance = pkt.stance;
    flying.onGround = pkt.onGround;
    flying.moving = true;
    handleFlying(flying);
}

void NetServerHandler::handlePlayerLook(Packet12PlayerLook& pkt) {
    Packet10Flying flying;
    flying.yaw = pkt.yaw; flying.pitch = pkt.pitch;
    flying.onGround = pkt.onGround;
    flying.rotating = true;
    handleFlying(flying);
}

void NetServerHandler::handlePlayerLookMove(Packet13PlayerLookMove& pkt) {
    Packet10Flying flying;
    flying.x = pkt.x; flying.y = pkt.y; flying.z = pkt.z;
    flying.stance = pkt.stance;
    flying.yaw = pkt.yaw; flying.pitch = pkt.pitch;
    flying.onGround = pkt.onGround;
    flying.moving = true;
    flying.rotating = true;
    handleFlying(flying);
}

void NetServerHandler::handleBlockDig(Packet14BlockDig& pkt) {
    // Java: restore field_10_k (held item) into inventory slot before every dig packet
    if (heldItemId_ > 0) {
        int lastSlot = static_cast<int>(player_->inventory.mainInventory.size()) - 1;
        bool found = false;
        for (int i = 0; i < lastSlot; ++i) {
            auto* s = player_->inventory.mainInventory[i].get();
            if (s && s->itemID == heldItemId_) {
                player_->inventory.currentItem = i;
                found = true;
                break;
            }
        }
        if (!found) {
            if (!heldItem_ || heldItem_->itemID != heldItemId_) {
                heldItem_ = std::make_unique<ItemStack>(heldItemId_, 1, 0);
            }
            player_->inventory.mainInventory[lastSlot].reset(); // non-owning slot
            player_->inventory.currentItem = lastSlot;
        }
    }

    bool isOp = mcServer_->configManager->isOp(player_->username);
    bool checkDist = false;
    
    if (pkt.status == 0) checkDist = true;
    if (pkt.status == 1) checkDist = true;
    
    int x = pkt.x;
    int y = pkt.y;
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
            sendPacket(std::make_unique<Packet53BlockChange>(x, y, z, 
                mcServer_->worldMngr->getBlockId(x, y, z), 
                mcServer_->worldMngr->getBlockMetadata(x, y, z)));
        }
    }
}

void NetServerHandler::handlePlace(Packet15Place& pkt) {
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
            if (pkt.itemId >= 0) {
                ItemStack* held = player_->inventory.getCurrentItem();
                if (held && held->itemID == pkt.itemId && held->stackSize > 0) {
                    itemstack = held;
                } else {
                    for (auto& s : player_->inventory.mainInventory) {
                        if (s && s->itemID == pkt.itemId && s->stackSize > 0) { itemstack = s.get(); break; }
                    }
                }
            }

            if (itemstack) {
                player_->itemInWorldManager->activeBlockOrUseItem(
                    player_, mcServer_->worldMngr.get(), itemstack, x, y, z, direction);
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
        sendPacket(std::make_unique<Packet53BlockChange>(x, y, z,
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
        
        sendPacket(std::make_unique<Packet53BlockChange>(nx, ny, nz,
            mcServer_->worldMngr->getBlockId(nx, ny, nz),
            mcServer_->worldMngr->getBlockMetadata(nx, ny, nz)));
    }
}

void NetServerHandler::handleBlockItemSwitch(Packet16BlockItemSwitch& pkt) {
    int lastSlot = static_cast<int>(player_->inventory.mainInventory.size()) - 1;
    player_->inventory.mainInventory[lastSlot].reset();

    heldItemId_ = pkt.itemId;

    if (pkt.itemId == 0) {
        heldItem_.reset();
        player_->inventory.currentItem = 0;
        return;
    }

    // Point currentItem at the real slot
    for (int i = 0; i < lastSlot; ++i) {
        auto* s = player_->inventory.mainInventory[i].get();
        if (s && s->itemID == pkt.itemId) {
            player_->inventory.currentItem = i;
            return;
        }
    }

    // Not in inventory yet — use placeholder in lastSlot (stackSize=0 prevents placing)
    if (!heldItem_ || heldItem_->itemID != pkt.itemId) {
        heldItem_ = std::make_unique<ItemStack>(pkt.itemId, 0, 0);
    }
    player_->inventory.currentItem = lastSlot;
    player_->inventory.mainInventory[lastSlot].reset(); // non-owning, managed by heldItem_
}

void NetServerHandler::handleArmAnimation(Packet18ArmAnimation& pkt) {
    if (pkt.animate == 1) {
        auto broadcastPkt = std::make_unique<Packet18ArmAnimation>(player_->entityId, 1);
        mcServer_->entityTracker->broadcastPacket(player_, std::move(broadcastPkt));
    } else if (pkt.animate == 104) {
        player_->isSneaking = true;
    } else if (pkt.animate == 105) {
        player_->isSneaking = false;
    }
}

void NetServerHandler::handleKickDisconnect(Packet255KickDisconnect& pkt) {
    netManager_->shutdown("Quitting");
}

void NetServerHandler::handlePlayerInventory(Packet5PlayerInventory& pkt) {
    auto applySlots = [](std::vector<std::unique_ptr<ItemStack>>& inv,
                         const std::vector<Packet5PlayerInventory::SlotData>& slots, int skipSlot) {
        size_t count = std::min(slots.size(), inv.size());
        for (size_t i = 0; i < count; i++) {
            if ((int)i == skipSlot) continue;
            int16_t id = slots[i].itemId;
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
        applySlots(player_->inventory.mainInventory, pkt.slots, lastSlot);
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
        applySlots(player_->inventory.craftingInventory, pkt.slots, -1);
    else if (pkt.type == -3)
        applySlots(player_->inventory.armorInventory, pkt.slots, -1);
}

void NetServerHandler::handlePickupSpawn(Packet21PickupSpawn& pkt) {
    if (pkt.itemId <= 0 || pkt.count <= 0) return;

    // Try to consume from server inventory (Q-key drop while holding item).
    // For inventory GUI drops the client already sent Packet5 removing the item,
    // so we spawn regardless — the inventory is already authoritative via Packet5.
    for (auto& s : player_->inventory.mainInventory) {
        if (s && s->itemID == pkt.itemId && s->stackSize > 0) {
            s->stackSize -= pkt.count;
            if (s->stackSize <= 0) {
                s.reset();
            }
            break;
        }
    }

    double wx = pkt.x / 32.0;
    double wy = pkt.y / 32.0;
    double wz = pkt.z / 32.0;

    auto item = std::make_unique<EntityItem>(pkt.itemId, pkt.count, 0);
    item->setPosition(wx, wy, wz);
    item->motionX    = pkt.rotation / 128.0;
    item->motionY    = pkt.pitch    / 128.0;
    item->motionZ    = pkt.roll     / 128.0;
    item->pickupDelay = 10;
    mcServer_->worldMngr->spawnEntityInWorld(std::move(item));
}

void NetServerHandler::handleComplexEntity(Packet59ComplexEntity& pkt) {
    // Client sends Packet59 when closing a chest/furnace GUI.
    // entityData is always GZip-compressed NBT (Java: CompressedStreamTools.func_773_a)
    if (pkt.nbtData.empty()) {
        return;
    }

    // Decompress GZip
    z_stream strm{};
    strm.zalloc = Z_NULL; strm.zfree = Z_NULL; strm.opaque = Z_NULL;
    strm.next_in  = const_cast<Bytef*>(pkt.nbtData.data());
    strm.avail_in = static_cast<uInt>(pkt.nbtData.size());

    if (inflateInit2(&strm, 15 + 16) != Z_OK) {
        return;
    }

    std::vector<uint8_t> decompressed(65536);
    strm.next_out  = decompressed.data();
    strm.avail_out = static_cast<uInt>(decompressed.size());
    int ret = inflate(&strm, Z_FINISH);
    size_t decompSize = strm.total_out;
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        return;
    }
    decompressed.resize(decompSize);
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
        Logger::debug("Sign text: '{}' '{}' '{}' '{}'", sign->signText[0], sign->signText[1], sign->signText[2], sign->signText[3]);
    }

    // Mark dirty: saves chunk + broadcasts Packet59 to nearby players
    mcServer_->worldMngr->markTileEntityChanged(pkt.x, pkt.y, pkt.z, te);
}

void NetServerHandler::handleErrorMessage(const std::string& reason) {
    Logger::info("{} lost connection: {}", player_->username, reason);
    player_->savedHeldItemId = heldItemId_;
    disconnected = true;
    mcServer_->configManager->broadcastPacket(
        std::make_unique<Packet3Chat>("\u00a7e" + player_->username + " left the game."));
    mcServer_->configManager->playerLoggedOut(player_);
}
