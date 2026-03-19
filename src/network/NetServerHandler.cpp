#include "../world/World.h"
#include "NetServerHandler.h"
#include "../MinecraftServer.h"
#include "../entity/EntityPlayerMP.h"
#include "../world/Chunk.h"
#include "../block/Block.h"

#include <iostream>
#include <algorithm>

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

    int cx = static_cast<int>(player_->posX) >> 4;
    int cz = static_cast<int>(player_->posZ) >> 4;
    if (lastChunkX_ != cx || lastChunkZ_ != cz) {
        int r = mcServer_->viewDistance;
        std::vector<std::pair<int, int>> chunksToLoad;
        for (int i = cx - r; i <= cx + r; ++i) {
            for (int j = cz - r; j <= cz + r; ++j) {
                if (std::abs(i - lastChunkX_) > r || std::abs(j - lastChunkZ_) > r) {
                    chunksToLoad.push_back({i, j});
                }
            }
        }
        
        std::sort(chunksToLoad.begin(), chunksToLoad.end(), [cx, cz](const std::pair<int, int>& a, const std::pair<int, int>& b) {
            int distA = (a.first - cx) * (a.first - cx) + (a.second - cz) * (a.second - cz);
            int distB = (b.first - cx) * (b.first - cx) + (b.second - cz) * (b.second - cz);
            return distA < distB;
        });

        for (const auto& coords : chunksToLoad) {
            auto* chunk = mcServer_->worldMngr->getChunk(coords.first, coords.second);
            if (chunk) {
                sendPacket(std::make_unique<Packet50PreChunk>(coords.first, coords.second, true));
                sendPacket(std::make_unique<Packet51MapChunk>(coords.first * 16, 0, coords.second * 16, 16, 128, 16, chunk->getChunkData()));
            }
        }
        lastChunkX_ = cx;
        lastChunkZ_ = cz;
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
    for (int cx = chunkX - r; cx <= chunkX + r; ++cx) {
        for (int cz = chunkZ - r; cz <= chunkZ + r; ++cz) {
            chunksToLoad.push_back({cx, cz});
        }
    }
    
    // Sort chunks by distance to player so they load center-outwards
    std::sort(chunksToLoad.begin(), chunksToLoad.end(), [chunkX, chunkZ](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        int distA = (a.first - chunkX) * (a.first - chunkX) + (a.second - chunkZ) * (a.second - chunkZ);
        int distB = (b.first - chunkX) * (b.first - chunkX) + (b.second - chunkZ) * (b.second - chunkZ);
        return distA < distB;
    });

    for (const auto& coords : chunksToLoad) {
        int cx = coords.first;
        int cz = coords.second;
        auto* chunk = mcServer_->worldMngr->getChunk(cx, cz);
        if (!chunk) continue;

        // Tell client to allocate chunk
        sendPacket(std::make_unique<Packet50PreChunk>(cx, cz, true));
        // Send compressed chunk data
        sendPacket(std::make_unique<Packet51MapChunk>(
            cx * 16, 0, cz * 16, 16, 128, 16, chunk->getChunkData()
        ));
    }
    
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
    if (pkt.status == 0) {
        player_->miningStartX = pkt.x;
        player_->miningStartY = pkt.y;
        player_->miningStartZ = pkt.z;
        player_->miningTicks = 0;
    } else if (pkt.status == 2) {
        player_->miningTicks = -1;
    } else if (pkt.status == 1) {
        if (player_->miningTicks >= 0 && pkt.x == player_->miningStartX && pkt.y == player_->miningStartY && pkt.z == player_->miningStartZ) {
            player_->miningTicks++;
            
            uint8_t id = mcServer_->worldMngr->getBlockId(pkt.x, pkt.y, pkt.z);
            if (id > 0) {
                // Determine target ticks
                int reqTicks = 15; // default 0.75s
                if (id == 18 || id == 20) reqTicks = 4; // leaves, glass fast
                else if (id == 3 || id == 12 || id == 13) reqTicks = 10; // dirt, sand
                else if (id >= 1 && id != 7) reqTicks = 20; // stone etc.

                if (player_->miningTicks >= reqTicks) {
                    mcServer_->worldMngr->setBlockWithNotify(pkt.x, pkt.y, pkt.z, 0);
                    if (Block::blocksList[id]) {
                        Block::blocksList[id]->dropBlockAsItem(mcServer_->worldMngr.get(), pkt.x, pkt.y, pkt.z, 0);
                    }
                    player_->miningTicks = -1;
                }
            }
        }
    } else if (pkt.status == 4) { // Drop held item
        // Not implemented yet
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
