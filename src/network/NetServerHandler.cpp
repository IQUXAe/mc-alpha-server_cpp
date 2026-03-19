#include "NetServerHandler.h"
#include "../MinecraftServer.h"
#include "../entity/EntityPlayerMP.h"

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

#include "../world/ChunkGenerator.h"

void NetServerHandler::sendChunks() {
    int chunkX = static_cast<int>(player_->posX) >> 4;
    int chunkZ = static_cast<int>(player_->posZ) >> 4;

    auto rawChunk = ChunkGenerator::generateFlatChunk();
    auto compressedChunk = ChunkGenerator::compressChunkData(rawChunk);

    for (int cx = chunkX - 2; cx <= chunkX + 2; ++cx) {
        for (int cz = chunkZ - 2; cz <= chunkZ + 2; ++cz) {
            // Tell client to allocate chunk
            sendPacket(std::make_unique<Packet50PreChunk>(cx, cz, true));
            // Send chunk data (16x128x16 starting at cx*16, 0, cz*16)
            sendPacket(std::make_unique<Packet51MapChunk>(
                cx * 16,     // x
                0,           // y
                cz * 16,     // z
                16,          // sizeX
                128,         // sizeY
                16,          // sizeZ
                compressedChunk
            ));
        }
    }
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
    // TODO: Implement block digging with ItemInWorldManager
    std::cout << "[DEBUG] Block dig at " << pkt.x << ", " << (int)pkt.y << ", " << pkt.z
              << " face=" << (int)pkt.face << " status=" << (int)pkt.status << std::endl;
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
