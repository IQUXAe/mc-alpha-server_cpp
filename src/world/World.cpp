#include "World.h"
#include "../block/Block.h"
#include "../entity/Entity.h"
#include "../core/AxisAlignedBB.h"
#include "../MinecraftServer.h"
#include <iostream>
#include <cmath>

World::World(MinecraftServer* server, const std::string& savePath)
    : mcServer(server), worldPath_(savePath) {
    randomSeed = time(nullptr); // Basic seed

    // Initialize WorldChunkManager (biome system)
    worldChunkManager = std::make_unique<WorldChunkManager>(randomSeed);

    // Initialize exactly same as alpha
    chunkProvider = std::make_unique<ChunkProviderGenerate>(this, randomSeed);

    std::cout << "[INFO] World initialized with seed " << randomSeed << std::endl;
}

World::~World() = default;

void World::tick() {
    worldTime++;

    // Unload chunks far from all players periodically
    if (worldTime % 100 == 0) {
        int r = 0;
        if (mcServer) {
            r = mcServer->viewDistance + 2; // Generation radius mapping
        }

        std::vector<uint64_t> toUnload;
        for (const auto& [key, chunk] : chunks_) {
            int cx = static_cast<int>(static_cast<int32_t>(key >> 32));
            int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));

            bool keep = false;
            if (mcServer && mcServer->configManager) {
                for (auto* player : mcServer->configManager->playerEntities) {
                    int px = static_cast<int>(std::floor(player->posX)) >> 4;
                    int pz = static_cast<int>(std::floor(player->posZ)) >> 4;
                    
                    if (std::abs(cx - px) <= r && std::abs(cz - pz) <= r) {
                        keep = true;
                        break;
                    }
                }
            } else {
                keep = true; // Don't unload if no server attached yet
            }

            // Spawn chunks protection could be added here (e.g. keep around 0,0)
            if (std::abs(cx) <= 3 && std::abs(cz) <= 3) {
                keep = true;
            }

            if (!keep) {
                toUnload.push_back(key);
            }
        }

        for (uint64_t key : toUnload) {
            chunks_.erase(key);
        }
    }
}

Chunk* World::getChunk(int chunkX, int chunkZ, bool generate) {
    auto key = getChunkKey(chunkX, chunkZ);
    auto it = chunks_.find(key);
    if (it != chunks_.end()) return it->second.get();

    if (!generate) return nullptr;

    // Use ChunkProviderGenerate instead of inline procedural generator
    Chunk* ptr = chunkProvider->provideChunk(chunkX, chunkZ);
    chunks_[key] = std::unique_ptr<Chunk>(ptr);

    // Instead of forcing THIS chunk to populate immediately,
    // we evaluate if any of the surrounding chunks are now ready to populate.
    // A chunk (px, pz) is ready to populate if:
    // (px, pz), (px+1, pz), (px, pz+1), (px+1, pz+1) all exist.
    // AND (px, pz) is NOT populated yet.
    // When (chunkX, chunkZ) generates, it might complete the 2x2 square for:
    // (chunkX, chunkZ), (chunkX-1, chunkZ), (chunkX, chunkZ-1), (chunkX-1, chunkZ-1)

    auto checkPopulate = [&](int px, int pz) {
        auto itP = chunks_.find(getChunkKey(px, pz));
        if (itP == chunks_.end()) return;
        Chunk* c = itP->second.get();
        if (c && !c->isTerrainPopulated) {
            if (chunkExists(px + 1, pz) && chunkExists(px, pz + 1) && chunkExists(px + 1, pz + 1)) {
                c->isTerrainPopulated = true;
                chunkProvider->populate(px, pz);
                // Recalculate lighting for the chunk AFTER trees/ores are placed
                c->generateSkylightMap();
            }
        }
    };

    checkPopulate(chunkX, chunkZ);
    checkPopulate(chunkX - 1, chunkZ);
    checkPopulate(chunkX, chunkZ - 1);
    checkPopulate(chunkX - 1, chunkZ - 1);

    return ptr;
}

Chunk* World::getChunkFromBlockCoords(int x, int z, bool generate) {
    return getChunk(x >> 4, z >> 4, generate);
}

bool World::chunkExists(int chunkX, int chunkZ) const {
    return chunks_.find(getChunkKey(chunkX, chunkZ)) != chunks_.end();
}

uint8_t World::getBlockId(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 0;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return 0;

    return chunk->getBlockID(x & 15, y, z & 15);
}

uint8_t World::getBlockMetadata(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 0;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return 0;

    return chunk->getBlockMetadata(x & 15, y, z & 15);
}

bool World::setBlock(int x, int y, int z, uint8_t blockId) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return false;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return false;

    bool placed = chunk->setBlockIDWithMetadata(x & 15, y, z & 15, blockId, 0);
    return placed;
}

bool World::setBlockWithNotify(int x, int y, int z, uint8_t blockId) {
    if (!setBlock(x, y, z, blockId)) return false;

    markBlockNeedsUpdate(x, y, z);
    notifyBlocksOfNeighborChange(x, y, z, blockId);
    return true;
}

bool World::setBlockAndMetadata(int x, int y, int z, uint8_t blockId, uint8_t metadata) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return false;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return false;

    return chunk->setBlockIDWithMetadata(x & 15, y, z & 15, blockId, metadata);
}

bool World::setBlockAndMetadataWithNotify(int x, int y, int z, uint8_t blockId, uint8_t metadata) {
    if (!setBlockAndMetadata(x, y, z, blockId, metadata)) return false;

    markBlockNeedsUpdate(x, y, z);
    notifyBlocksOfNeighborChange(x, y, z, blockId);
    return true;
}

void World::markBlockNeedsUpdate(int x, int y, int z) {
    if (mcServer && mcServer->configManager) {
        mcServer->configManager->broadcastPacket(
            std::make_unique<Packet53BlockChange>(x, y, z, getBlockId(x, y, z), getBlockMetadata(x, y, z))
        );
    }
}

void World::notifyBlocksOfNeighborChange(int x, int y, int z, uint8_t blockId) {
    auto notify = [&](int nx, int ny, int nz) {
        uint8_t nId = getBlockId(nx, ny, nz);
        if (nId > 0 && Block::blocksList[nId]) {
            Block::blocksList[nId]->onNeighborBlockChange(this, nx, ny, nz, blockId);
        }
    };

    notify(x - 1, y, z);
    notify(x + 1, y, z);
    notify(x, y - 1, z);
    notify(x, y + 1, z);
    notify(x, y, z - 1);
    notify(x, y, z + 1);
}

// Get height at world coordinates (top non-air block Y)
int World::getHeightValue(int x, int z) {
    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return 0;
    return chunk->getHeightValue(x & 15, z & 15);
}

// Check if block at position is solid (for snow placement etc.)
bool World::isBlockSolid(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return false;
    uint8_t id = getBlockId(x, y, z);
    if (id == 0) return false;
    // Non-solid blocks: air(0), water(8,9), lava(10,11), snow layers(78), flowers(37,38),
    // mushrooms(39,40), reeds(83), fire(51), etc.
    if (id == 8 || id == 9 || id == 10 || id == 11) return false;
    if (id == 78 || id == 37 || id == 38 || id == 39 || id == 40) return false;
    if (id == 83 || id == 51 || id == 6) return false;
    return true;
}

void World::getCollidingBoundingBoxes(Entity* entity, const AxisAlignedBB& mask, std::vector<AxisAlignedBB>& result) {
    int minX = std::floor(mask.minX);
    int maxX = std::floor(mask.maxX + 1.0);
    int minY = std::floor(mask.minY);
    int maxY = std::floor(mask.maxY + 1.0);
    int minZ = std::floor(mask.minZ);
    int maxZ = std::floor(mask.maxZ + 1.0);

    for (int x = minX; x < maxX; ++x) {
        for (int y = minY; y < maxY; ++y) {
            for (int z = minZ; z < maxZ; ++z) {
                uint8_t bId = getBlockId(x, y, z);
                if (bId > 0 && Block::blocksList[bId]) {
                    Block::blocksList[bId]->getCollidingBoundingBoxes(this, x, y, z, mask, result);
                }
            }
        }
    }
}
