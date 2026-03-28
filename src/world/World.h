#pragma once

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cstdint>
#include <vector>
#include <functional>
#include "Chunk.h"
#include "gen/ChunkProviderGenerate.h"
#include "biome/WorldChunkManager.h"
#include "../core/NBT.h"
#include "../forward.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <compare>
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
    void requestChunkAsync(int chunkX, int chunkZ, int priority = 1);
    void spawnHostileMobs();
    void spawnPassiveMobs();
    int countHostileMobs() const;
    int countPassiveAnimals() const;
    EntityPlayerMP* getClosestPlayer(double x, double y, double z, double maxDistance) const;
    int getBlockLightValue(int x, int y, int z);
    int getSavedLightValue(int type, int x, int y, int z);
    bool canBlockSeeSky(int x, int y, int z);
    bool isDaytime() const;

    // Block access and modification
    uint8_t getBlockId(int x, int y, int z);
    uint8_t getBlockIdNoChunkLoad(int x, int y, int z);
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
    bool isBlockSolidNoChunkLoad(int x, int y, int z);
    Material* getBlockMaterial(int x, int y, int z);
    Material* getBlockMaterialNoChunkLoad(int x, int y, int z);

    // Entity integration
    void getCollidingBoundingBoxes(Entity* entity, const AxisAlignedBB& mask, std::vector<AxisAlignedBB>& result);
    void getEntitiesWithinAABBExcludingEntity(Entity* entity, const AxisAlignedBB& mask, std::vector<Entity*>& result);
    void spawnEntityInWorld(std::unique_ptr<Entity> entity);
    void removeEntity(Entity* entity);

    void saveWorld();

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
    void prewarmSpawnArea();
    void saveWorker(std::stop_token st);
    void chunkWorker(std::stop_token st);
    bool loadLevelDat();
    void saveLevelDat();
    std::vector<uint8_t> compressChunkData(Chunk* chunk);
    void decompressChunkData(Chunk* chunk, const std::vector<uint8_t>& data,
                             std::vector<std::unique_ptr<TileEntity>>* detachedTileEntities = nullptr);
    void populateChunkIfReady(int chunkX, int chunkZ);
    void tryPopulateChunksAround(int chunkX, int chunkZ);
    void drainPreparedChunks(size_t maxChunks = 8);
    bool commitPreparedChunk(uint64_t key);

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

    struct PreparedChunk {
        std::unique_ptr<Chunk> chunk;
        std::vector<std::unique_ptr<TileEntity>> tileEntities;
    };
    struct ChunkBuildRequest {
        uint64_t key = 0;
        int priority = 0;
        uint64_t sequence = 0;
    };
    struct ChunkBuildRequestCompare {
        bool operator()(const ChunkBuildRequest& lhs, const ChunkBuildRequest& rhs) const {
            if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
            }
            return lhs.sequence > rhs.sequence;
        }
    };
    std::priority_queue<ChunkBuildRequest, std::vector<ChunkBuildRequest>, ChunkBuildRequestCompare> chunkBuildQueue_;
    std::unordered_set<uint64_t> chunkBuildInFlight_;
    std::unordered_map<uint64_t, PreparedChunk> preparedChunks_;
    std::queue<uint64_t> preparedChunkOrder_;
    std::mutex chunkBuildMutex_;
    std::condition_variable chunkBuildCondition_;
    uint64_t chunkBuildSequence_ = 0;
    std::unique_ptr<WorldChunkManager> asyncChunkManager_;
    std::unique_ptr<ChunkProviderGenerate> asyncChunkProvider_;
    std::jthread chunkBuildThread_;

    leveldb::DB* db_ = nullptr;

    inline uint64_t getChunkKey(int chunkX, int chunkZ) const {
        return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32) |
                static_cast<uint32_t>(chunkZ);
    }

    inline uint64_t getTileEntityKey(int x, int y, int z) const {
        // Pack as: x(32 bits) | y(16 bits) | z(16 bits)
        // y is clamped to [0,255], x/z use full int32 range via reinterpret as uint32
        // To avoid collision on negative z, encode z offset by 32768 into uint16
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               (static_cast<uint64_t>(static_cast<uint8_t>(y)) << 16) |
                static_cast<uint16_t>(static_cast<int16_t>(z & 0xFFFF));
    }
};
