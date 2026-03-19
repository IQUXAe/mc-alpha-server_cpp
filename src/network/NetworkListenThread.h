#pragma once

#include "NetworkManager.h"
#include "NetHandler.h"
#include "../forward.h"

#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <iostream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class NetLoginHandler;
class NetServerHandler;
class MinecraftServer;

class NetworkListenThread {
public:
    MinecraftServer* mcServer;
    std::atomic<bool> isRunning{false};

    NetworkListenThread(MinecraftServer* server, const std::string& bindAddress, int port);
    ~NetworkListenThread();

    void addConnection(NetServerHandler* handler);
    void addPendingConnection(std::shared_ptr<NetLoginHandler> handler);

    // Called every tick from the main server thread
    void networkTick();

private:
    int serverSocketFd_ = -1;
    std::thread acceptThread_;
    int connectionCounter_ = 0;

    std::mutex pendingMutex_;
    std::vector<std::shared_ptr<NetLoginHandler>> pendingConnections_;

    std::mutex activeMutex_;
    std::vector<NetServerHandler*> activeConnections_;

    void acceptLoop();
};
