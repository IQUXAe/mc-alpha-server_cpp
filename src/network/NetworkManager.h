#pragma once

#include "Packet.h"
#include "NetHandler.h"
#include "packets/AllPackets.h"
#include "../core/Logger.h"

#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <string>
#include <memory>
#include <stdexcept>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class NetworkManager {
public:
    NetworkManager(int socketFd, const std::string& desc, NetHandler* handler)
        : socketFd_(socketFd), netHandler_(handler), description_(desc) {
        // Start read and write threads
        readThread_ = std::thread([this]() { readLoop(); });
        writeThread_ = std::thread([this]() { writeLoop(); });
    }

    ~NetworkManager() {
        shutdown("Destructor");
        if (readThread_.joinable()) readThread_.join();
        if (writeThread_.joinable()) writeThread_.join();
    }

    void setNetHandler(NetHandler* handler) {
        netHandler_ = handler;
    }

    void addToSendQueue(std::unique_ptr<Packet> pkt) {
        if (isServerTerminating_) return;
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueueByteLength_ += pkt->getPacketSize() + 1;
        if (pkt->isChunkDataPacket) {
            chunkDataPackets_.push(std::move(pkt));
        } else {
            dataPackets_.push(std::move(pkt));
        }
    }

    // Convenience: create and queue a packet
    template<typename T, typename... Args>
    void sendPacket(Args&&... args) {
        addToSendQueue(std::make_unique<T>(std::forward<Args>(args)...));
    }

    void processReadPackets() {
        if (sendQueueByteLength_ > 1048576) {
            shutdown("Send buffer overflow");
        }

        std::vector<std::unique_ptr<Packet>> toProcess;
        {
            std::lock_guard<std::mutex> lock(readMutex_);
            if (readPackets_.empty()) {
                if (++timeSinceLastRead_ >= 1200) {
                    shutdown("Timed out");
                }
            } else {
                timeSinceLastRead_ = 0;
            }

            int count = 100;
            while (!readPackets_.empty() && count-- >= 0) {
                toProcess.push_back(std::move(readPackets_.front()));
                readPackets_.pop();
            }
        }

        for (auto& pkt : toProcess) {
            try {
                pkt->processPacket(*netHandler_);
            } catch (const std::exception& e) {
                Logger::warning("Failed to process packet: {} - {}", pkt->getPacketId(), e.what());
                // Kick the player on packet processing error
                if (netHandler_) {
                    netHandler_->handleErrorMessage("Packet processing error: " + std::string(e.what()));
                }
            }
        }

        if (isTerminating_) {
            std::lock_guard<std::mutex> lock(readMutex_);
            if (readPackets_.empty()) {
                netHandler_->handleErrorMessage(terminationReason_);
            }
        }
    }

    void shutdown(const std::string& reason) {
        if (!isRunning_) return;
        isTerminating_ = true;
        terminationReason_ = reason;
        isRunning_ = false;

        if (socketFd_ >= 0) {
            ::shutdown(socketFd_, SHUT_RDWR);
            ::close(socketFd_);
            socketFd_ = -1;
        }
    }

    void serverShutdown() {
        isServerTerminating_ = true;
        // Wait a bit for remaining packets to send, then close
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            shutdown("Server shutdown");
        }).detach();
    }

    std::string getRemoteAddress() const {
        return remoteAddress_;
    }

    void setRemoteAddress(const std::string& addr) {
        remoteAddress_ = addr;
    }

    int getNumChunkDataPackets() const {
        return static_cast<int>(chunkDataPackets_.size());
    }

    bool isRunning() const { return isRunning_; }

private:
    int socketFd_;
    NetHandler* netHandler_;
    std::string description_;
    std::string remoteAddress_;

    std::atomic<bool> isRunning_{true};
    std::atomic<bool> isServerTerminating_{false};
    std::atomic<bool> isTerminating_{false};
    std::string terminationReason_;

    std::mutex readMutex_;
    std::queue<std::unique_ptr<Packet>> readPackets_;

    std::mutex sendMutex_;
    std::queue<std::unique_ptr<Packet>> dataPackets_;
    std::queue<std::unique_ptr<Packet>> chunkDataPackets_;
    std::atomic<int> sendQueueByteLength_{0};
    int chunkDataSendCounter_ = 0;
    int timeSinceLastRead_ = 0;

    std::thread readThread_;
    std::thread writeThread_;

    void readLoop() {
        while (isRunning_) {
            try {
                auto pkt = Packet::readPacket(socketFd_);
                if (!pkt) {
                    shutdown("End of stream");
                    return;
                }
                std::lock_guard<std::mutex> lock(readMutex_);
                readPackets_.push(std::move(pkt));
            } catch (const std::exception& e) {
                if (!isTerminating_) {
                    Logger::warning("Read error on {}: {}", description_, e.what());
                    shutdown("Internal exception: " + std::string(e.what()));
                }
                return;
            }
        }
    }

    void writeLoop() {
        while (isRunning_ || isServerTerminating_) {
            try {
                bool idle = true;

                // Drain all pending data packets first (movement, chat, block updates, etc.)
                while (true) {
                    std::unique_ptr<Packet> pkt;
                    {
                        std::lock_guard<std::mutex> lock(sendMutex_);
                        if (dataPackets_.empty()) break;
                        pkt = std::move(dataPackets_.front());
                        dataPackets_.pop();
                        sendQueueByteLength_ -= pkt->getPacketSize() + 1;
                    }
                    Packet::writePacket(*pkt, socketFd_);
                    idle = false;
                }

                // Send one chunk packet per iteration (rate limited to avoid flooding)
                {
                    std::unique_ptr<Packet> pkt;
                    {
                        std::lock_guard<std::mutex> lock(sendMutex_);
                        if (!chunkDataPackets_.empty()) {
                            pkt = std::move(chunkDataPackets_.front());
                            chunkDataPackets_.pop();
                            sendQueueByteLength_ -= pkt->getPacketSize() + 1;
                            idle = false;
                        }
                    }
                    if (pkt) Packet::writePacket(*pkt, socketFd_);
                }

                if (idle) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            } catch (const std::exception& e) {
                if (!isTerminating_) {
                    Logger::warning("Write error on {}: {}", description_, e.what());
                    shutdown("Internal exception: " + std::string(e.what()));
                }
                return;
            }
        }
    }
};
