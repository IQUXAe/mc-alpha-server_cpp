#include "Chunk.h"
#include "World.h"
#include "TileEntity.h"
#include <stdexcept>
#include <algorithm>
#include <ranges>
#include <queue>

struct LightNode {
    int x, y, z; // Global world coordinates
};

Chunk::Chunk(World* world, int x, int z) 
    : xPosition(x), zPosition(z), worldObj(world),
      blocks(CHUNK_VOLUME, 0),
      data(CHUNK_VOLUME),
      skylight(CHUNK_VOLUME),
      blocklight(CHUNK_VOLUME),
      heightMap(CHUNK_AREA, 0) {
    // Everything is air initially.
}

Chunk::~Chunk() {
    // TileEntities are owned by World, not Chunk
    tileEntities_.clear();
}

uint8_t Chunk::getBlockID(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_SIZE_Y || x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) return 0;
    return blocks[getIndex(x, y, z)];
}

bool Chunk::setBlockID(int x, int y, int z, uint8_t blockID) {
    int id = getBlockID(x, y, z);
    if (id == blockID) return false;

    blocks[getIndex(x, y, z)] = blockID;
    generateHeightMap();
    if (worldObj && !worldObj->isPopulating) isModified = true;
    return true;
}

bool Chunk::setBlockIDWithMetadata(int x, int y, int z, uint8_t blockID, uint8_t metadata) {
    int oldId = getBlockID(x, y, z);
    if (oldId == blockID && data.getNibble(x, y, z) == metadata) return false;

    blocks[getIndex(x, y, z)] = blockID;
    data.setNibble(x, y, z, metadata);
    
    int oldHeight = heightMap[(z << 4) | x];
    if (blockID != 0) {
        if (y >= oldHeight) heightMap[(z << 4) | x] = static_cast<uint8_t>(y + 1);
    } else {
        if (y == oldHeight - 1) {
            // Recalculate height
            int newY = y;
            while (newY > 0 && blocks[getIndex(x, newY - 1, z)] == 0) --newY;
            heightMap[(z << 4) | x] = static_cast<uint8_t>(newY);
        }
    }

    // Basic Skylight update
    if (worldObj && !worldObj->isPopulating) {
        isModified = true;
        generateHeightMap();   // Update heightmap first
        generateSkylightMap(); // Then update lighting based on new heights
    }
    return true;
}

uint8_t Chunk::getBlockMetadata(int x, int y, int z) const {
    return data.getNibble(x, y, z);
}

void Chunk::setBlockMetadata(int x, int y, int z, uint8_t metadata) {
    data.setNibble(x, y, z, metadata);
    if (worldObj && !worldObj->isPopulating) isModified = true;
}

uint8_t Chunk::getSavedLightValue(int type, int x, int y, int z) const {
    // 0 = sky, 1 = block
    if (type == 0) return skylight.getNibble(x, y, z);
    return blocklight.getNibble(x, y, z);
}

void Chunk::setLightValue(int type, int x, int y, int z, uint8_t value) {
    if (type == 0) skylight.setNibble(x, y, z, value);
    else blocklight.setNibble(x, y, z, value);
    if (worldObj && !worldObj->isPopulating) isModified = true;
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
    std::queue<LightNode> skyQueue;
    std::queue<LightNode> blockQueue;

    // Fast local transparency check
    auto isTransparent = [](uint8_t id) {
        if (id == 0) return true; // Air
        if (id == 8 || id == 9 || id == 10 || id == 11) return true; // Liquids
        if (id == 78 || id == 37 || id == 38 || id == 39 || id == 40) return true; // Snow, flowers, mushrooms
        if (id == 83 || id == 51 || id == 6) return true; // Reeds, fire, sapling
        if (id == 18 || id == 20) return true; // Leaves and glass
        return false; 
    };

    int globalX = xPosition * 16;
    int globalZ = zPosition * 16;

    // STEP 1: Vertical initial pass for Skylight and detection of light emitters
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            int currentSky = 15;
            for (int y = 127; y >= 0; --y) {
                uint8_t id = getBlockID(x, y, z);
                
                if (!isTransparent(id)) {
                    currentSky = 0; // Solid block blocks sunlight
                } else if (id == 18 || id == 8 || id == 9 || id == 10 || id == 11) {
                    currentSky -= 3; // Leaves and liquids attenuate light
                }
                
                if (currentSky < 0) currentSky = 0;
                skylight.setNibble(x, y, z, currentSky);
                
                // Add to queue if has skylight using GLOBAL coordinates
                if (currentSky > 0) skyQueue.push({globalX + x, y, globalZ + z});

                // Block light emission (torches, lava, etc)
                int emission = 0;
                if (id == 10 || id == 11 || id == 51 || id == 89 || id == 91) emission = 15;
                else if (id == 50) emission = 14;
                else if (id == 74) emission = 9;
                else if (id == 76) emission = 7;
                
                blocklight.setNibble(x, y, z, emission);
                if (emission > 0) blockQueue.push({globalX + x, y, globalZ + z});
            }
        }
    }

    // STEP 2: Global Breadth-First Spread for Skylight
    while (!skyQueue.empty()) {
        LightNode node = skyQueue.front();
        skyQueue.pop();

        Chunk* currentChunk = worldObj->getChunkFromBlockCoords(node.x, node.z, false);
        if (!currentChunk) continue;

        int currentLight = currentChunk->getSavedLightValue(0, node.x & 15, node.y, node.z & 15);
        if (currentLight <= 1) continue;

        auto spread = [&](int nx, int ny, int nz) {
            if (ny >= 0 && ny < 128) {
                Chunk* neighborChunk = worldObj->getChunkFromBlockCoords(nx, nz, false);
                if (!neighborChunk) return;

                uint8_t id = neighborChunk->getBlockID(nx & 15, ny, nz & 15);
                if (!isTransparent(id)) return;

                int opacity = 1;
                if (id == 18 || id == 8 || id == 9 || id == 10 || id == 11) opacity = 3;

                int neighborLight = neighborChunk->getSavedLightValue(0, nx & 15, ny, nz & 15);
                int newLight = currentLight - opacity;
                if (newLight > neighborLight) {
                    neighborChunk->setLightValue(0, nx & 15, ny, nz & 15, newLight);
                    skyQueue.push({nx, ny, nz});
                }
            }
        };

        spread(node.x - 1, node.y, node.z); spread(node.x + 1, node.y, node.z);
        spread(node.x, node.y - 1, node.z); spread(node.x, node.y + 1, node.z);
        spread(node.x, node.y, node.z - 1); spread(node.x, node.y, node.z + 1);
    }

    // STEP 3: Global Breadth-First Spread for Blocklight
    while (!blockQueue.empty()) {
        LightNode node = blockQueue.front();
        blockQueue.pop();

        Chunk* currentChunk = worldObj->getChunkFromBlockCoords(node.x, node.z, false);
        if (!currentChunk) continue;

        int currentLight = currentChunk->getSavedLightValue(1, node.x & 15, node.y, node.z & 15);
        if (currentLight <= 1) continue;

        auto spread = [&](int nx, int ny, int nz) {
            if (ny >= 0 && ny < 128) {
                Chunk* neighborChunk = worldObj->getChunkFromBlockCoords(nx, nz, false);
                if (!neighborChunk) return;

                uint8_t id = neighborChunk->getBlockID(nx & 15, ny, nz & 15);
                if (!isTransparent(id)) return;

                int opacity = 1;
                if (id == 18 || id == 8 || id == 9 || id == 10 || id == 11) opacity = 3;

                int neighborLight = neighborChunk->getSavedLightValue(1, nx & 15, ny, nz & 15);
                int newLight = currentLight - opacity;
                if (newLight > neighborLight) {
                    neighborChunk->setLightValue(1, nx & 15, ny, nz & 15, newLight);
                    blockQueue.push({nx, ny, nz});
                }
            }
        };

        spread(node.x - 1, node.y, node.z); spread(node.x + 1, node.y, node.z);
        spread(node.x, node.y - 1, node.z); spread(node.x, node.y + 1, node.z);
        spread(node.x, node.y, node.z - 1); spread(node.x, node.y, node.z + 1);
    }
}

#include <zlib.h>

std::vector<uint8_t> Chunk::getChunkData() const {
    std::vector<uint8_t> rawData(CHUNK_VOLUME * 5 / 2, 0);

    std::ranges::copy(blocks,           rawData.begin());
    std::ranges::copy(data.data,        rawData.begin() + CHUNK_VOLUME);
    std::ranges::copy(blocklight.data,  rawData.begin() + CHUNK_VOLUME + CHUNK_VOLUME / 2);
    std::ranges::copy(skylight.data,    rawData.begin() + CHUNK_VOLUME + CHUNK_VOLUME);

    std::vector<uint8_t> compressed(compressBound(rawData.size()));
    z_stream strm{};
    deflateInit(&strm, 1);
    strm.avail_in  = static_cast<uInt>(rawData.size());
    strm.next_in   = const_cast<Bytef*>(rawData.data());
    strm.avail_out = static_cast<uInt>(compressed.size());
    strm.next_out  = compressed.data();
    deflate(&strm, Z_FINISH);
    compressed.resize(strm.total_out);
    deflateEnd(&strm);
    return compressed;
}

void Chunk::addTileEntity(TileEntity* te) {
    if (!te) return;
    int localX = te->xCoord - (xPosition * 16);
    int localZ = te->zCoord - (zPosition * 16);
    tileEntities_[getTileEntityKey(localX, te->yCoord, localZ)] = te;
}

void Chunk::removeTileEntity(int x, int y, int z) {
    tileEntities_.erase(getTileEntityKey(x, y, z));
}

TileEntity* Chunk::getTileEntity(int x, int y, int z) const {
    auto it = tileEntities_.find(getTileEntityKey(x, y, z));
    return it != tileEntities_.end() ? it->second : nullptr;
}
