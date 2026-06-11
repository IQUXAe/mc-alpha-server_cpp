#include <iostream>
#include <chrono>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>

// Mock classes
class Packet {
public:
    virtual ~Packet() = default;
    virtual std::unique_ptr<Packet> clone() const = 0;
};
class Packet53BlockChange : public Packet {
public:
    Packet53BlockChange(int x, int8_t y, int z, int8_t id, int8_t meta) {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet53BlockChange>(0,0,0,0,0); }
};

class NetHandler {
public:
    int playerIdx;
    NetHandler(int idx) : playerIdx(idx) {}
    bool hasChunkLoaded(int64_t key) {
        // Only 5 players have chunk (0,0) loaded
        return playerIdx < 5 && key == chunkKey(0,0);
    }
    int64_t chunkKey(int x, int z) { return ((int64_t)x << 32) | (z & 0xFFFFFFFF); }
    void sendPacket(std::unique_ptr<Packet> pkt) { volatile int a = 1; }
};

class EntityPlayerMP {
public:
    NetHandler* netHandler;
    EntityPlayerMP(int idx) { netHandler = new NetHandler(idx); }
    ~EntityPlayerMP() { delete netHandler; }
};

class ConfigManager {
public:
    std::vector<EntityPlayerMP*> playerEntities;
};

class MinecraftServer {
public:
    ConfigManager* configManager;
    MinecraftServer() { configManager = new ConfigManager(); }
    ~MinecraftServer() { delete configManager; }
};

int main() {
    MinecraftServer* mcServer = new MinecraftServer();
    for (int i = 0; i < 1000; ++i) {
        mcServer->configManager->playerEntities.push_back(new EntityPlayerMP(i));
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10000; ++i) {
        int x = 0, y = 0, z = 0;
        int chunkX = x >> 4;
        int chunkZ = z >> 4;
        auto pkt = std::make_unique<Packet53BlockChange>(x, y, z, 1, 0);

        for (auto* player : mcServer->configManager->playerEntities) {
            if (player->netHandler && player->netHandler->hasChunkLoaded(
                    player->netHandler->chunkKey(chunkX, chunkZ))) {
                player->netHandler->sendPacket(pkt->clone());
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Baseline time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " us" << std::endl;

    std::unordered_map<int64_t, std::vector<EntityPlayerMP*>> chunkToPlayers;
    for (int i=0; i<1000; ++i) {
        auto* player = mcServer->configManager->playerEntities[i];
        if (i < 5) {
            chunkToPlayers[player->netHandler->chunkKey(0, 0)].push_back(player);
        } else {
            chunkToPlayers[player->netHandler->chunkKey(i, i)].push_back(player);
        }
    }

    auto start2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        int x = 0, y = 0, z = 0;
        int chunkX = x >> 4;
        int chunkZ = z >> 4;
        auto pkt = std::make_unique<Packet53BlockChange>(x, y, z, 1, 0);

        auto key = mcServer->configManager->playerEntities[0]->netHandler->chunkKey(chunkX, chunkZ);
        if (chunkToPlayers.count(key)) {
            for (auto* player : chunkToPlayers[key]) {
                player->netHandler->sendPacket(pkt->clone());
            }
        }
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    std::cout << "Optimized time: " << std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2).count() << " us" << std::endl;

    return 0;
}
