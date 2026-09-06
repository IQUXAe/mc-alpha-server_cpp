#pragma once

#include "NetHandler.h"
#include "RustPackets.h"
#include "../core/Logger.h"
#include "../core/ByteBuffer.h"
#include "alpha_bridge.h"

#include <string>
#include <memory>
#include <vector>
#include <stdexcept>
#include <chrono>

class NetworkManager {
public:
    NetworkManager(int socketFd, const std::string& desc, NetHandler* handler)
        : netHandler_(handler), description_(desc) {
        rustManager_ = rust_network_manager_create(socketFd);
    }

    ~NetworkManager() {
        if (rustManager_) {
            rust_network_manager_destroy(rustManager_);
            rustManager_ = nullptr;
        }
    }

    void setNetHandler(NetHandler* handler) { netHandler_ = handler; }

    void sendPacket(const RustPacket& pkt, bool isChunkData = false) {
        if (!rustManager_) return;
        rust_network_manager_send_packet(rustManager_, &pkt, isChunkData);
    }

    void sendRaw(const uint8_t* data, size_t len, bool isChunkData = false) {
        if (!rustManager_) return;
        rust_network_manager_send(rustManager_, data, len, isChunkData);
    }

    void processReadPackets() {
        if (!rustManager_) return;

        if (rust_network_manager_get_send_queue_length(rustManager_) > 1048576) {
            shutdown("Send buffer overflow");
            return;
        }

        bool polledAny = false;
        RustPacket* ffiPacket = nullptr;
        size_t packetCount = 0;
        constexpr size_t kMaxPacketsPerTick = 50;

        while ((ffiPacket = rust_network_manager_poll_parsed(rustManager_)) != nullptr) {
            polledAny = true;
            if (!netHandler_) {
                rust_network_manager_free_packet(ffiPacket);
                shutdown("Missing network handler");
                return;
            }
            if (++packetCount > kMaxPacketsPerTick) {
                rust_network_manager_free_packet(ffiPacket);
                shutdown("Rate limit exceeded");
                return;
            }
            try {
                netHandler_->processPacket(*ffiPacket);
            } catch (const std::exception& e) {
                Logger::warning("Failed to process packet: {} - {}", ffiPacket->packet_id, e.what());
                netHandler_->handleErrorMessage("Packet processing error: " + std::string(e.what()));
            }
            rust_network_manager_free_packet(ffiPacket);
        }

        if (!polledAny) {
            if (netHandler_ && netHandler_->shouldBypassReadTimeout()) {
                timeSinceLastRead_ = 0;
            } else if (++timeSinceLastRead_ >= 1200) {
                shutdown("Timed out");
            }
        } else {
            timeSinceLastRead_ = 0;
        }

        if (rust_network_manager_is_terminating(rustManager_)) {
            char reasonBuf[256];
            rust_network_manager_get_termination_reason(rustManager_, reasonBuf, sizeof(reasonBuf));
            if (netHandler_) {
                netHandler_->handleErrorMessage(std::string(reasonBuf));
            }
        }
    }

    void shutdown(const std::string& reason) {
        if (rustManager_) {
            rust_network_manager_shutdown(rustManager_, reason.c_str());
        }
    }

    void serverShutdown() {
        if (rustManager_) {
            rust_network_manager_server_shutdown(rustManager_);
        }
    }

    [[nodiscard]] std::string getRemoteAddress() const { return remoteAddress_; }
    void setRemoteAddress(const std::string& addr) { remoteAddress_ = addr; }
    [[nodiscard]] int getNumChunkDataPackets() const { return 0; }
    [[nodiscard]] bool isRunning() const {
        return rustManager_ && rust_network_manager_is_running(rustManager_);
    }

private:
    RustNetworkManager* rustManager_ = nullptr;
    NetHandler* netHandler_;
    std::string description_;
    std::string remoteAddress_;
    int timeSinceLastRead_ = 0;
};
