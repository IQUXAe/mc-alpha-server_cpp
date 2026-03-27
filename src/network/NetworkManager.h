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
#include <condition_variable>
#include <chrono>
#include <optional>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>

class NetworkManager {
public:
    NetworkManager(int socketFd, const std::string& desc, NetHandler* handler)
        : socketFd_(socketFd), netHandler_(handler), description_(desc) {
        const int enabled = 1;
        ::setsockopt(socketFd_, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
        ::setsockopt(socketFd_, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));

        readThread_  = std::jthread([this](std::stop_token st) { readLoop(st); });
        writeThread_ = std::jthread([this](std::stop_token st) { writeLoop(st); });
    }

    ~NetworkManager() {
        shutdown("Destructor");
    }

    void setNetHandler(NetHandler* handler) { netHandler_ = handler; }

    void addToSendQueue(std::unique_ptr<Packet> pkt) {
        if (isServerTerminating_) return;
        {
            std::lock_guard lock(sendMutex_);
            sendQueueByteLength_ += pkt->getPacketSize() + 1;
            if (pkt->isChunkDataPacket) {
                chunkDataPackets_.push(std::move(pkt));
            } else {
                dataPackets_.push(std::move(pkt));
            }
        }
        sendCondition_.notify_one();
    }

    void processReadPackets() {
        if (sendQueueByteLength_.load(std::memory_order_relaxed) > 1048576) {
            shutdown("Send buffer overflow");
        }

        std::vector<std::unique_ptr<Packet>> toProcess;
        {
            std::lock_guard lock(readMutex_);
            if (readPackets_.empty()) {
                if (++timeSinceLastRead_ >= 1200) shutdown("Timed out");
            } else {
                timeSinceLastRead_ = 0;
            }
            for (int count = 100; !readPackets_.empty() && count-- > 0; ) {
                toProcess.push_back(std::move(readPackets_.front()));
                readPackets_.pop();
            }
        }

        for (auto& pkt : toProcess) {
            try {
                pkt->processPacket(*netHandler_);
            } catch (const std::exception& e) {
                Logger::warning("Failed to process packet: {} - {}", pkt->getPacketId(), e.what());
                if (netHandler_) netHandler_->handleErrorMessage("Packet processing error: " + std::string(e.what()));
            }
        }

        if (isTerminating_) {
            std::lock_guard lock(readMutex_);
            if (readPackets_.empty() && netHandler_) netHandler_->handleErrorMessage(terminationReason_);
        }
    }

    void shutdown(const std::string& reason) {
        if (!isRunning_.exchange(false)) return;
        isTerminating_ = true;
        terminationReason_ = reason;
        serverShutdownDeadline_.reset();
        if (socketFd_ >= 0) {
            ::shutdown(socketFd_, SHUT_RDWR);
            ::close(socketFd_);
            socketFd_ = -1;
        }
        readThread_.request_stop();
        writeThread_.request_stop();
        sendCondition_.notify_all();
    }

    void serverShutdown() {
        isServerTerminating_ = true;
        serverShutdownDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        sendCondition_.notify_all();
    }

    [[nodiscard]] std::string getRemoteAddress() const { return remoteAddress_; }
    void setRemoteAddress(const std::string& addr) { remoteAddress_ = addr; }
    [[nodiscard]] int getNumChunkDataPackets() const { return static_cast<int>(chunkDataPackets_.size()); }
    [[nodiscard]] bool isRunning() const { return isRunning_; }

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
    std::condition_variable sendCondition_;
    std::queue<std::unique_ptr<Packet>> dataPackets_;
    std::queue<std::unique_ptr<Packet>> chunkDataPackets_;
    std::atomic<int> sendQueueByteLength_{0};
    int timeSinceLastRead_ = 0;
    std::optional<std::chrono::steady_clock::time_point> serverShutdownDeadline_;

    std::jthread readThread_;
    std::jthread writeThread_;

    void readLoop(std::stop_token st) {
        while (!st.stop_requested() && isRunning_) {
            try {
                auto pkt = Packet::readPacket(socketFd_);
                if (!pkt) { shutdown("End of stream"); return; }
                std::lock_guard lock(readMutex_);
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

    void writeLoop(std::stop_token st) {
        while (!st.stop_requested()) {
            try {
                std::unique_ptr<Packet> pkt;
                {
                    std::unique_lock lock(sendMutex_);
                    sendCondition_.wait_for(lock, std::chrono::milliseconds(50), [&] {
                        return st.stop_requested()
                            || !dataPackets_.empty()
                            || !chunkDataPackets_.empty()
                            || !isRunning_.load(std::memory_order_relaxed)
                            || serverShutdownDeadline_.has_value();
                    });

                    if (st.stop_requested()) {
                        return;
                    }

                    if (!dataPackets_.empty()) {
                        pkt = std::move(dataPackets_.front());
                        dataPackets_.pop();
                    } else if (!chunkDataPackets_.empty()) {
                        pkt = std::move(chunkDataPackets_.front());
                        chunkDataPackets_.pop();
                    } else if (!isRunning_.load(std::memory_order_relaxed)) {
                        return;
                    }
                }

                if (pkt) {
                    sendQueueByteLength_ -= pkt->getPacketSize() + 1;
                    Packet::writePacket(*pkt, socketFd_);
                }

                if (serverShutdownDeadline_) {
                    const auto deadline = *serverShutdownDeadline_;
                    if (std::chrono::steady_clock::now() >= deadline) {
                        bool queueEmpty = false;
                        {
                            std::lock_guard lock(sendMutex_);
                            queueEmpty = dataPackets_.empty() && chunkDataPackets_.empty();
                        }
                        if (queueEmpty) {
                            shutdown("Server shutdown");
                            return;
                        }
                    }
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
