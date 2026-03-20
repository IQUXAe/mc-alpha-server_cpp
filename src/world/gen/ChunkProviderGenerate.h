#pragma once

#include "world/Chunk.h"
#include "world/World.h"
#include "world/noise/AlphaNoise.h"
#include "world/biome/WorldChunkManager.h"
#include "world/biome/MobSpawnerBase.h"
#include "core/JavaRandom.h"
#include <vector>

class ChunkProviderGenerate {
private:
    World* worldObj;  // Must be first for correct initialization order
    JavaRandom rand;
    NoiseGeneratorOctaves field_705_k;
    NoiseGeneratorOctaves field_704_l;
    NoiseGeneratorOctaves field_703_m;
    NoiseGeneratorOctaves field_702_n;
    NoiseGeneratorOctaves field_701_o;
    NoiseGeneratorOctaves field_715_a;
    NoiseGeneratorOctaves field_714_b;
    NoiseGeneratorOctaves field_713_c;

    std::vector<double> field_4224_q;
    std::vector<double> field_698_r;
    std::vector<double> field_697_s;
    std::vector<double> field_696_t;

    std::vector<double> field_4229_d;
    std::vector<double> field_4228_e;
    std::vector<double> field_4227_f;
    std::vector<double> field_4226_g;
    std::vector<double> field_4225_h;

    std::vector<MobSpawnerBase> biomesForGeneration;
    std::vector<double> field_4222_w;

    WorldChunkManager* chunkManager; // Not owned, from World

    void func_4058_a(int x, int y, int z, int xSize, int ySize, int zSize);
    void generateTerrain(int chunkX, int chunkZ, std::vector<uint8_t>& blocks,
                         std::vector<MobSpawnerBase>& biomes, std::vector<double>& temps);
    void replaceBlocksForBiome(int chunkX, int chunkZ, std::vector<uint8_t>& blocks,
                               std::vector<MobSpawnerBase>& biomes);

public:
    ChunkProviderGenerate(World* world, int64_t seed);
    Chunk* provideChunk(int chunkX, int chunkZ);
    void populate(int chunkX, int chunkZ);
};
