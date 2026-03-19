#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include "../core/NibbleArray.h"
#include "../forward.h"

// Alpha 1.2.6 Chunk dimensions
constexpr int CHUNK_SIZE_X = 16;
constexpr int CHUNK_SIZE_Y = 128;
constexpr int CHUNK_SIZE_Z = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z; // 32768
constexpr int CHUNK_AREA = CHUNK_SIZE_X * CHUNK_SIZE_Z; // 256

class Chunk {
public:
    const int xPosition;
    const int zPosition;
    World* worldObj;

    bool isTerrainPopulated = false;
    bool isModified = false;

    // Direct flat arrays for massive CPU cache-locality (C++ advantage)
    std::vector<uint8_t> blocks;      // 32768 bytes
    NibbleArray data;                 // 16384 bytes
    NibbleArray skylight;             // 16384 bytes
    NibbleArray blocklight;           // 16384 bytes
    std::vector<uint8_t> heightMap;   // 256 bytes

    Chunk(World* world, int x, int z);
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

private:
    void updateSkylightColumn(int x, int z, int startY, int endY);
};
