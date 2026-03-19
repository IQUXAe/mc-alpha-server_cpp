#pragma once

#include "NetHandler.h"
#include "NetworkManager.h"
#include "packets/AllPackets.h"
#include "../forward.h"

#include <memory>
#include <string>
#include <random>
#include <iostream>

class MinecraftServer;

class NetLoginHandler : public NetHandler {
public:
    std::unique_ptr<NetworkManager> netManager;
    bool finishedProcessing = false;

    NetLoginHandler(MinecraftServer* server, int socketFd, const std::string& remoteAddr, const std::string& desc);

    void tryLogin();
    void kickUser(const std::string& reason);

    // NetHandler overrides
    void handleHandshake(Packet2Handshake& pkt) override;
    void handleLogin(Packet1Login& pkt) override;
    void handleErrorMessage(const std::string& reason) override;

    std::string getUserAndIPString() const;

private:
    MinecraftServer* mcServer_;
    int tickCounter_ = 0;
    std::string username_;
    std::unique_ptr<Packet1Login> pendingLogin_;
    std::string serverId_;

    void doLogin(Packet1Login& pkt);

    static std::mt19937_64 rng_;
};
