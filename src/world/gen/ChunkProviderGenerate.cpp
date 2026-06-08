#include "ChunkProviderGenerate.h"
#include "block/Block.h"
#include "core/Logger.h"
#include <cmath>

static thread_local World* current_world = nullptr;

ChunkProviderGenerate::ChunkProviderGenerate(World* world, int64_t seed, WorldChunkManager* managerOverride)
    : worldObj(world), rand(seed),
      chunkManager(managerOverride ? managerOverride : world->func_4077_a()) {
    
    rustGen_ = rust_chunk_provider_generate_create(seed);
}

ChunkProviderGenerate::~ChunkProviderGenerate() {
    if (rustGen_) {
        rust_chunk_provider_generate_destroy(rustGen_);
        rustGen_ = nullptr;
    }
}

// Check if chunk can be populated (all 3 neighbors exist)
bool ChunkProviderGenerate::canPopulateChunk(int chunkX, int chunkZ) {
    return worldObj->chunkExists(chunkX + 1, chunkZ) &&
           worldObj->chunkExists(chunkX, chunkZ + 1) &&
           worldObj->chunkExists(chunkX + 1, chunkZ + 1);
}

void ChunkProviderGenerate::generateChunk(Chunk& chunk, bool generateLighting) {
    const int chunkX = chunk.xPosition;
    const int chunkZ = chunk.zPosition;

    // Get biome data from WorldChunkManager
    biomesForGeneration = chunkManager->loadBlockGeneratorData(chunkX * 16, chunkZ * 16, 16, 16);
    std::vector<double>& temps = chunkManager->temperature;
    std::vector<double>& humids = chunkManager->humidity;

    rust_chunk_provider_generate_chunk(
        rustGen_,
        chunkX,
        chunkZ,
        chunk.blocks.data(),
        biomesForGeneration.data(),
        temps.data(),
        humids.data()
    );

    // Update heightmap AFTER terrain generation and cave carving
    chunk.generateHeightMap();
    if (generateLighting) {
        chunk.generateSkylightMap();
    }
}

// Generate terrain for existing chunk (modified to work with pre-created chunk)
void ChunkProviderGenerate::provideChunk(int chunkX, int chunkZ) {
    Chunk* chunk = worldObj->getChunk(chunkX, chunkZ, false);
    if (!chunk) return;
    generateChunk(*chunk, true);
}

// Exact port of Java's populate delegating decoration to Rust with 2x2 chunk batch
void ChunkProviderGenerate::populate(int chunkX, int chunkZ) {
    int var4 = chunkX * 16;
    int var5 = chunkZ * 16;

    // Get biome for this chunk region (center of chunk +16, +16)
    MobSpawnerBase var6 = chunkManager->func_4067_a(var4 + 16, var5 + 16);

    // Get temperatures for snow layer
    field_4222_w = chunkManager->getTemperatures(field_4222_w, var4 + 8, var5 + 8, 16, 16);

    current_world = worldObj;

    Chunk* c00 = worldObj->getChunk(chunkX, chunkZ, false);
    Chunk* c01 = worldObj->getChunk(chunkX, chunkZ + 1, false);
    Chunk* c10 = worldObj->getChunk(chunkX + 1, chunkZ, false);
    Chunk* c11 = worldObj->getChunk(chunkX + 1, chunkZ + 1, false);

    if (c00 && c01 && c10 && c11) {
        RustChunkDataBatch batch;
        batch.chunks[0] = { c00->blocks.data(), c00->data.data.data(), c00->xPosition, c00->zPosition };
        batch.chunks[1] = { c01->blocks.data(), c01->data.data.data(), c01->xPosition, c01->zPosition };
        batch.chunks[2] = { c10->blocks.data(), c10->data.data.data(), c10->xPosition, c10->zPosition };
        batch.chunks[3] = { c11->blocks.data(), c11->data.data.data(), c11->xPosition, c11->zPosition };

        WorldAccessor accessor {
            .get_block_id = [](int32_t x, int32_t y, int32_t z) -> uint8_t {
                if (y < 0 || y >= 128) return 0;
                Chunk* chunk = current_world->getLoadedChunkFromBlockCoords(x, z);
                if (!chunk) return 0;
                return chunk->getBlockID(x & 15, y, z & 15);
            },
            .set_block_id = [](int32_t x, int32_t y, int32_t z, uint8_t id) {
                if (y < 0 || y >= 128) return;
                Chunk* chunk = current_world->getLoadedChunkFromBlockCoords(x, z);
                if (!chunk) return;
                
                uint8_t oldId = chunk->getBlockID(x & 15, y, z & 15);
                bool placed = chunk->setBlockIDWithMetadata(x & 15, y, z & 15, id, 0);
                if (placed) {
                    current_world->markBlockNeedsUpdate(x, y, z);
                    if (oldId > 0 && Block::blocksList[oldId])
                        Block::blocksList[oldId]->onBlockRemoval(current_world, x, y, z);
                    if (id > 0 && Block::blocksList[id])
                        Block::blocksList[id]->onBlockAdded(current_world, x, y, z);
                    current_world->notifyBlocksOfNeighborChange(x, y, z, id);
                }
            },
            .get_block_meta = [](int32_t x, int32_t y, int32_t z) -> uint8_t {
                if (y < 0 || y >= 128) return 0;
                Chunk* chunk = current_world->getLoadedChunkFromBlockCoords(x, z);
                if (!chunk) return 0;
                return chunk->getBlockMetadata(x & 15, y, z & 15);
            },
            .set_block_meta = [](int32_t x, int32_t y, int32_t z, uint8_t meta) {
                if (y < 0 || y >= 128) return;
                Chunk* chunk = current_world->getLoadedChunkFromBlockCoords(x, z);
                if (!chunk) return;
                
                uint8_t id = chunk->getBlockID(x & 15, y, z & 15);
                chunk->setBlockMetadata(x & 15, y, z & 15, meta);
                chunk->isModified = true;
                
                current_world->markBlockNeedsUpdate(x, y, z);
                current_world->notifyBlocksOfNeighborChange(x, y, z, id);
            },
            .allows_attachment = [](int32_t x, int32_t y, int32_t z) -> bool {
                if (y < 0 || y >= 128) return false;
                Chunk* chunk = current_world->getLoadedChunkFromBlockCoords(x, z);
                if (!chunk) return false;
                int id = chunk->getBlockID(x & 15, y, z & 15);
                return id >= 0 && id < 256 && Block::allowsAttachmentArr[id];
            },
            .is_block_solid = [](int32_t x, int32_t y, int32_t z) -> bool {
                return current_world->isBlockSolidNoChunkLoad(x, y, z);
            },
            .get_height_value = [](int32_t x, int32_t z) -> int32_t {
                Chunk* chunk = current_world->getLoadedChunkFromBlockCoords(x, z);
                if (!chunk) return 0;
                return chunk->getHeightValue(x & 15, z & 15);
            }
        };

        rust_chunk_provider_populate_batch(
            rustGen_,
            &batch,
            accessor,
            chunkX,
            chunkZ,
            static_cast<int32_t>(var6.type),
            field_4222_w.data()
        );

        c00->isModified = true;
        c01->isModified = true;
        c10->isModified = true;
        c11->isModified = true;
    }

    current_world = nullptr;
}
