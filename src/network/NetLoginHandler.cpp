#include "NetLoginHandler.h"
#include "NetServerHandler.h"
#include "../MinecraftServer.h"
#include "../core/Logger.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sstream>
#include <iomanip>
#include <string_view>
#include <utility>

namespace {
constexpr std::string_view kSessionHost = "www.minecraft.net";
constexpr std::string_view kSessionPath = "/game/checkserver.jsp";

std::string urlEncode(std::string_view value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;

    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << static_cast<char>(ch);
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }

    return encoded.str();
}

bool recvAll(int socketFd, std::string& response) {
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t received = ::recv(socketFd, buffer.data(), buffer.size(), 0);
        if (received == 0) {
            return true;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        response.append(buffer.data(), static_cast<std::size_t>(received));
    }
}

std::string performSessionCheck(std::string_view username, std::string_view serverId) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* rawResult = nullptr;
    const int lookupResult = ::getaddrinfo(std::string(kSessionHost).c_str(), "80", &hints, &rawResult);
    if (lookupResult != 0) {
        throw std::runtime_error(std::string("Session DNS lookup failed: ") + ::gai_strerror(lookupResult));
    }

    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses(rawResult, ::freeaddrinfo);
    int socketFd = -1;

    for (addrinfo* addr = addresses.get(); addr != nullptr; addr = addr->ai_next) {
        socketFd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (socketFd < 0) {
            continue;
        }

        timeval timeout{.tv_sec = 5, .tv_usec = 0};
        ::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        if (::connect(socketFd, addr->ai_addr, addr->ai_addrlen) == 0) {
            break;
        }

        ::close(socketFd);
        socketFd = -1;
    }

    if (socketFd < 0) {
        throw std::runtime_error("Failed to connect to session server");
    }

    const auto closeSocket = [&]() {
        if (socketFd >= 0) {
            ::shutdown(socketFd, SHUT_RDWR);
            ::close(socketFd);
            socketFd = -1;
        }
    };

    const std::string request =
        "GET " + std::string(kSessionPath) + "?user=" + urlEncode(username) + "&serverId=" + urlEncode(serverId) + " HTTP/1.1\r\n"
        "Host: " + std::string(kSessionHost) + "\r\n"
        "Connection: close\r\n"
        "User-Agent: alpha_server/0.2.8\r\n"
        "\r\n";

    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t writeResult = ::send(socketFd, request.data() + sent, request.size() - sent, 0);
        if (writeResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            closeSocket();
            throw std::runtime_error(std::string("Failed to send session request: ") + std::strerror(errno));
        }
        sent += static_cast<std::size_t>(writeResult);
    }

    std::string response;
    if (!recvAll(socketFd, response)) {
        const std::string error = std::strerror(errno);
        closeSocket();
        throw std::runtime_error("Failed to read session response: " + error);
    }
    closeSocket();

    const std::size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        throw std::runtime_error("Invalid HTTP response from session server");
    }

    const std::string_view statusLine(response.data(), response.find("\r\n"));
    if (statusLine.find("200") == std::string_view::npos) {
        throw std::runtime_error("Session server returned non-200 status");
    }

    std::string body = response.substr(headerEnd + 4);
    while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' ' || body.back() == '\t')) {
        body.pop_back();
    }
    return body;
}
}

std::mt19937_64 NetLoginHandler::rng_(std::random_device{}());

NetLoginHandler::NetLoginHandler(MinecraftServer* server, int socketFd, const std::string& remoteAddr, const std::string& desc)
    : mcServer_(server) {
    netManager = std::make_unique<NetworkManager>(socketFd, desc, this);
    netManager->setRemoteAddress(remoteAddr);
}

void NetLoginHandler::tryLogin() {
    std::optional<Packet1Login> loginToProcess;
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

    if (++tickCounter_ >= 600) {
        kickUser("Took too long to log in");
    } else {
        netManager->processReadPackets();
    }
}

void NetLoginHandler::kickUser(const std::string& reason) {
    try {
        Logger::info("Disconnecting {}: {}", getUserAndIPString(), reason);
        netManager->addToSendQueue(std::make_unique<Packet255KickDisconnect>(reason));
        netManager->serverShutdown();
        finishedProcessing.store(true, std::memory_order_relaxed);
    } catch (const std::exception& e) {
        Logger::severe("{}", e.what());
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
    while (!pkt.username.empty() && (pkt.username.back() == '\0' || pkt.username.back() == '\r' || pkt.username.back() == '\n' || pkt.username.back() == ' ')) {
        pkt.username.pop_back();
    }
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
        {
            std::lock_guard lock(stateMutex_);
            if (verificationStarted_) {
                kickUser("Duplicate login packet");
                return;
            }
            verificationStarted_ = true;
        }

        loginVerifierThread_ = std::jthread([this, pkt](std::stop_token) mutable {
            verifyLoginSession(std::move(pkt));
        });
    }
}

void NetLoginHandler::doLogin(Packet1Login& pkt) {
    auto player = mcServer_->configManager->login(this, pkt.username, pkt.password);
    if (player) {
        Logger::info("{} logged in with entity id {}", getUserAndIPString(), player->entityId);

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
        serverHandler->sendPacket(std::make_unique<Packet8UpdateHealth>(player->health));
        
        // Send full inventory to client (Packet5) - exactly as Java's NetServerHandler.func_40_d()
        serverHandler->sendInventory();
        
        serverHandler->sendChunks();

        // Register connection
        mcServer_->networkListenThread->addConnection(serverHandler);

        // Send time
        serverHandler->sendPacket(std::make_unique<Packet4UpdateTime>(mcServer_->getWorldTime()));
    }

    finishedProcessing.store(true, std::memory_order_relaxed);
}

void NetLoginHandler::handleErrorMessage(const std::string& reason) {
    Logger::info("{} lost connection", getUserAndIPString());
    finishedProcessing.store(true, std::memory_order_relaxed);
}

std::string NetLoginHandler::getUserAndIPString() const {
    if (!username_.empty()) {
        return username_ + " [" + netManager->getRemoteAddress() + "]";
    }
    return netManager->getRemoteAddress();
}

void NetLoginHandler::verifyLoginSession(Packet1Login pkt) {
    try {
        const std::string reply = performSessionCheck(pkt.username, serverId_);
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
