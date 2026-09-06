#pragma once

#include "NetHandler.h"
#include "NetworkManager.h"
#include "RustPackets.h"
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

struct LoginData {
    int32_t protocolVersion = 0;
    std::string username;
    std::string password;
    int64_t mapSeed = 0;
    int8_t dimension = 0;
};

class NetLoginHandler : public NetHandler {
public:
    std::unique_ptr<NetworkManager> netManager;
    std::atomic<bool> finishedProcessing = false;

    NetLoginHandler(MinecraftServer* server, int socketFd, const std::string& remoteAddr, const std::string& desc);

    void tryLogin();
    void kickUser(const std::string& reason);

    // NetHandler overrides
    void handleHandshake(const RustPacket2Handshake& pkt) override;
    void handleLogin(const RustPacket1Login& pkt) override;
    void handleErrorMessage(const std::string& reason) override;

    std::string getUserAndIPString() const;

    // The remote address survives after netManager is moved out in doLogin().
    std::string getRemoteAddress() const { return remoteAddress_; }

private:
    MinecraftServer* mcServer_;
    std::string remoteAddress_;
    int tickCounter_ = 0;
    std::string username_;
    std::string serverId_;
    std::mutex stateMutex_;
    std::optional<LoginData> pendingLogin_;
    std::jthread loginVerifierThread_;
    bool verificationStarted_ = false;

    void doLogin(const LoginData& pkt);
    void verifyLoginSession(LoginData pkt);

    static std::mt19937_64 rng_;
};
