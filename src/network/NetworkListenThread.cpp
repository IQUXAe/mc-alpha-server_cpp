#include "NetworkListenThread.h"
#include "NetLoginHandler.h"
#include "NetServerHandler.h"
#include "../core/Logger.h"

#include <cstring>
#include <stdexcept>

NetworkListenThread::NetworkListenThread(MinecraftServer* server, const std::string& bindAddress, int port)
    : mcServer(server) {

    serverSocketFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketFd_ < 0) {
        throw std::runtime_error("Failed to create server socket");
    }

    int opt = 1;
    ::setsockopt(serverSocketFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bindAddress.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) {
            ::close(serverSocketFd_);
            throw std::runtime_error("Invalid bind address: " + bindAddress);
        }
    }

    if (::bind(serverSocketFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(serverSocketFd_);
        throw std::runtime_error("Failed to bind to port " + std::to_string(port));
    }

    if (::listen(serverSocketFd_, 16) < 0) {
        ::close(serverSocketFd_);
        throw std::runtime_error("Failed to listen on port " + std::to_string(port));
    }

    isRunning = true;
    acceptThread_ = std::jthread([this](std::stop_token stopToken) { acceptLoop(stopToken); });
}

NetworkListenThread::~NetworkListenThread() {
    isRunning = false;
    if (serverSocketFd_ >= 0) {
        ::shutdown(serverSocketFd_, SHUT_RDWR);
        ::close(serverSocketFd_);
        serverSocketFd_ = -1;
    }
    acceptThread_.request_stop();
}

void NetworkListenThread::addConnection(NetServerHandler* handler) {
    std::lock_guard<std::mutex> lock(activeMutex_);
    activeConnections_.push_back(handler);
}

void NetworkListenThread::addPendingConnection(std::shared_ptr<NetLoginHandler> handler) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingConnections_.push_back(std::move(handler));
}

void NetworkListenThread::networkTick() {
    // Process pending login connections
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (auto it = pendingConnections_.begin(); it != pendingConnections_.end(); ) {
            auto& loginHandler = *it;
            try {
                loginHandler->tryLogin();
            } catch (const std::exception& e) {
                loginHandler->kickUser("Internal server error");
                std::cerr << "[WARNING] Failed to handle packet: " << e.what() << std::endl;
            }

            if (loginHandler->finishedProcessing) {
                it = pendingConnections_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Process active connections
    {
        std::lock_guard<std::mutex> lock(activeMutex_);
        for (auto it = activeConnections_.begin(); it != activeConnections_.end(); ) {
            auto* handler = *it;
            try {
                handler->tick();
            } catch (const std::exception& e) {
                std::cerr << "[WARNING] Failed to handle packet: " << e.what() << std::endl;
                handler->kick("Internal server error");
            }

            if (handler->disconnected) {
                it = activeConnections_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void NetworkListenThread::acceptLoop(std::stop_token stopToken) {
    while (isRunning && !stopToken.stop_requested()) {
        struct sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = ::accept(serverSocketFd_, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);

        if (clientFd < 0) {
            if (!isRunning || stopToken.stop_requested()) break;
            Logger::warning("Accept failed: {}", std::strerror(errno));
            continue;
        }

        // Get client address string
        char addrStr[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
        int clientPort = ntohs(clientAddr.sin_port);
        std::string remoteAddr = std::string("/") + addrStr + ":" + std::to_string(clientPort);

        std::string connDesc = "#" + std::to_string(connectionCounter_++);

        try {
            auto loginHandler = std::make_shared<NetLoginHandler>(mcServer, clientFd, remoteAddr, connDesc);
            addPendingConnection(loginHandler);
        } catch (const std::exception& e) {
            Logger::warning("Failed to create login handler: {}", e.what());
            ::close(clientFd);
        }
    }
}
