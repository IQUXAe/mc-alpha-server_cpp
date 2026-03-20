#include "../world/World.h"
#include "NetServerHandler.h"
#include "../MinecraftServer.h"
#include "../entity/EntityPlayerMP.h"
#include "../world/Chunk.h"
#include "../block/Block.h"

#include <iostream>
#include <algorithm>
#include <cmath>

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

        int r = mcServer_->viewDistance;
        int genR = r + 2; // extra generation ring so border chunks get populated

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
        for (int i = cx - genR; i <= cx + genR; ++i) {
            for (int j = cz - genR; j <= cz + genR; ++j) {
                bool isPadding = (std::abs(i - cx) > r || std::abs(j - cz) > r);
                if (isPadding) {
                    // Queue for generation silently (don't add to toLoad), just force generate now
                    mcServer_->worldMngr->getChunk(i, j);
                    continue;
                }
                // Only queue visible chunks that haven't been sent yet
                if (sentChunks_.find(chunkKey(i, j)) == sentChunks_.end()) {
                    needed.push_back({i, j});
                }
            }
        }

        std::sort(needed.begin(), needed.end(), [cx, cz](const std::pair<int,int>& a, const std::pair<int,int>& b) {
            int dA = (a.first - cx)*(a.first - cx) + (a.second - cz)*(a.second - cz);
            int dB = (b.first - cx)*(b.first - cx) + (b.second - cz)*(b.second - cz);
            return dA < dB;
        });

        // Prepend to queue (new position = higher priority), avoid duplicates with a quick erase
        // Simple approach: replace queue with newly sorted list merged with existing
        // Keep existing items that are still in view radius
        std::vector<std::pair<int,int>> merged;
        merged.reserve(needed.size() + chunksToLoad_.size());
        // Add new ones first (higher priority - closet to player)
        for (auto& c : needed) {
            merged.push_back(c);
        }
        // Re-add old queued items that weren't in needed and still within view
        for (auto& c : chunksToLoad_) {
            bool alreadyInNeeded = false;
            for (auto& n : needed) {
                if (n.first == c.first && n.second == c.second) { alreadyInNeeded = true; break; }
            }
            if (!alreadyInNeeded && sentChunks_.find(chunkKey(c.first, c.second)) == sentChunks_.end()) {
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

        auto* chunk = mcServer_->worldMngr->getChunk(px, pz);

        if (chunk && chunk->isTerrainPopulated) {
            // Generate the +1, +1 padding neighbors synchronously to ensure population
            mcServer_->worldMngr->getChunk(px + 1, pz);
            mcServer_->worldMngr->getChunk(px, pz + 1);
            mcServer_->worldMngr->getChunk(px + 1, pz + 1);

            sendPacket(std::make_unique<Packet50PreChunk>(px, pz, true));
            sendPacket(std::make_unique<Packet51MapChunk>(px * 16, 0, pz * 16, 16, 128, 16, chunk->getChunkData()));
            sentChunks_.insert(chunkKey(px, pz));
            it = chunksToLoad_.erase(it);
            ++sent;
        } else {
            // Chunk exists but not yet populated — its neighbors might not be generated yet.
            // Force-generate the 2x2 block of neighbors so World::getChunk can trigger population.
            if (chunk) {
                mcServer_->worldMngr->getChunk(px + 1, pz);
                mcServer_->worldMngr->getChunk(px, pz + 1);
                mcServer_->worldMngr->getChunk(px + 1, pz + 1);
            }
            ++it;
        }
    }
}


void NetServerHandler::kick(const std::string& reason) {
    if (disconnected) return;
    std::cout << "[INFO] Disconnecting " << player_->username << ": " << reason << std::endl;
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

void NetServerHandler::teleport(double x, double y, double z, float yaw, float pitch) {
    lastX_ = x;
    lastY_ = y;
    lastZ_ = z;
    player_->setPosition(x, y, z);
    player_->rotationYaw = yaw;
    player_->rotationPitch = pitch;
    sendPacket(std::make_unique<Packet13PlayerLookMove>(x, y + 1.6200000047683716, y, z, yaw, pitch, false));
}

void NetServerHandler::sendChunks() {
    int chunkX = static_cast<int>(player_->posX) >> 4;
    int chunkZ = static_cast<int>(player_->posZ) >> 4;
    int r = mcServer_->viewDistance;

    std::vector<std::pair<int, int>> chunksToLoad;
    int genR = r + 2; // Generate 2 extra rings for full population guarantee
    for (int cx = chunkX - genR; cx <= chunkX + genR; ++cx) {
        for (int cz = chunkZ - genR; cz <= chunkZ + genR; ++cz) {
            chunksToLoad.push_back({cx, cz});
        }
    }
    
    // Sort chunks by distance to player so they load center-outwards
    std::sort(chunksToLoad.begin(), chunksToLoad.end(), [chunkX, chunkZ](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        int distA = (a.first - chunkX) * (a.first - chunkX) + (a.second - chunkZ) * (a.second - chunkZ);
        int distB = (b.first - chunkX) * (b.first - chunkX) + (b.second - chunkZ) * (b.second - chunkZ);
        return distA < distB;
    });

    // Replace the queue entirely
    chunksToLoad_ = chunksToLoad;
    lastChunkX_ = chunkX;
    lastChunkZ_ = chunkZ;
}

// ======= Packet handlers =======

void NetServerHandler::handleChat(Packet3Chat& pkt) {
    std::string msg = pkt.message;
    if (msg.size() > 100) msg = msg.substr(0, 100);

    // Check for / commands
    if (msg.starts_with("/")) {
        // TODO: handle commands
        std::cout << "[INFO] " << player_->username << " issued command: " << msg << std::endl;
    } else {
        std::string fullMsg = "<" + player_->username + "> " + msg;
        std::cout << "[CHAT] " << fullMsg << std::endl;
        mcServer_->configManager->broadcastPacket(std::make_unique<Packet3Chat>(fullMsg));
    }
}

void NetServerHandler::handleFlying(Packet10Flying& pkt) {
    // Update on-ground state
    player_->onGround = pkt.onGround;

    if (pkt.moving) {
        player_->posX = pkt.x;
        player_->posY = pkt.y;
        player_->posZ = pkt.z;
        lastX_ = pkt.x;
        lastY_ = pkt.y;
        lastZ_ = pkt.z;
    }

    if (pkt.rotating) {
        player_->rotationYaw = pkt.yaw;
        player_->rotationPitch = pkt.pitch;
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
    int realX = pkt.x;
    int realY = pkt.y;
    int realZ = pkt.z;

    // In Alpha, status 2/3 often sends 0,0,0 coordinates
    if ((pkt.status == 2 || pkt.status == 3) && pkt.x == 0 && pkt.y == 0 && pkt.z == 0) {
        realX = player_->miningStartX;
        realY = player_->miningStartY;
        realZ = player_->miningStartZ;
    }

    std::cout << "[DEBUG] Dig packet: status=" << (int)pkt.status << " x=" << realX << " y=" << (int)realY << " z=" << realZ << std::endl;

    if (pkt.status == 0) {
        player_->miningStartX = realX;
        player_->miningStartY = realY;
        player_->miningStartZ = realZ;
        player_->miningStartTime = mcServer_->getWorldTime();
        
        uint8_t id = mcServer_->worldMngr->getBlockId(realX, realY, realZ);
        // Instant-break for decorations
        if (id == 37 || id == 38 || id == 39 || id == 40 || id == 50 || id == 51 || id == 55 || id == 59 || id == 83) {
            mcServer_->worldMngr->setBlockWithNotify(realX, realY, realZ, 0);
            if (Block::blocksList[id]) {
                Block::blocksList[id]->dropBlockAsItem(mcServer_->worldMngr.get(), realX, realY, realZ, 0);
            }
        }
    } else if (pkt.status == 2) {
        uint8_t id = mcServer_->worldMngr->getBlockId(realX, realY, realZ);
        if (id == 0) return;

        std::cout << "[DEBUG] Block broken at: " << realX << "," << realY << "," << realZ << " (id=" << (int)id << ")" << std::endl;
        
        mcServer_->worldMngr->setBlockWithNotify(realX, realY, realZ, 0);
        if (Block::blocksList[id]) {
            Block::blocksList[id]->dropBlockAsItemWithChance(mcServer_->worldMngr.get(), realX, realY, realZ, 0, 1.0f);
        }
    } else if (pkt.status == 3) {
        // Mining aborted - do nothing, just log
        std::cout << "[DEBUG] Mining aborted by client at: " << realX << "," << realY << "," << realZ << std::endl;
    }
}

void NetServerHandler::handlePlace(Packet15Place& pkt) {
    // TODO: Implement block placing
    std::cout << "[DEBUG] Place at " << pkt.x << ", " << (int)pkt.y << ", " << pkt.z
              << " dir=" << (int)pkt.direction << " item=" << pkt.itemId << std::endl;
}

void NetServerHandler::handleBlockItemSwitch(Packet16BlockItemSwitch& pkt) {
    // TODO: Update player's held item
}

void NetServerHandler::handleArmAnimation(Packet18ArmAnimation& pkt) {
    if (pkt.animate == 1) {
        // Broadcast arm swing to other players
        // TODO: implement when entity tracking is complete
    }
}

void NetServerHandler::handleKickDisconnect(Packet255KickDisconnect& pkt) {
    netManager_->shutdown("Quitting");
}

void NetServerHandler::handleErrorMessage(const std::string& reason) {
    std::cout << "[INFO] " << player_->username << " lost connection: " << reason << std::endl;
    disconnected = true;
    mcServer_->configManager->broadcastPacket(
        std::make_unique<Packet3Chat>("\u00a7e" + player_->username + " left the game."));
    mcServer_->configManager->playerLoggedOut(player_);
}
