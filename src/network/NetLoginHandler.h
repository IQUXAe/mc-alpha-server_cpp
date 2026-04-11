#pragma once

#include "NetHandler.h"
#include "NetworkManager.h"
#include "packets/AllPackets.h"
#include "../forward.h"

#include <atomic>
#include <memory>
#include <string>
#include <random>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

class MinecraftServer;

class NetLoginHandler : public NetHandler {
public:
    std::unique_ptr<NetworkManager> netManager;
    std::atomic<bool> finishedProcessing = false;

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
    std::string serverId_;
    std::mutex stateMutex_;
    std::optional<Packet1Login> pendingLogin_;
    std::jthread loginVerifierThread_;
    bool verificationStarted_ = false;

    void doLogin(Packet1Login& pkt);
    void verifyLoginSession(Packet1Login pkt);

    static std::mt19937_64 rng_;
};
