#pragma once

#include <unordered_map>
#include <memory>
#include <cstdint>
#include <vector>
#include <functional>
#include "Chunk.h"
#include "gen/ChunkProviderGenerate.h"
#include "biome/WorldChunkManager.h"
#include "../forward.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <leveldb/db.h>

#include <set>
#include <tuple>
#include <random>

class TileEntity;

struct NextTickListEntry {
    int x, y, z;
    int blockId;
    int64_t scheduledTime;

    bool operator<(const NextTickListEntry& other) const {
        if (scheduledTime != other.scheduledTime) return scheduledTime < other.scheduledTime;
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
    
    bool operator==(const NextTickListEntry& other) const {
        return x == other.x && y == other.y && z == other.z && blockId == other.blockId;
    }
};

class ChunkProviderGenerate;

class World {
public:
    int spawnX = 0;
    int spawnY = 64;
    int spawnZ = 0;
    int64_t randomSeed = 0;
    int64_t worldTime = 0;
    bool isPopulating = false;

    std::unique_ptr<ChunkProviderGenerate> chunkProvider;
    std::unique_ptr<WorldChunkManager> worldChunkManager;
    std::mt19937_64 rand;

    MinecraftServer* mcServer;

    World(MinecraftServer* server, const std::string& savePath, int64_t seed = 0);
    ~World();

    void tick();
    void scheduleBlockUpdate(int x, int y, int z, int blockId, int delay);

    // WorldChunkManager access (func_4077_a in Java)
    WorldChunkManager* func_4077_a() { return worldChunkManager.get(); }

    // Chunk management
    Chunk* getChunk(int chunkX, int chunkZ, bool generate = true);
    Chunk* getChunkFromBlockCoords(int x, int z, bool generate = false);
    bool chunkExists(int chunkX, int chunkZ) const;
    void ensureChunkPopulated(int chunkX, int chunkZ);  // Force population if not already done

    // Block access and modification
    uint8_t getBlockId(int x, int y, int z);
    uint8_t getBlockMetadata(int x, int y, int z);
    bool setBlock(int x, int y, int z, uint8_t blockId);
    bool setBlockWithNotify(int x, int y, int z, uint8_t blockId);
    // Like setBlockWithNotify but skips markBlockNeedsUpdate — caller sends the packet
    bool setBlockWithNotifyNoClientUpdate(int x, int y, int z, uint8_t blockId);
    // Like setBlockWithNotify but without notifying neighbors
    bool setBlockAndUpdate(int x, int y, int z, uint8_t blockId);
    bool setBlockAndMetadata(int x, int y, int z, uint8_t blockId, uint8_t metadata);
    bool setBlockAndMetadataWithNotify(int x, int y, int z, uint8_t blockId, uint8_t metadata);
    bool setBlockMetadata(int x, int y, int z, uint8_t metadata);
    void markBlockNeedsUpdate(int x, int y, int z);
    void notifyBlocksOfNeighborChange(int x, int y, int z, uint8_t neighborId);

    // Height and terrain queries
    int getHeightValue(int x, int z);
    int func_4075_e(int x, int z) { return getHeightValue(x, z); }
    bool isBlockSolid(int x, int y, int z);
    Material* getBlockMaterial(int x, int y, int z);

    // Entity integration
    void getCollidingBoundingBoxes(Entity* entity, const AxisAlignedBB& mask, std::vector<AxisAlignedBB>& result);
    void spawnEntityInWorld(std::unique_ptr<Entity> entity);
    void removeEntity(Entity* entity);

    void saveWorld();
    void loadEntities();
    void saveEntities();

    // TileEntity management
    TileEntity* getTileEntity(int x, int y, int z);
    void setTileEntity(int x, int y, int z, std::unique_ptr<TileEntity> tileEntity);
    void removeTileEntity(int x, int y, int z);
    void markTileEntityChanged(int x, int y, int z, TileEntity* te);
    void saveChunkImmediate(Chunk* chunk);

    // Callback set by MinecraftServer to broadcast Packet59 to nearby players.
    // Matches Java's IWorldAccess.func_686_a -> WorldManager -> configManager.sentTileEntityToPlayer
    std::function<void(int x, int y, int z, TileEntity* te)> onTileEntityChanged;

private:
    void findSafeSpawnPoint();
    void saveWorker(std::stop_token st);
    bool loadLevelDat();
    void saveLevelDat();
    std::vector<uint8_t> compressChunkData(Chunk* chunk);
    void decompressChunkData(Chunk* chunk, const std::vector<uint8_t>& data);

    std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
    mutable std::mutex chunksMutex_;  // Protects chunks_ map from concurrent access
    std::vector<std::unique_ptr<Entity>> entities_;
    std::string worldPath_;
    std::set<NextTickListEntry> scheduledTicks;
    std::unordered_map<uint64_t, std::unique_ptr<TileEntity>> tileEntities_;
    mutable std::mutex tileEntitiesMutex_;

    // Asynchronous saving queue
    struct SaveTask {
        uint64_t key;
        std::vector<uint8_t> data;
    };
    std::queue<SaveTask> saveQueue_;
    std::mutex saveMutex_;
    std::condition_variable saveCondition_;
    std::jthread saveThread_;
    std::atomic<bool> stopSaving_{false};
    leveldb::DB* db_ = nullptr;

    inline uint64_t getChunkKey(int chunkX, int chunkZ) const {
        return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32) |
                static_cast<uint32_t>(chunkZ);
    }

    inline uint64_t getTileEntityKey(int x, int y, int z) const {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               (static_cast<uint64_t>(static_cast<uint16_t>(y)) << 16) |
                static_cast<uint16_t>(z);
    }
};
