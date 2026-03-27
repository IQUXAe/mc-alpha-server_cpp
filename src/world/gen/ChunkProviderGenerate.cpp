#include "ChunkProviderGenerate.h"
#include "block/Block.h"
#include "Decorators.h"
#include "core/Logger.h"
#include <cmath>

ChunkProviderGenerate::ChunkProviderGenerate(World* world, int64_t seed, WorldChunkManager* managerOverride)
    : worldObj(world), rand(seed),
      field_705_k(rand, 16),
      field_704_l(rand, 16),
      field_703_m(rand, 8),
      field_702_n(rand, 4),
      field_701_o(rand, 4),
      field_715_a(rand, 10),
      field_714_b(rand, 16),
      field_713_c(rand, 8),
      chunkManager(managerOverride ? managerOverride : world->func_4077_a()) {
    // Initialize noise arrays
    field_4224_q.reserve(17 * 17 * 17);
}

// Exact port of Java's func_4058_a (density field generation)
void ChunkProviderGenerate::func_4058_a(int var2, int var3, int var4, int var5, int var6, int var7,
                                         const std::vector<double>& temperatures, const std::vector<double>& humidities) {
    if (field_4224_q.size() < (size_t)(var5 * var6 * var7)) {
        field_4224_q.resize(var5 * var6 * var7);
    }

    double var8 = 684.412;
    double var10 = 684.412;

    field_4226_g = field_715_a.func_4103_a(field_4226_g, var2, var4, var5, var7, 1.121, 1.121, 0.5);
    field_4225_h = field_714_b.func_4103_a(field_4225_h, var2, var4, var5, var7, 200.0, 200.0, 0.5);
    field_4229_d = field_703_m.func_648_a(field_4229_d, (double)var2, (double)var3, (double)var4, var5, var6, var7, var8 / 80.0, var10 / 160.0, var8 / 80.0);
    field_4228_e = field_705_k.func_648_a(field_4228_e, (double)var2, (double)var3, (double)var4, var5, var6, var7, var8, var10, var8);
    field_4227_f = field_704_l.func_648_a(field_4227_f, (double)var2, (double)var3, (double)var4, var5, var6, var7, var8, var10, var8);

    int var14 = 0;
    int var15 = 0;
    int var16 = 16 / var5;

    for (int var17 = 0; var17 < var5; ++var17) {
        int var18 = var17 * var16 + var16 / 2;
        for (int var19 = 0; var19 < var7; ++var19) {
            int var20 = var19 * var16 + var16 / 2;
            double var21 = temperatures[var18 * 16 + var20];
            double var23 = humidities[var18 * 16 + var20] * var21;
            double var25 = 1.0 - var23;
            var25 *= var25;
            var25 *= var25;
            var25 = 1.0 - var25;
            double var27 = (field_4226_g[var15] + 256.0) / 512.0;
            var27 *= var25;
            if (var27 > 1.0) var27 = 1.0;

            double var29 = field_4225_h[var15] / 8000.0;
            if (var29 < 0.0) var29 = -var29 * 0.3;

            var29 = var29 * 3.0 - 2.0;
            if (var29 < 0.0) {
                var29 /= 2.0;
                if (var29 < -1.0) var29 = -1.0;
                var29 /= 1.4;
                var29 /= 2.0;
                var27 = 0.0;
            } else {
                if (var29 > 1.0) var29 = 1.0;
                var29 /= 8.0;
            }

            if (var27 < 0.0) var27 = 0.0;

            var27 += 0.5;
            var29 = var29 * (double)var6 / 16.0;
            double var31 = (double)var6 / 2.0 + var29 * 4.0;
            ++var15;

            for (int var33 = 0; var33 < var6; ++var33) {
                double var34 = 0.0;
                double var36 = ((double)var33 - var31) * 12.0 / var27;
                if (var36 < 0.0) var36 *= 4.0;

                double var38 = field_4228_e[var14] / 512.0;
                double var40 = field_4227_f[var14] / 512.0;
                double var42 = (field_4229_d[var14] / 10.0 + 1.0) / 2.0;

                if (var42 < 0.0) var34 = var38;
                else if (var42 > 1.0) var34 = var40;
                else var34 = var38 + (var40 - var38) * var42;

                var34 -= var36;
                if (var33 > var6 - 4) {
                    double var44 = (double)((float)(var33 - (var6 - 4)) / 3.0f);
                    var34 = var34 * (1.0 - var44) + -10.0 * var44;
                }

                field_4224_q[var14] = var34;
                ++var14;
            }
        }
    }
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

    // Java signature: func_648_a(double[] d, double x, double y, double z, int w, int h, int d, ...)
    // field_698_r: x=chunkX*16, z=chunkZ*16 (horizontal noise)
    field_698_r = field_702_n.func_648_a(field_698_r, chunkX * 16, chunkZ * 16, 0, 16, 16, 1, var6, var6, 1.0);
    // field_697_s: x=chunkZ*16, z=chunkX*16 (SWAPPED! Java uses var2, var1 order here)
    field_697_s = field_702_n.func_648_a(field_697_s, chunkZ * 16, 109.0134, chunkX * 16, 16, 1, 16, var6, 1.0, var6);
    field_696_t = field_701_o.func_648_a(field_696_t, chunkX * 16, chunkZ * 16, 0, 16, 16, 1, var6 * 2.0, var6 * 2.0, var6 * 2.0);

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

    // Cave generation
    MapGenCaves caveGen;
    caveGen.func_667_a(this, worldObj, chunkX, chunkZ, blocks);

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

    // Exact Java Random seed sequence
    rand.setSeed(worldObj->randomSeed);
    int64_t var7 = rand.nextLong() / 2LL * 2LL + 1LL;
    int64_t var9 = rand.nextLong() / 2LL * 2LL + 1LL;
    rand.setSeed(((int64_t)chunkX * var7 + (int64_t)chunkZ * var9) ^ worldObj->randomSeed);

    // --- Water lakes ---
    if (rand.nextInt(4) == 0) {
        int var13 = var4 + rand.nextInt(16) + 8;
        int var14 = rand.nextInt(128);
        int var15 = var5 + rand.nextInt(16) + 8;
        WorldGenLakes(8).generate(worldObj, rand, var13, var14, var15); // waterMoving
    }

    // --- Lava lakes ---
    if (rand.nextInt(8) == 0) {
        int var13 = var4 + rand.nextInt(16) + 8;
        int var14 = rand.nextInt(rand.nextInt(120) + 8);
        int var15 = var5 + rand.nextInt(16) + 8;
        if (var14 < 64 || rand.nextInt(10) == 0) {
            WorldGenLakes(10).generate(worldObj, rand, var13, var14, var15); // lavaMoving
        }
    }

    // --- Dungeons (8 attempts) ---
    for (int i = 0; i < 8; ++i) {
        int dx = var4 + rand.nextInt(16) + 8;
        int dy = rand.nextInt(128);
        int dz = var5 + rand.nextInt(16) + 8;
        WorldGenDungeons().generate(worldObj, rand, dx, dy, dz);
    }

    // --- Clay (10 veins) ---
    for (int i = 0; i < 10; ++i) {
        int cx = var4 + rand.nextInt(16);
        int cy = rand.nextInt(128);
        int cz = var5 + rand.nextInt(16);
        WorldGenClay(32).generate(worldObj, rand, cx, cy, cz);
    }

    // --- Dirt veins (20) ---
    for (int i = 0; i < 20; ++i) {
        int mx = var4 + rand.nextInt(16);
        int my = rand.nextInt(128);
        int mz = var5 + rand.nextInt(16);
        WorldGenMinable(3, 32).generate(worldObj, rand, mx, my, mz); // dirt
    }

    // --- Gravel veins (10) ---
    for (int i = 0; i < 10; ++i) {
        int mx = var4 + rand.nextInt(16);
        int my = rand.nextInt(128);
        int mz = var5 + rand.nextInt(16);
        WorldGenMinable(13, 32).generate(worldObj, rand, mx, my, mz); // gravel
    }

    // --- Coal ore (20) ---
    for (int i = 0; i < 20; ++i) {
        int mx = var4 + rand.nextInt(16);
        int my = rand.nextInt(128);
        int mz = var5 + rand.nextInt(16);
        WorldGenMinable(16, 16).generate(worldObj, rand, mx, my, mz);
    }

    // --- Iron ore (20, max y=64) ---
    for (int i = 0; i < 20; ++i) {
        int mx = var4 + rand.nextInt(16);
        int my = rand.nextInt(64);
        int mz = var5 + rand.nextInt(16);
        WorldGenMinable(15, 8).generate(worldObj, rand, mx, my, mz);
    }

    // --- Gold ore (2, max y=32) ---
    for (int i = 0; i < 2; ++i) {
        int mx = var4 + rand.nextInt(16);
        int my = rand.nextInt(32);
        int mz = var5 + rand.nextInt(16);
        WorldGenMinable(14, 8).generate(worldObj, rand, mx, my, mz);
    }

    // --- Redstone ore (8, max y=16) ---
    for (int i = 0; i < 8; ++i) {
        int mx = var4 + rand.nextInt(16);
        int my = rand.nextInt(16);
        int mz = var5 + rand.nextInt(16);
        WorldGenMinable(73, 7).generate(worldObj, rand, mx, my, mz);
    }

    // --- Diamond ore (1, max y=16) ---
    for (int i = 0; i < 1; ++i) {
        int mx = var4 + rand.nextInt(16);
        int my = rand.nextInt(16);
        int mz = var5 + rand.nextInt(16);
        WorldGenMinable(56, 7).generate(worldObj, rand, mx, my, mz);
    }

    // --- Trees ---
    double var11d = 0.5;
    int var13t = (int)((field_713_c.func_647_a((double)var4 * var11d, (double)var5 * var11d) / 8.0 + rand.nextDouble() * 4.0 + 4.0) / 3.0);
    int var14t = 0;
    if (rand.nextInt(10) == 0) {
        ++var14t;
    }

    if (var6 == MobSpawnerBase::forest) var14t += var13t + 5;
    if (var6 == MobSpawnerBase::rainforest) var14t += var13t + 5;
    if (var6 == MobSpawnerBase::seasonalForest) var14t += var13t + 2;
    if (var6 == MobSpawnerBase::taiga) var14t += var13t + 5;
    if (var6 == MobSpawnerBase::desert) var14t -= 20;
    if (var6 == MobSpawnerBase::tundra) var14t -= 20;
    if (var6 == MobSpawnerBase::plains) var14t -= 20;

    // Tree type selection
    bool useBigTree = false;
    if (rand.nextInt(10) == 0) {
        useBigTree = true;
    }
    if (var6 == MobSpawnerBase::rainforest && rand.nextInt(3) == 0) {
        useBigTree = true;
    }

    for (int i = 0; i < var14t; ++i) {
        int tx = var4 + rand.nextInt(16) + 8;
        int tz = var5 + rand.nextInt(16) + 8;
        int ty = worldObj->getHeightValue(tx, tz);
        if (useBigTree) {
            WorldGenBigTree().generate(worldObj, rand, tx, ty, tz);
        } else {
            WorldGenTrees().generate(worldObj, rand, tx, ty, tz);
        }
    }

    // --- Yellow flowers (2 attempts) ---
    for (int i = 0; i < 2; ++i) {
        int fx = var4 + rand.nextInt(16) + 8;
        int fy = rand.nextInt(128);
        int fz = var5 + rand.nextInt(16) + 8;
        WorldGenFlowers(37).generate(worldObj, rand, fx, fy, fz);
    }

    // --- Red flower (50% chance) ---
    if (rand.nextInt(2) == 0) {
        int fx = var4 + rand.nextInt(16) + 8;
        int fy = rand.nextInt(128);
        int fz = var5 + rand.nextInt(16) + 8;
        WorldGenFlowers(38).generate(worldObj, rand, fx, fy, fz);
    }

    // --- Brown mushroom (25% chance) ---
    if (rand.nextInt(4) == 0) {
        int fx = var4 + rand.nextInt(16) + 8;
        int fy = rand.nextInt(128);
        int fz = var5 + rand.nextInt(16) + 8;
        WorldGenFlowers(39).generate(worldObj, rand, fx, fy, fz);
    }

    // --- Red mushroom (12.5% chance) ---
    if (rand.nextInt(8) == 0) {
        int fx = var4 + rand.nextInt(16) + 8;
        int fy = rand.nextInt(128);
        int fz = var5 + rand.nextInt(16) + 8;
        WorldGenFlowers(40).generate(worldObj, rand, fx, fy, fz);
    }

    // --- Reed / Sugar cane (10 attempts) ---
    for (int i = 0; i < 10; ++i) {
        int rx = var4 + rand.nextInt(16) + 8;
        int ry = rand.nextInt(128);
        int rz = var5 + rand.nextInt(16) + 8;
        WorldGenReed().generate(worldObj, rand, rx, ry, rz);
    }

    // --- Pumpkin (1/32 chance) ---
    if (rand.nextInt(32) == 0) {
        int px = var4 + rand.nextInt(16) + 8;
        int py = rand.nextInt(128);
        int pz = var5 + rand.nextInt(16) + 8;
        WorldGenPumpkin().generate(worldObj, rand, px, py, pz);
    }

    // --- Cactus (desert only, 10 per chunk) ---
    int cactusCount = 0;
    if (var6 == MobSpawnerBase::desert) {
        cactusCount += 10;
    }
    for (int i = 0; i < cactusCount; ++i) {
        int cx = var4 + rand.nextInt(16) + 8;
        int cy = rand.nextInt(128);
        int cz = var5 + rand.nextInt(16) + 8;
        WorldGenCactus().generate(worldObj, rand, cx, cy, cz);
    }

    // --- Underground water springs (50) ---
    for (int i = 0; i < 50; ++i) {
        int sx = var4 + rand.nextInt(16) + 8;
        int sy = rand.nextInt(rand.nextInt(120) + 8);
        int sz = var5 + rand.nextInt(16) + 8;
        WorldGenLiquids(9).generate(worldObj, rand, sx, sy, sz); // waterStill
    }

    // --- Underground lava springs (20) ---
    for (int i = 0; i < 20; ++i) {
        int sx = var4 + rand.nextInt(16) + 8;
        int sy = rand.nextInt(rand.nextInt(rand.nextInt(112) + 8) + 8);
        int sz = var5 + rand.nextInt(16) + 8;
        WorldGenLiquids(11).generate(worldObj, rand, sx, sy, sz); // lavaStill
    }

    // --- Snow ---
    field_4222_w = chunkManager->getTemperatures(field_4222_w, var4 + 8, var5 + 8, 16, 16);

    for (int var17 = var4 + 8; var17 < var4 + 8 + 16; ++var17) {
        for (int var18 = var5 + 8; var18 < var5 + 8 + 16; ++var18) {
            int var19 = var17 - (var4 + 8);
            int var20 = var18 - (var5 + 8);
            int var21 = worldObj->getHeightValue(var17, var18);
            double var22 = field_4222_w[var19 * 16 + var20] - (double)(var21 - 64) / 64.0 * 0.3;

            if (var22 < 0.5 && var21 > 0 && var21 < 128 &&
                worldObj->getBlockId(var17, var21, var18) == 0 &&
                worldObj->isBlockSolid(var17, var21 - 1, var18) &&
                worldObj->getBlockId(var17, var21 - 1, var18) != 79) { // not ice
                worldObj->setBlock(var17, var21, var18, 78); // snow layer
            }
        }
    }
}
