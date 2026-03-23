#include "NetLoginHandler.h"
#include "NetServerHandler.h"
#include "../MinecraftServer.h"

#include <sstream>
#include <iomanip>

std::mt19937_64 NetLoginHandler::rng_(std::random_device{}());

NetLoginHandler::NetLoginHandler(MinecraftServer* server, int socketFd, const std::string& remoteAddr, const std::string& desc)
    : mcServer_(server) {
    netManager = std::make_unique<NetworkManager>(socketFd, desc, this);
    netManager->setRemoteAddress(remoteAddr);
}

void NetLoginHandler::tryLogin() {
    if (pendingLogin_) {
        doLogin(*pendingLogin_);
        pendingLogin_.reset();
    }

    if (++tickCounter_ >= 600) {
        kickUser("Took too long to log in");
    } else {
        netManager->processReadPackets();
    }
}

void NetLoginHandler::kickUser(const std::string& reason) {
    try {
        std::cout << "[INFO] Disconnecting " << getUserAndIPString() << ": " << reason << std::endl;
        netManager->addToSendQueue(std::make_unique<Packet255KickDisconnect>(reason));
        netManager->serverShutdown();
        finishedProcessing = true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
    }
}

void NetLoginHandler::handleHandshake(Packet2Handshake& pkt) {
    if (mcServer_->isOnlineMode()) {
        // Generate server ID for auth
        std::uniform_int_distribution<int64_t> dist;
        int64_t randVal = dist(rng_);
        std::stringstream ss;
        ss << std::hex << randVal;
        serverId_ = ss.str();
        netManager->addToSendQueue(std::make_unique<Packet2Handshake>(serverId_));
    } else {
        netManager->addToSendQueue(std::make_unique<Packet2Handshake>("-"));
    }
}

void NetLoginHandler::handleLogin(Packet1Login& pkt) {
    username_ = pkt.username;
    if (pkt.protocolVersion != 6) {
        if (pkt.protocolVersion > 6) {
            kickUser("Outdated server!");
        } else {
            kickUser("Outdated client!");
        }
        return;
    }

    // In offline mode, login directly
    if (!mcServer_->isOnlineMode()) {
        doLogin(pkt);
    } else {
        // TODO: online auth verification
        // For now, accept directly
        doLogin(pkt);
    }
}

void NetLoginHandler::doLogin(Packet1Login& pkt) {
    auto player = mcServer_->configManager->login(this, pkt.username, pkt.password);
    if (player) {
        std::cout << "[INFO] " << getUserAndIPString() << " logged in with entity id " << player->entityId << std::endl;

        auto serverHandler = new NetServerHandler(mcServer_, std::move(netManager), player);

        // Send login response
        serverHandler->sendPacket(std::make_unique<Packet1Login>(
            "", "", player->entityId, mcServer_->getWorldSeed(), mcServer_->getWorldDimension()));

        // Send spawn position
        serverHandler->sendPacket(std::make_unique<Packet6SpawnPosition>(
            mcServer_->getSpawnX(), mcServer_->getSpawnY(), mcServer_->getSpawnZ()));

        // Broadcast join message
        mcServer_->configManager->broadcastPacket(
            std::make_unique<Packet3Chat>("\u00a7e" + player->username + " joined the game."));

        // Complete player login
        mcServer_->configManager->playerLoggedIn(player);

        // Restore held item from saved state (mirrors Java's field_10_k)
        if (player->savedHeldItemId > 0) {
            serverHandler->restoreHeldItem(player->savedHeldItemId);
        }

        // Send position
        serverHandler->teleport(player->posX, player->posY, player->posZ, player->rotationYaw, player->rotationPitch);
        
        // Send full inventory to client (Packet5) - exactly as Java's NetServerHandler.func_40_d()
        serverHandler->sendInventory();
        
        serverHandler->sendChunks();

        // Register connection
        mcServer_->networkListenThread->addConnection(serverHandler);

        // Send time
        serverHandler->sendPacket(std::make_unique<Packet4UpdateTime>(mcServer_->getWorldTime()));
    }

    finishedProcessing = true;
}

void NetLoginHandler::handleErrorMessage(const std::string& reason) {
    std::cout << "[INFO] " << getUserAndIPString() << " lost connection" << std::endl;
    finishedProcessing = true;
}

std::string NetLoginHandler::getUserAndIPString() const {
    if (!username_.empty()) {
        return username_ + " [" + netManager->getRemoteAddress() + "]";
    }
    return netManager->getRemoteAddress();
}
