#include "ChunkProviderGenerate.h"
#include "block/Block.h"
#include "core/Logger.h"
#include <cmath>
#include <thread>

static thread_local World* current_world = nullptr;

ChunkProviderGenerate::ChunkProviderGenerate(World* world, int64_t seed, WorldChunkManager* managerOverride)
    : worldObj(world), rand(seed),
      chunkManager(managerOverride ? managerOverride : world->func_4077_a()) {
    
    alpha_noise_octaves_create_all(
        seed,
        &field_705_k,
        &field_704_l,
        &field_703_m,
        &field_702_n,
        &field_701_o,
        &field_715_a,
        &field_714_b,
        &field_713_c
    );
    
    // Initialize noise arrays
    field_4224_q.reserve(17 * 17 * 17);
}

ChunkProviderGenerate::~ChunkProviderGenerate() {
    alpha_noise_octaves_free(field_705_k);
    alpha_noise_octaves_free(field_704_l);
    alpha_noise_octaves_free(field_703_m);
    alpha_noise_octaves_free(field_702_n);
    alpha_noise_octaves_free(field_701_o);
    alpha_noise_octaves_free(field_715_a);
    alpha_noise_octaves_free(field_714_b);
    alpha_noise_octaves_free(field_713_c);
}

// Exact port of Java's func_4058_a (density field generation)
void ChunkProviderGenerate::func_4058_a(int var2, int var3, int var4, int var5, int var6, int var7,
                                         const std::vector<double>& temperatures, const std::vector<double>& humidities) {
    if (field_4224_q.size() < (size_t)(var5 * var6 * var7)) {
        field_4224_q.resize(var5 * var6 * var7);
    }

    alpha_density_generate_field(
        field_4224_q.data(),
        field_4224_q.size(),
        var2,
        var3,
        var4,
        var5,
        var6,
        var7,
        temperatures.data(),
        humidities.data(),
        field_715_a,
        field_714_b,
        field_703_m,
        field_705_k,
        field_704_l
    );
}

// Exact port of Java's generateTerrain
void ChunkProviderGenerate::generateTerrain(int chunkX, int chunkZ, std::vector<uint8_t>& blocks,
                                             std::vector<MobSpawnerBase>& biomes, std::vector<double>& temps) {
    int var6 = 4;
    int var7 = 64;
    int var8 = var6 + 1;
    int var9 = 17;
    int var10 = var6 + 1;

    func_4058_a(chunkX * var6, 0, chunkZ * var6, var8, var9, var10, temps, chunkManager->humidity);

    for (int var11 = 0; var11 < var6; ++var11) {
        for (int var12 = 0; var12 < var6; ++var12) {
            for (int var13 = 0; var13 < 16; ++var13) {
                double var14 = 0.125;
                double var16 = field_4224_q[((var11 + 0) * var10 + var12 + 0) * var9 + var13 + 0];
                double var18 = field_4224_q[((var11 + 0) * var10 + var12 + 1) * var9 + var13 + 0];
                double var20 = field_4224_q[((var11 + 1) * var10 + var12 + 0) * var9 + var13 + 0];
                double var22 = field_4224_q[((var11 + 1) * var10 + var12 + 1) * var9 + var13 + 0];
                double var24 = (field_4224_q[((var11 + 0) * var10 + var12 + 0) * var9 + var13 + 1] - var16) * var14;
                double var26 = (field_4224_q[((var11 + 0) * var10 + var12 + 1) * var9 + var13 + 1] - var18) * var14;
                double var28 = (field_4224_q[((var11 + 1) * var10 + var12 + 0) * var9 + var13 + 1] - var20) * var14;
                double var30 = (field_4224_q[((var11 + 1) * var10 + var12 + 1) * var9 + var13 + 1] - var22) * var14;

                for (int var32 = 0; var32 < 8; ++var32) {
                    double var33 = 0.25;
                    double var35 = var16;
                    double var37 = var18;
                    double var39 = (var20 - var16) * var33;
                    double var41 = (var22 - var18) * var33;

                    for (int var43 = 0; var43 < 4; ++var43) {
                        // Exact Java index: var43 + var11 * 4 << 11 | 0 + var12 * 4 << 7 | var13 * 8 + var32
                        int var44 = ((var43 + var11 * 4) << 11) | ((0 + var12 * 4) << 7) | (var13 * 8 + var32);
                        int var45 = 128; // short
                        double var46 = 0.25;
                        double var48 = var35;
                        double var50 = (var37 - var35) * var46;

                        for (int var52 = 0; var52 < 4; ++var52) {
                            double var53 = temps[(var11 * 4 + var43) * 16 + var12 * 4 + var52];
                            uint8_t var55 = 0;
                            if (var13 * 8 + var32 < var7) {
                                if (var53 < 0.5 && var13 * 8 + var32 >= var7 - 1) {
                                    var55 = 79; // ice
                                } else {
                                    var55 = 8; // waterMoving
                                }
                            }

                            if (var48 > 0.0) {
                                var55 = 1; // stone
                            }

                            blocks[var44] = var55;
                            var44 += var45;
                            var48 += var50;
                        }

                        var35 += var39;
                        var37 += var41;
                    }

                    var16 += var24;
                    var18 += var26;
                    var20 += var28;
                    var22 += var30;
                }
            }
        }
    }
}

// Exact port of Java's replaceBlocksForBiome
void ChunkProviderGenerate::replaceBlocksForBiome(int chunkX, int chunkZ, std::vector<uint8_t>& blocks,
                                                    std::vector<MobSpawnerBase>& biomes) {
    int var5 = 64;
    double var6 = 1.0 / 32.0;

    field_698_r.resize(256);
    alpha_noise_octaves_func_648_a(field_702_n, field_698_r.data(), field_698_r.size(), chunkX * 16, chunkZ * 16, 0, 16, 16, 1, var6, var6, 1.0);
    
    // field_697_s: x=chunkZ*16, z=chunkX*16 (SWAPPED! Java uses var2, var1 order here)
    field_697_s.resize(256);
    alpha_noise_octaves_func_648_a(field_702_n, field_697_s.data(), field_697_s.size(), chunkZ * 16, 109.0134, chunkX * 16, 16, 1, 16, var6, 1.0, var6);
    
    field_696_t.resize(256);
    alpha_noise_octaves_func_648_a(field_701_o, field_696_t.data(), field_696_t.size(), chunkX * 16, chunkZ * 16, 0, 16, 16, 1, var6 * 2.0, var6 * 2.0, var6 * 2.0);

    for (int var8 = 0; var8 < 16; ++var8) {
        for (int var9 = 0; var9 < 16; ++var9) {
            MobSpawnerBase& var10 = biomes[var8 * 16 + var9];
            bool var11 = field_698_r[var8 + var9 * 16] + rand.nextDouble() * 0.2 > 0.0;
            bool var12 = field_697_s[var8 + var9 * 16] + rand.nextDouble() * 0.2 > 3.0;
            int var13 = (int)(field_696_t[var8 + var9 * 16] / 3.0 + 3.0 + rand.nextDouble() * 0.25);
            int var14 = -1;
            uint8_t var15 = var10.topBlock;
            uint8_t var16 = var10.fillerBlock;

            for (int var17 = 127; var17 >= 0; --var17) {
                int var18 = (var8 * 16 + var9) * 128 + var17;
                if (var17 <= 0 + rand.nextInt(5)) {
                    blocks[var18] = 7; // bedrock
                } else {
                    uint8_t var19 = blocks[var18];
                    if (var19 == 0) {
                        var14 = -1;
                    } else if (var19 == 1) { // stone
                        if (var14 == -1) {
                            if (var13 <= 0) {
                                var15 = 0;
                                var16 = 1;
                            } else if (var17 >= var5 - 4 && var17 <= var5 + 1) {
                                var15 = var10.topBlock;
                                var16 = var10.fillerBlock;
                                if (var12) var15 = 0;
                                if (var12) var16 = 13; // gravel
                                if (var11) var15 = 12; // sand
                                if (var11) var16 = 12; // sand
                            }

                            if (var17 < var5 && var15 == 0) {
                                var15 = 8; // waterMoving
                            }

                            var14 = var13;
                            if (var17 >= var5 - 1) {
                                blocks[var18] = var15;
                            } else {
                                blocks[var18] = var16;
                            }
                        } else if (var14 > 0) {
                            --var14;
                            blocks[var18] = var16;
                        }
                    }
                }
            }
        }
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

    rand.setSeed((int64_t)chunkX * 341873128712LL + (int64_t)chunkZ * 132897987541LL);
    std::vector<uint8_t>& blocks = chunk.blocks;

    // Get biome data from WorldChunkManager
    biomesForGeneration = chunkManager->loadBlockGeneratorData(chunkX * 16, chunkZ * 16, 16, 16);
    std::vector<double> temps = chunkManager->temperature;

    generateTerrain(chunkX, chunkZ, blocks, biomesForGeneration, temps);
    replaceBlocksForBiome(chunkX, chunkZ, blocks, biomesForGeneration);

    // Cave generation (Rust FFI)
    alpha_caves_generate(worldObj->randomSeed, chunkX, chunkZ, blocks.data(), blocks.size());

    // IMPORTANT: Update heightmap AFTER terrain generation and cave carving
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

// Exact port of Java's populate
void ChunkProviderGenerate::populate(int chunkX, int chunkZ) {
    int var4 = chunkX * 16;
    int var5 = chunkZ * 16;

    // Get biome for this chunk region (center of chunk +16, +16)
    MobSpawnerBase var6 = chunkManager->func_4067_a(var4 + 16, var5 + 16);

    // Get temperatures for snow layer
    field_4222_w = chunkManager->getTemperatures(field_4222_w, var4 + 8, var5 + 8, 16, 16);

    current_world = worldObj;

    WorldAccessor accessor {
        .get_block_id = [](int32_t x, int32_t y, int32_t z) -> uint8_t {
            return current_world->getBlockId(x, y, z);
        },
        .set_block_id = [](int32_t x, int32_t y, int32_t z, uint8_t id) {
            current_world->setBlock(x, y, z, id);
        },
        .get_block_meta = [](int32_t x, int32_t y, int32_t z) -> uint8_t {
            return current_world->getBlockMetadata(x, y, z);
        },
        .set_block_meta = [](int32_t x, int32_t y, int32_t z, uint8_t meta) {
            current_world->setBlockMetadata(x, y, z, meta);
        },
        .allows_attachment = [](int32_t x, int32_t y, int32_t z) -> bool {
            int id = current_world->getBlockId(x, y, z);
            return id >= 0 && id < 256 && Block::allowsAttachmentArr[id];
        },
        .is_block_solid = [](int32_t x, int32_t y, int32_t z) -> bool {
            return current_world->isBlockSolid(x, y, z);
        },
        .get_height_value = [](int32_t x, int32_t z) -> int32_t {
            return current_world->getHeightValue(x, z);
        }
    };

    alpha_decorate_chunk(accessor, worldObj->randomSeed, chunkX, chunkZ, static_cast<int32_t>(var6.type), field_713_c, field_4222_w.data());

    current_world = nullptr;
}
