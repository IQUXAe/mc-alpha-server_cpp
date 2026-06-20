#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <atomic>
#include "../core/NibbleArray.h"
#include "../forward.h"

class TileEntity;

// Alpha 1.2.6 Chunk dimensions
constexpr int CHUNK_SIZE_X = 16;
constexpr int CHUNK_SIZE_Y = 128;
constexpr int CHUNK_SIZE_Z = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z; // 32768
constexpr int CHUNK_AREA = CHUNK_SIZE_X * CHUNK_SIZE_Z; // 256

// Serialized EntityItem data stored inside a chunk.
// age counts only while the chunk is loaded — frozen on unload, resumed on load.
struct ChunkEntityData {
    int itemID;
    int count;
    int metadata;
    int age;         // ticks alive (max 6000 = 5 min)
    int pickupDelay;
    double posX, posY, posZ;
};

struct ChunkAnimalData {
    std::string id;
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    float rotationYaw, rotationPitch;
    int16_t health, maxHealth;
    bool saddled = false, sheared = false;
    int eggLayTime = 0;
};

struct ChunkBoatData {
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    float rotationYaw, rotationPitch;
    int timeSinceHit, damageTaken, forwardDirection;
};

class Chunk {
public:
    const int xPosition;
    const int zPosition;
    World* worldObj;

    bool isTerrainPopulated = false;
    // std::atomic<bool> provides an implicit `operator=(bool)` that
    // performs an atomic store with sequentially-consistent semantics, so
    // direct `chunk->isModified = true` is safe from any thread.
    // Reads should still use `.load()` for clarity, but `if (!chunk->isModified)`
    // also works via the contextual-bool conversion. The atomicity matters
    // because:
    //   * Async chunk build thread (ChunkProviderGenerate) sets dirty after decoration
    //   * Save thread reads dirty to skip clean chunks
    //   * World::tick loads + clears dirty during the auto-save window
    std::atomic<bool> isModified{false};

    // EntityItems waiting to be spawned when the chunk is loaded
    std::vector<ChunkEntityData> pendingItems;
    std::vector<ChunkAnimalData> pendingAnimals;
    std::vector<ChunkAnimalData> pendingMonsters;
    std::vector<ChunkBoatData> pendingBoats;

    // Direct flat arrays allocated in Rust and kept alive there (zero-copy)
    uint8_t* blocks = nullptr;        // 32768 bytes
    NibbleArray data;                 // 16384 bytes
    NibbleArray skylight;             // 16384 bytes
    NibbleArray blocklight;           // 16384 bytes
    uint8_t* heightMap = nullptr;     // 256 bytes

    Chunk(World* world, int x, int z);
    Chunk(World* world, int x, int z, uint8_t* raw_blocks, uint8_t* raw_data, uint8_t* raw_sky, uint8_t* raw_blocklight, uint8_t* raw_height);
    ~Chunk();

    // The index formula matching Alpha exactly: x << 11 | z << 7 | y
    inline int getIndex(int x, int y, int z) const {
        return (x << 11) | (z << 7) | y;
    }

    uint8_t getBlockID(int x, int y, int z) const;
    bool setBlockID(int x, int y, int z, uint8_t blockID);
    bool setBlockIDWithMetadata(int x, int y, int z, uint8_t blockID, uint8_t metadata);
    
    uint8_t getBlockMetadata(int x, int y, int z) const;
    void setBlockMetadata(int x, int y, int z, uint8_t metadata);

    uint8_t getSavedLightValue(int type, int x, int y, int z) const;
    void setLightValue(int type, int x, int y, int z, uint8_t value);

    int getHeightValue(int x, int z) const;
    void generateHeightMap();

    void generateSkylightMap();

    // Packing into Packet51MapChunk format (81920 bytes)
    std::vector<uint8_t> getChunkData() const;

    // TileEntity management
    void addTileEntity(TileEntity* te);
    void removeTileEntity(int x, int y, int z);
    TileEntity* getTileEntity(int x, int y, int z) const;
    const std::unordered_map<uint64_t, TileEntity*>& getTileEntities() const { return tileEntities_; }

private:
    void recalculateHeightColumn(int x, int z);
    void updateSkylightColumn(int x, int z, int startY, int endY);
    std::unordered_map<uint64_t, TileEntity*> tileEntities_; // Key: (x << 16) | (y << 8) | z
    
    inline uint64_t getTileEntityKey(int x, int y, int z) const {
        return (static_cast<uint64_t>(x) << 16) | (static_cast<uint64_t>(y) << 8) | static_cast<uint64_t>(z);
    }
};
