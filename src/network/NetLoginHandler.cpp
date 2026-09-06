#include "NetLoginHandler.h"
#include "NetServerHandler.h"
#include "../MinecraftServer.h"
#include "../core/Logger.h"
#include "../../rust/alpha_bridge/alpha_bridge.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace {

std::string performSessionCheck(std::string_view username, std::string_view serverId) {
    char buf[128];
    const bool ok = rust_session_check(
        std::string(username).c_str(),
        std::string(serverId).c_str(),
        buf, sizeof(buf));
    if (!ok) {
        throw std::runtime_error("Session verification failed");
    }
    return std::string(buf);
}
}

std::mt19937_64 NetLoginHandler::rng_(std::random_device{}());

NetLoginHandler::NetLoginHandler(MinecraftServer* server, int socketFd, const std::string& remoteAddr, const std::string& desc)
    : mcServer_(server), remoteAddress_(remoteAddr) {
    netManager = std::make_unique<NetworkManager>(socketFd, desc, this);
    netManager->setRemoteAddress(remoteAddr);
}

void NetLoginHandler::tryLogin() {
    std::optional<LoginData> loginToProcess;
    {
        std::lock_guard lock(stateMutex_);
        if (pendingLogin_) {
            loginToProcess = std::move(pendingLogin_);
            pendingLogin_.reset();
        }
    }

    if (loginToProcess) {
        doLogin(*loginToProcess);
    }

    if (finishedProcessing.load(std::memory_order_relaxed)) {
        return;
    }

    if (++tickCounter_ >= 600) {
        kickUser("Took too long to log in");
    } else {
        if (netManager) {
            netManager->processReadPackets();
        }
    }
}

void NetLoginHandler::kickUser(const std::string& reason) {
    if (finishedProcessing.load(std::memory_order_relaxed)) {
        return;
    }
    try {
        Logger::info("Disconnecting {}: {}", getUserAndIPString(), reason);
        if (netManager) {
            netManager->sendPacket(RustPackets::kickDisconnect(reason));
            netManager->serverShutdown();
        }
        finishedProcessing.store(true, std::memory_order_relaxed);
    } catch (const std::exception& e) {
        Logger::severe("{}", e.what());
    }
}

void NetLoginHandler::handleHandshake(const RustPacket2Handshake& pkt) {
    if (mcServer_->isOnlineMode()) {
        // Generate server ID for auth
        std::uniform_int_distribution<int64_t> dist{std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()};
        int64_t randVal = dist(rng_);
        std::stringstream ss;
        ss << std::hex << randVal;
        {
            std::lock_guard lock(stateMutex_);
            serverId_ = ss.str();
        }
        netManager->sendPacket(RustPackets::handshake(serverId_));
    } else {
        netManager->sendPacket(RustPackets::handshake("-"));
    }
}

void NetLoginHandler::handleLogin(const RustPacket1Login& pkt) {
    LoginData data;
    data.protocolVersion = pkt.protocol_version;
    data.username = pkt.username ? pkt.username : "";
    data.password = pkt.password ? pkt.password : "";
    data.mapSeed = pkt.map_seed;
    data.dimension = pkt.dimension;

    while (!data.username.empty() && (data.username.back() == '\0' || data.username.back() == '\r' || data.username.back() == '\n' || data.username.back() == ' ')) {
        data.username.pop_back();
    }
    username_ = data.username;
    if (data.protocolVersion != 6) {
        if (data.protocolVersion > 6) {
            kickUser("Outdated server!");
        } else {
            kickUser("Outdated client!");
        }
        return;
    }

    // In offline mode, login directly
    if (!mcServer_->isOnlineMode()) {
        doLogin(data);
    } else {
        {
            std::lock_guard lock(stateMutex_);
            if (verificationStarted_) {
                kickUser("Duplicate login packet");
                return;
            }
            verificationStarted_ = true;
        }

        loginVerifierThread_ = std::jthread([this, data](std::stop_token) mutable {
            verifyLoginSession(std::move(data));
        });
    }
}

void NetLoginHandler::doLogin(const LoginData& pkt) {
    auto player = mcServer_->configManager->login(this, pkt.username, pkt.password);
    if (player) {
        Logger::info("{} logged in with entity id {}", getUserAndIPString(), player->entityId);

        auto serverHandler = std::make_unique<NetServerHandler>(mcServer_, std::move(netManager), player);

        // Send login response
        serverHandler->sendPacket(RustPackets::login(
            player->entityId, "", "", mcServer_->getWorldSeed(), mcServer_->getWorldDimension()));

        // Send spawn position
        serverHandler->sendPacket(RustPackets::spawnPosition(
            mcServer_->getSpawnX(), mcServer_->getSpawnY(), mcServer_->getSpawnZ()));

        // Broadcast join message
        mcServer_->configManager->broadcastPacket(
            RustPackets::chat("\u00a7e" + player->username + " joined the game."));

        // Complete player login
        mcServer_->configManager->playerLoggedIn(player);

        // Restore held item from saved state (mirrors Java's field_10_k)
        if (player->savedHeldItemId > 0) {
            serverHandler->restoreHeldItem(player->savedHeldItemId);
        }

        // Send position
        serverHandler->teleport(player->posX, player->posY, player->posZ, player->rotationYaw, player->rotationPitch);
        serverHandler->sendPacket(RustPackets::updateHealth(player->health));
        
        // Send full inventory to client (Packet5) - exactly as Java's NetServerHandler.func_40_d()
        serverHandler->sendInventory();
        
        serverHandler->sendChunks();
        serverHandler->sendPacket(RustPackets::updateTime(mcServer_->getWorldTime()));

        // Register connection (transfer ownership to NetworkListenThread)
        mcServer_->networkListenThread->addConnection(std::move(serverHandler));
    }

    finishedProcessing.store(true, std::memory_order_relaxed);
}

void NetLoginHandler::handleErrorMessage(const std::string& reason) {
    Logger::info("{} lost connection", getUserAndIPString());
    finishedProcessing.store(true, std::memory_order_relaxed);
}

std::string NetLoginHandler::getUserAndIPString() const {
    std::string ip = netManager ? netManager->getRemoteAddress() : "unknown";
    if (!username_.empty()) {
        return username_ + " [" + ip + "]";
    }
    return ip;
}

void NetLoginHandler::verifyLoginSession(LoginData pkt) {
    try {
        std::string sid;
        {
            std::lock_guard lock(stateMutex_);
            sid = serverId_;
        }
        const std::string reply = performSessionCheck(pkt.username, sid);
        Logger::info("Session check for {} returned '{}'", pkt.username, reply);

        if (reply == "YES") {
            std::lock_guard lock(stateMutex_);
            pendingLogin_ = std::move(pkt);
            return;
        }

        kickUser("Failed to verify username!");
    } catch (const std::exception& e) {
        Logger::warning("Session verification failed for {}: {}", pkt.username, e.what());
        kickUser("Failed to verify username!");
    }
}
