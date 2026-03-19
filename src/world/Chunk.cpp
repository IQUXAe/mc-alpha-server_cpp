#include "Chunk.h"
#include <stdexcept>
#include <cstring>
#include <iostream>

Chunk::Chunk(World* world, int x, int z) 
    : xPosition(x), zPosition(z), worldObj(world),
      blocks(CHUNK_VOLUME, 0),
      data(CHUNK_VOLUME),
      skylight(CHUNK_VOLUME),
      blocklight(CHUNK_VOLUME),
      heightMap(CHUNK_AREA, 0) {
    // Everything is air initially.
}

Chunk::~Chunk() = default;

uint8_t Chunk::getBlockID(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_SIZE_Y || x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) return 0;
    return blocks[getIndex(x, y, z)];
}

bool Chunk::setBlockID(int x, int y, int z, uint8_t blockID) {
    int id = getBlockID(x, y, z);
    if (id == blockID) return false;

    blocks[getIndex(x, y, z)] = blockID;
    generateHeightMap();
    isModified = true;
    return true;
}

bool Chunk::setBlockIDWithMetadata(int x, int y, int z, uint8_t blockID, uint8_t metadata) {
    int id = getBlockID(x, y, z);
    if (id == blockID && data.getNibble(x, y, z) == metadata) return false;

    blocks[getIndex(x, y, z)] = blockID;
    data.setNibble(x, y, z, metadata);
    generateHeightMap();
    isModified = true;
    return true;
}

uint8_t Chunk::getBlockMetadata(int x, int y, int z) const {
    return data.getNibble(x, y, z);
}

void Chunk::setBlockMetadata(int x, int y, int z, uint8_t metadata) {
    data.setNibble(x, y, z, metadata);
    isModified = true;
}

uint8_t Chunk::getSavedLightValue(int type, int x, int y, int z) const {
    // 0 = sky, 1 = block
    if (type == 0) return skylight.getNibble(x, y, z);
    return blocklight.getNibble(x, y, z);
}

void Chunk::setLightValue(int type, int x, int y, int z, uint8_t value) {
    if (type == 0) skylight.setNibble(x, y, z, value);
    else blocklight.setNibble(x, y, z, value);
    isModified = true;
}

int Chunk::getHeightValue(int x, int z) const {
    if (x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) return 0;
    return heightMap[(z << 4) | x];
}

void Chunk::generateHeightMap() {
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int y = CHUNK_SIZE_Y - 1;
            while (y > 0 && blocks[getIndex(x, y - 1, z)] == 0) { // Air
                --y;
            }
            heightMap[(z << 4) | x] = static_cast<uint8_t>(y);
        }
    }
}

void Chunk::generateSkylightMap() {
    generateHeightMap();
    
    // Step 1: Initial pass for Skylight vertically and Blocklight emission
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int currentSky = 15;
            for (int y = 127; y >= 0; --y) {
                uint8_t id = getBlockID(x, y, z);
                
                int opacity = 0;
                if (id == 0 || id == 20 /*glass*/ || id == 50 /*torch*/) opacity = 0; // Wait, light attenuation from moving 1 block is handled in flood fill
                else if (id == 18 /*leaves*/) opacity = 1;
                else if (id == 8 || id == 9 /*water*/) opacity = 3;
                else opacity = 255;
                
                if (opacity > 0) currentSky -= opacity;
                if (currentSky < 0) currentSky = 0;
                
                skylight.setNibble(x, y, z, currentSky);
                
                // Block light emission
                int emission = 0;
                if (id == 10 || id == 11 || id == 51 || id == 89 || id == 91) emission = 15;
                else if (id == 50) emission = 14;
                else if (id == 74) emission = 9;
                else if (id == 76) emission = 7;
                
                blocklight.setNibble(x, y, z, emission);
            }
        }
    }
    
    // Step 2: Flood-fill propagation for BOTH skylight and blocklight
    bool changed;
    int maxIterations = 15;
    do {
        changed = false;
        for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
            for (int x = 0; x < CHUNK_SIZE_X; ++x) {
                for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
                    uint8_t id = getBlockID(x, y, z);
                    int opacity = 1; // Default attenuation is 1 per block
                    if (id == 18) opacity = 1;
                    else if (id == 8 || id == 9) opacity = 3;
                    else if (id != 0 && id != 20 && id != 50 && id != 51 && id != 59) opacity = 255; // solid block
                    
                    int currentSky = skylight.getNibble(x, y, z);
                    int currentBlock = blocklight.getNibble(x, y, z);
                    
                    int maxNeighborSky = currentSky;
                    int maxNeighborBlock = currentBlock;
                    
                    auto checkNeighbors = [&](int nx, int ny, int nz) {
                        if (nx < 0 || nx >= CHUNK_SIZE_X || ny < 0 || ny >= CHUNK_SIZE_Y || nz < 0 || nz >= CHUNK_SIZE_Z) return;
                        int s = skylight.getNibble(nx, ny, nz);
                        if (s > maxNeighborSky) maxNeighborSky = s;
                        int b = blocklight.getNibble(nx, ny, nz);
                        if (b > maxNeighborBlock) maxNeighborBlock = b;
                    };
                    
                    checkNeighbors(x - 1, y, z); checkNeighbors(x + 1, y, z);
                    checkNeighbors(x, y - 1, z); checkNeighbors(x, y + 1, z);
                    checkNeighbors(x, y, z - 1); checkNeighbors(x, y, z + 1);
                    
                    if (opacity < 15) {
                        if (maxNeighborSky > currentSky + opacity) {
                            skylight.setNibble(x, y, z, maxNeighborSky - opacity);
                            changed = true;
                        }
                        if (maxNeighborBlock > currentBlock + opacity) {
                            blocklight.setNibble(x, y, z, maxNeighborBlock - opacity);
                            changed = true;
                        }
                    }
                }
            }
        }
    } while (changed && --maxIterations > 0);
}

#include <zlib.h>

std::vector<uint8_t> Chunk::getChunkData() const {
    std::vector<uint8_t> rawData(CHUNK_VOLUME * 5 / 2, 0); // 81920 bytes
    
    // Memory copy blocks
    std::memcpy(rawData.data(), blocks.data(), CHUNK_VOLUME);
    
    // Memory copy nibbles
    std::memcpy(rawData.data() + CHUNK_VOLUME, data.data.data(), CHUNK_VOLUME / 2);
    std::memcpy(rawData.data() + CHUNK_VOLUME + CHUNK_VOLUME / 2, blocklight.data.data(), CHUNK_VOLUME / 2);
    std::memcpy(rawData.data() + CHUNK_VOLUME + CHUNK_VOLUME, skylight.data.data(), CHUNK_VOLUME / 2);
    
    // Compress raw chunk data using zlib (Deflater level 1, like Java)
    std::vector<uint8_t> compressed(compressBound(rawData.size()));

    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    deflateInit(&strm, 1); // Level 1 = best speed

    strm.avail_in = static_cast<uInt>(rawData.size());
    strm.next_in = const_cast<Bytef*>(rawData.data());
    strm.avail_out = static_cast<uInt>(compressed.size());
    strm.next_out = compressed.data();

    deflate(&strm, Z_FINISH);
    
    size_t compressedSize = strm.total_out;
    deflateEnd(&strm);

    compressed.resize(compressedSize);
    return compressed;
}
