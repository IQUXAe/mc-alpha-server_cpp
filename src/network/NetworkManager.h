#pragma once

#include "Packet.h"
#include "NetHandler.h"
#include "packets/AllPackets.h"
#include "../core/Logger.h"
#include "../../rust/alpha_bridge/alpha_bridge.h"

#include <string>
#include <memory>
#include <vector>
#include <stdexcept>

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

    void addToSendQueue(std::unique_ptr<Packet> pkt) {
        if (!rustManager_ || !pkt) return;
        ByteBuffer buf;
        buf.writeUByte(static_cast<uint8_t>(pkt->getPacketId()));
        pkt->writePacketData(buf);
        rust_network_manager_send(rustManager_, buf.data.data(), buf.data.size(), pkt->isChunkDataPacket);
    }

    void processReadPackets() {
        if (!rustManager_) return;

        if (rust_network_manager_get_send_queue_length(rustManager_) > 1048576) {
            shutdown("Send buffer overflow");
            return;
        }

        uint8_t packetId = 0;
        uint8_t* payload = nullptr;
        size_t payloadLen = 0;

        std::vector<std::unique_ptr<Packet>> toProcess;

        // Poll incoming packets from Rust
        bool polledAny = false;
        while (rust_network_manager_poll(rustManager_, &packetId, &payload, &payloadLen)) {
            polledAny = true;
            try {
                auto pkt = Packet::createPacket(static_cast<int>(packetId));
                if (!pkt) {
                    throw std::runtime_error("Bad packet id " + std::to_string(packetId));
                }

                std::vector<uint8_t> payloadVec;
                if (payload && payloadLen > 0) {
                    payloadVec.assign(payload, payload + payloadLen);
                    rust_network_manager_free_buffer(payload, payloadLen);
                    payload = nullptr;
                }
                
                ByteBuffer buf(payloadVec);
                pkt->readPacketData(buf);
                toProcess.push_back(std::move(pkt));
            } catch (const std::exception& e) {
                if (payload) {
                    rust_network_manager_free_buffer(payload, payloadLen);
                }
                Logger::warning("Failed to parse packet ID {}: {}", packetId, e.what());
                shutdown("Packet parsing error: " + std::string(e.what()));
                break;
            }
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

        for (auto& pkt : toProcess) {
            try {
                pkt->processPacket(*netHandler_);
            } catch (const std::exception& e) {
                Logger::warning("Failed to process packet: {} - {}", pkt->getPacketId(), e.what());
                if (netHandler_) {
                    netHandler_->handleErrorMessage("Packet processing error: " + std::string(e.what()));
                }
            }
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
