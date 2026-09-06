#pragma once

#include "world/Chunk.h"
#include "world/World.h"
#include "../../../rust/alpha_bridge/alpha_bridge.h"
#include "world/biome/WorldChunkManager.h"
#include "world/biome/MobSpawnerBase.h"
#include <vector>

class ChunkProviderGenerate {
private:
    World* worldObj;  // Must be first for correct initialization order
    
    RustChunkProviderGenerate* rustGen_;
    std::vector<MobSpawnerBase> biomesForGeneration;
    std::vector<double> field_4222_w;
    WorldChunkManager* chunkManager; // Not owned, from World

public:
    ChunkProviderGenerate(World* world, int64_t seed, WorldChunkManager* managerOverride = nullptr);
    ~ChunkProviderGenerate();
    void generateChunk(Chunk& chunk, bool generateLighting = true);
    void populate(int chunkX, int chunkZ);
};
