#include "Decorators.h"
#include "../../block/Block.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// ============================================
// MapGenBase
// ============================================
void MapGenBase::func_667_a(ChunkProviderGenerate* provider, World* world, int chunkX, int chunkZ, std::vector<uint8_t>& blocks) {
    int range = this->field_947_a;
    this->field_946_b.setSeed(world->randomSeed);
    long l1 = this->field_946_b.nextLong() / 2LL * 2LL + 1LL;
    long l2 = this->field_946_b.nextLong() / 2LL * 2LL + 1LL;

    for (int x = chunkX - range; x <= chunkX + range; ++x) {
        for (int z = chunkZ - range; z <= chunkZ + range; ++z) {
            this->field_946_b.setSeed(((long)x * l1 + (long)z * l2) ^ world->randomSeed);
            this->func_666_a(world, x, z, chunkX, chunkZ, blocks);
        }
    }
}

void MapGenBase::func_666_a(World* world, int x, int z, int chunkX, int chunkZ, std::vector<uint8_t>& blocks) {}

// ============================================
// MapGenCaves — exact port
// ============================================
void MapGenCaves::func_669_a(int var1, int var2, std::vector<uint8_t>& var3, double var4, double var6, double var8) {
    this->func_668_a(var1, var2, var3, var4, var6, var8, 1.0F + this->field_946_b.nextFloat() * 6.0F, 0.0F, 0.0F, -1, -1, 0.5);
}

void MapGenCaves::func_668_a(int var1, int var2, std::vector<uint8_t>& var3, double var4, double var6, double var8, float var10, float var11, float var12, int var13, int var14, double var15) {
    double var17 = (double)(var1 * 16 + 8);
    double var19 = (double)(var2 * 16 + 8);
    float var21 = 0.0F;
    float var22 = 0.0F;
    JavaRandom var23(this->field_946_b.nextLong());

    if (var14 <= 0) {
        int var24 = this->field_947_a * 16 - 16;
        var14 = var24 - var23.nextInt(var24 / 4);
    }
    bool var52 = false;
    if (var13 == -1) {
        var13 = var14 / 2;
        var52 = true;
    }
    int var25 = var23.nextInt(var14 / 2) + var14 / 4;

    for (bool var26 = var23.nextInt(6) == 0; var13 < var14; ++var13) {
        double var27 = 1.5 + (double)(std::sin((float)var13 * (float)M_PI / (float)var14) * var10 * 1.0F);
        double var29 = var27 * var15;
        float var31 = std::cos(var12);
        float var32 = std::sin(var12);
        var4 += (double)(std::cos(var11) * var31);
        var6 += (double)var32;
        var8 += (double)(std::sin(var11) * var31);

        if (var26) var12 *= 0.92F;
        else var12 *= 0.7F;

        var12 += var22 * 0.1F;
        var11 += var21 * 0.1F;
        var22 *= 0.9F;
        var21 *= 12.0F / 16.0F;
        var22 += (var23.nextFloat() - var23.nextFloat()) * var23.nextFloat() * 2.0F;
        var21 += (var23.nextFloat() - var23.nextFloat()) * var23.nextFloat() * 4.0F;

        if (!var52 && var13 == var25 && var10 > 1.0F) {
            this->func_668_a(var1, var2, var3, var4, var6, var8, var23.nextFloat() * 0.5F + 0.5F, var11 - (float)M_PI * 0.5F, var12 / 3.0F, var13, var14, 1.0);
            this->func_668_a(var1, var2, var3, var4, var6, var8, var23.nextFloat() * 0.5F + 0.5F, var11 + (float)M_PI * 0.5F, var12 / 3.0F, var13, var14, 1.0);
            return;
        }

        if (var52 || var23.nextInt(4) != 0) {
            double var33 = var4 - var17;
            double var35 = var8 - var19;
            double var37 = (double)(var14 - var13);
            double var39 = (double)(var10 + 2.0F + 16.0F);
            if (var33 * var33 + var35 * var35 - var37 * var37 > var39 * var39) return;

            if (var4 >= var17 - 16.0 - var27 * 2.0 && var8 >= var19 - 16.0 - var27 * 2.0 && var4 <= var17 + 16.0 + var27 * 2.0 && var8 <= var19 + 16.0 + var27 * 2.0) {
                int var53 = std::floor(var4 - var27) - var1 * 16 - 1;
                int var34 = std::floor(var4 + var27) - var1 * 16 + 1;
                int var54 = std::floor(var6 - var29) - 1;
                int var36 = std::floor(var6 + var29) + 1;
                int var55 = std::floor(var8 - var27) - var2 * 16 - 1;
                int var38 = std::floor(var8 + var27) - var2 * 16 + 1;

                if (var53 < 0) var53 = 0;
                if (var34 > 16) var34 = 16;
                if (var54 < 1) var54 = 1;
                if (var36 > 120) var36 = 120;
                if (var55 < 0) var55 = 0;
                if (var38 > 16) var38 = 16;

                bool var56 = false;
                for (int var40 = var53; !var56 && var40 < var34; ++var40) {
                    for (int var41 = var55; !var56 && var41 < var38; ++var41) {
                        for (int var42 = var36 + 1; !var56 && var42 >= var54 - 1; --var42) {
                            int var43 = (var40 * 16 + var41) * 128 + var42;
                            if (var42 >= 0 && var42 < 128) {
                                if (var3[var43] == 9 || var3[var43] == 8) { // water
                                    var56 = true;
                                }
                                if (var42 != var54 - 1 && var40 != var53 && var40 != var34 - 1 && var41 != var55 && var41 != var38 - 1) {
                                    var42 = var54;
                                }
                            }
                        }
                    }
                }
                if (!var56) {
                    for (int var40 = var53; var40 < var34; ++var40) {
                        double var57 = ((double)(var40 + var1 * 16) + 0.5 - var4) / var27;
                        for (int var43 = var55; var43 < var38; ++var43) {
                            double var44 = ((double)(var43 + var2 * 16) + 0.5 - var8) / var27;
                            int var46 = (var40 * 16 + var43) * 128 + var36;
                            bool var47 = false;
                            for (int var48 = var36 - 1; var48 >= var54; --var48) {
                                double var49 = ((double)var48 + 0.5 - var6) / var29;
                                if (var49 > -0.7 && var57 * var57 + var49 * var49 + var44 * var44 < 1.0) {
                                    uint8_t var51 = var3[var46];
                                    if (var51 == 2) var47 = true; // grass
                                    if (var51 == 1 || var51 == 3 || var51 == 2) { // stone, dirt, grass
                                        if (var48 < 10) {
                                            var3[var46] = 11; // lavaStill
                                        } else {
                                            var3[var46] = 0;
                                            if (var47 && var3[var46 - 1] == 3) {
                                                var3[var46 - 1] = 2; // grass
                                            }
                                        }
                                    }
                                }
                                --var46;
                            }
                        }
                    }
                    if (var52) break;
                }
            }
        }
    }
}

void MapGenCaves::func_666_a(World* world, int x, int z, int chunkX, int chunkZ, std::vector<uint8_t>& blocks) {
    int var7 = this->field_946_b.nextInt(this->field_946_b.nextInt(this->field_946_b.nextInt(40) + 1) + 1);
    if (this->field_946_b.nextInt(15) != 0) var7 = 0;

    for (int var8 = 0; var8 < var7; ++var8) {
        double var9 = (double)(x * 16 + this->field_946_b.nextInt(16));
        double var11 = (double)this->field_946_b.nextInt(this->field_946_b.nextInt(120) + 8);
        double var13 = (double)(z * 16 + this->field_946_b.nextInt(16));
        int var15 = 1;
        if (this->field_946_b.nextInt(4) == 0) {
            this->func_669_a(chunkX, chunkZ, blocks, var9, var11, var13);
            var15 += this->field_946_b.nextInt(4);
        }
        for (int var16 = 0; var16 < var15; ++var16) {
            float var17 = this->field_946_b.nextFloat() * (float)M_PI * 2.0F;
            float var18 = (this->field_946_b.nextFloat() - 0.5F) * 2.0F / 8.0F;
            float var19 = this->field_946_b.nextFloat() * 2.0F + this->field_946_b.nextFloat();
            this->func_668_a(chunkX, chunkZ, blocks, var9, var11, var13, var19, var17, var18, 0, 0, 1.0);
        }
    }
}

// ============================================
// WorldGenMinable — exact port
// ============================================
WorldGenMinable::WorldGenMinable(int blockId, int count) : minableBlockId(blockId), numberOfBlocks(count) {}

bool WorldGenMinable::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    float var6 = rand.nextFloat() * (float)M_PI;
    double var7 = (double)((float)(x + 8) + std::sin(var6) * (float)numberOfBlocks / 8.0F);
    double var9 = (double)((float)(x + 8) - std::sin(var6) * (float)numberOfBlocks / 8.0F);
    double var11 = (double)((float)(z + 8) + std::cos(var6) * (float)numberOfBlocks / 8.0F);
    double var13 = (double)((float)(z + 8) - std::cos(var6) * (float)numberOfBlocks / 8.0F);
    double var15 = (double)(y + rand.nextInt(3) + 2);
    double var17 = (double)(y + rand.nextInt(3) + 2);

    for (int var19 = 0; var19 <= numberOfBlocks; ++var19) {
        double var20 = var7 + (var9 - var7) * (double)var19 / (double)numberOfBlocks;
        double var22 = var15 + (var17 - var15) * (double)var19 / (double)numberOfBlocks;
        double var24 = var11 + (var13 - var11) * (double)var19 / (double)numberOfBlocks;
        double var26 = rand.nextDouble() * (double)numberOfBlocks / 16.0;
        double var28 = (double)(std::sin((float)var19 * (float)M_PI / (float)numberOfBlocks) + 1.0F) * var26 + 1.0;
        double var30 = (double)(std::sin((float)var19 * (float)M_PI / (float)numberOfBlocks) + 1.0F) * var26 + 1.0;

        for (int var32 = (int)(var20 - var28 / 2.0); var32 <= (int)(var20 + var28 / 2.0); ++var32) {
            for (int var33 = (int)(var22 - var30 / 2.0); var33 <= (int)(var22 + var30 / 2.0); ++var33) {
                for (int var34 = (int)(var24 - var28 / 2.0); var34 <= (int)(var24 + var28 / 2.0); ++var34) {
                    double var35 = ((double)var32 + 0.5 - var20) / (var28 / 2.0);
                    double var37 = ((double)var33 + 0.5 - var22) / (var30 / 2.0);
                    double var39 = ((double)var34 + 0.5 - var24) / (var28 / 2.0);
                    if (var35 * var35 + var37 * var37 + var39 * var39 < 1.0 && world->getBlockId(var32, var33, var34) == 1) {
                        world->setBlock(var32, var33, var34, minableBlockId);
                    }
                }
            }
        }
    }
    return true;
}

// ============================================
// WorldGenTrees — exact port
// ============================================
bool WorldGenTrees::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    int var6 = rand.nextInt(3) + 4;
    bool var7 = true;
    if (y >= 1 && y + var6 + 1 <= 128) {
        for (int var8 = y; var8 <= y + 1 + var6; ++var8) {
            uint8_t var9 = 1;
            if (var8 == y) var9 = 0;
            if (var8 >= y + 1 + var6 - 2) var9 = 2;

            for (int var10 = x - var9; var10 <= x + var9 && var7; ++var10) {
                for (int var11 = z - var9; var11 <= z + var9 && var7; ++var11) {
                    if (var8 >= 0 && var8 < 128) {
                        int var12 = world->getBlockId(var10, var8, var11);
                        if (var12 != 0 && var12 != 18) var7 = false;
                    } else {
                        var7 = false;
                    }
                }
            }
        }
        if (!var7) return false;

        int var8 = world->getBlockId(x, y - 1, z);
        if ((var8 == 2 || var8 == 3) && y < 128 - var6 - 1) {
            world->setBlockWithNotify(x, y - 1, z, 3); // dirt

            // Leaves
            for (int var16 = y - 3 + var6; var16 <= y + var6; ++var16) {
                int var10 = var16 - (y + var6);
                int var11 = 1 - var10 / 2;
                for (int var12 = x - var11; var12 <= x + var11; ++var12) {
                    int var13 = var12 - x;
                    for (int var14 = z - var11; var14 <= z + var11; ++var14) {
                        int var15 = var14 - z;
                        if ((std::abs(var13) != var11 || std::abs(var15) != var11 || (rand.nextInt(2) != 0 && var10 != 0)) &&
                            world->getBlockId(var12, var16, var14) == 0) {
                            world->setBlockWithNotify(var12, var16, var14, 18); // leaves
                        }
                    }
                }
            }
            // Trunk
            for (int var16 = 0; var16 < var6; ++var16) {
                int var10 = world->getBlockId(x, y + var16, z);
                if (var10 == 0 || var10 == 18) {
                    world->setBlockWithNotify(x, y + var16, z, 17); // log
                }
            }
            return true;
        }
    }
    return false;
}

// ============================================
// WorldGenBigTree — exact port
// ============================================
const int8_t WorldGenBigTree::otherCoordPairs[6] = {2, 0, 0, 1, 2, 1};

WorldGenBigTree::~WorldGenBigTree() {
    if (leafNodes) {
        for (int i = 0; i < leafNodeCount; ++i) delete[] leafNodes[i];
        delete[] leafNodes;
    }
}

void WorldGenBigTree::func_420_a(double a, double b, double c) {
    heightLimitLimit = (int)(a * 12.0);
    if (a > 0.5) leafDistanceLimit = 5;
    scaleWidth = b;
    leafDensity = c;
}

float WorldGenBigTree::layerSize(int y) {
    if ((double)y < (double)((float)heightLimit) * 0.3) return -1.618F;
    float halfH = (float)heightLimit / 2.0F;
    float dist = halfH - (float)y;
    float result;
    if (dist == 0.0F) result = halfH;
    else if (std::abs(dist) >= halfH) result = 0.0F;
    else result = (float)std::sqrt(std::pow((double)std::abs(halfH), 2.0) - std::pow((double)std::abs(dist), 2.0));
    result *= 0.5F;
    return result;
}

float WorldGenBigTree::leafSize(int y) {
    return y >= 0 && y < leafDistanceLimit ? (y != 0 && y != leafDistanceLimit - 1 ? 3.0F : 2.0F) : -1.0F;
}

int WorldGenBigTree::checkBlockLine(int* from, int* to) {
    int diff[3] = {0, 0, 0};
    int8_t maxAxis = 0;
    for (int8_t i = 0; i < 3; ++i) {
        diff[i] = to[i] - from[i];
        if (std::abs(diff[i]) > std::abs(diff[maxAxis])) maxAxis = i;
    }
    if (diff[maxAxis] == 0) return -1;

    int8_t ax1 = otherCoordPairs[maxAxis];
    int8_t ax2 = otherCoordPairs[maxAxis + 3];
    int8_t step = diff[maxAxis] > 0 ? 1 : -1;
    double r1 = (double)diff[ax1] / (double)diff[maxAxis];
    double r2 = (double)diff[ax2] / (double)diff[maxAxis];
    int pos[3] = {0, 0, 0};
    int i = 0;
    int end = diff[maxAxis] + step;
    for (; i != end; i += step) {
        pos[maxAxis] = from[maxAxis] + i;
        pos[ax1] = (int)((double)from[ax1] + (double)i * r1);
        pos[ax2] = (int)((double)from[ax2] + (double)i * r2);
        int bid = worldObj->getBlockId(pos[0], pos[1], pos[2]);
        if (bid != 0 && bid != 18) break;
    }
    return i == end ? -1 : std::abs(i);
}

void WorldGenBigTree::placeBlockLine(int* from, int* to, int blockId) {
    int diff[3] = {0, 0, 0};
    int8_t maxAxis = 0;
    for (int8_t i = 0; i < 3; ++i) {
        diff[i] = to[i] - from[i];
        if (std::abs(diff[i]) > std::abs(diff[maxAxis])) maxAxis = i;
    }
    if (diff[maxAxis] == 0) return;

    int8_t ax1 = otherCoordPairs[maxAxis];
    int8_t ax2 = otherCoordPairs[maxAxis + 3];
    int8_t step = diff[maxAxis] > 0 ? 1 : -1;
    double r1 = (double)diff[ax1] / (double)diff[maxAxis];
    double r2 = (double)diff[ax2] / (double)diff[maxAxis];
    int pos[3] = {0, 0, 0};
    int i = 0;
    int end = diff[maxAxis] + step;
    for (; i != end; i += step) {
        pos[maxAxis] = (int)((double)(from[maxAxis] + i) + 0.5);
        pos[ax1] = (int)((double)from[ax1] + (double)i * r1 + 0.5);
        pos[ax2] = (int)((double)from[ax2] + (double)i * r2 + 0.5);
        worldObj->setBlockWithNotify(pos[0], pos[1], pos[2], blockId);
    }
}

void WorldGenBigTree::genTreeLayer(int x, int y, int z, float radius, int8_t axis, int blockId) {
    int intRadius = (int)((double)radius + 0.618);
    int8_t ax1 = otherCoordPairs[axis];
    int8_t ax2 = otherCoordPairs[axis + 3];
    int center[3] = {x, y, z};
    int pos[3] = {0, 0, 0};
    pos[axis] = center[axis];

    for (int i = -intRadius; i <= intRadius; ++i) {
        pos[ax1] = center[ax1] + i;
        for (int j = -intRadius; j <= intRadius; ++j) {
            double dist = std::sqrt(std::pow((double)std::abs(i) + 0.5, 2.0) + std::pow((double)std::abs(j) + 0.5, 2.0));
            if (dist > (double)radius) continue;
            pos[ax2] = center[ax2] + j;
            int bid = worldObj->getBlockId(pos[0], pos[1], pos[2]);
            if (bid != 0 && bid != 18) continue;
            worldObj->setBlockWithNotify(pos[0], pos[1], pos[2], blockId);
        }
    }
}

void WorldGenBigTree::generateLeafNode(int x, int y, int z) {
    for (int i = y; i < y + leafDistanceLimit; ++i) {
        float size = leafSize(i - y);
        genTreeLayer(x, i, z, size, 1, 18); // leaves
    }
}

void WorldGenBigTree::generateLeafNodeList() {
    height = (int)((double)heightLimit * heightAttenuation);
    if (height >= heightLimit) height = heightLimit - 1;

    int branchCount = (int)(1.382 + std::pow(leafDensity * (double)heightLimit / 13.0, 2.0));
    if (branchCount < 1) branchCount = 1;

    int** temp = new int*[branchCount * heightLimit];
    for (int i = 0; i < branchCount * heightLimit; ++i) temp[i] = new int[4];

    int topY = basePos[1] + heightLimit - leafDistanceLimit;
    int count = 1;
    int baseY = basePos[1] + height;
    int layerY = topY - basePos[1];
    temp[0][0] = basePos[0]; temp[0][1] = topY; temp[0][2] = basePos[2]; temp[0][3] = baseY;
    --topY;

    while (layerY >= 0) {
        float layerRad = layerSize(layerY);
        if (layerRad < 0.0F) { --topY; --layerY; continue; }

        for (int b = 0; b < branchCount; ++b) {
            double branchDist = scaleWidth * (double)layerRad * ((double)treeRand.nextFloat() + 0.328);
            double angle = (double)treeRand.nextFloat() * 2.0 * M_PI;
            int bx = (int)(branchDist * std::sin(angle) + (double)basePos[0] + 0.5);
            int bz = (int)(branchDist * std::cos(angle) + (double)basePos[2] + 0.5);
            int bFrom[3] = {bx, topY, bz};
            int bTo[3] = {bx, topY + leafDistanceLimit, bz};

            if (checkBlockLine(bFrom, bTo) == -1) {
                int trunk[3] = {basePos[0], basePos[1], basePos[2]};
                double hDist = std::sqrt(std::pow((double)std::abs(basePos[0] - bx), 2.0) + std::pow((double)std::abs(basePos[2] - bz), 2.0));
                double branchY = hDist * branchSlope;
                if ((double)bFrom[1] - branchY > (double)baseY) {
                    trunk[1] = baseY;
                } else {
                    trunk[1] = (int)((double)bFrom[1] - branchY);
                }
                if (checkBlockLine(trunk, bFrom) == -1) {
                    temp[count][0] = bx;
                    temp[count][1] = topY;
                    temp[count][2] = bz;
                    temp[count][3] = trunk[1];
                    ++count;
                }
            }
        }
        --topY;
        --layerY;
    }

    leafNodeCount = count;
    leafNodes = new int*[count];
    for (int i = 0; i < count; ++i) {
        leafNodes[i] = new int[4];
        std::memcpy(leafNodes[i], temp[i], 4 * sizeof(int));
    }
    for (int i = 0; i < branchCount * heightLimit; ++i) delete[] temp[i];
    delete[] temp;
}

void WorldGenBigTree::generateLeaves() {
    for (int i = 0; i < leafNodeCount; ++i) {
        generateLeafNode(leafNodes[i][0], leafNodes[i][1], leafNodes[i][2]);
    }
}

void WorldGenBigTree::generateTrunk() {
    int from[3] = {basePos[0], basePos[1], basePos[2]};
    int to[3] = {basePos[0], basePos[1] + height, basePos[2]};
    placeBlockLine(from, to, 17); // log
}

void WorldGenBigTree::generateLeafNodeBases() {
    int trunk[3] = {basePos[0], basePos[1], basePos[2]};
    for (int i = 0; i < leafNodeCount; ++i) {
        int node[3] = {leafNodes[i][0], leafNodes[i][1], leafNodes[i][2]};
        trunk[1] = leafNodes[i][3];
        int relY = trunk[1] - basePos[1];
        if ((double)relY >= (double)heightLimit * 0.2) {
            placeBlockLine(trunk, node, 17); // log
        }
    }
}

bool WorldGenBigTree::validTreeLocation() {
    int from[3] = {basePos[0], basePos[1], basePos[2]};
    int to[3] = {basePos[0], basePos[1] + heightLimit - 1, basePos[2]};
    int below = worldObj->getBlockId(basePos[0], basePos[1] - 1, basePos[2]);
    if (below != 2 && below != 3) return false; // grass/dirt
    int result = checkBlockLine(from, to);
    if (result == -1) return true;
    if (result < 6) return false;
    heightLimit = result;
    return true;
}

bool WorldGenBigTree::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    worldObj = world;
    int64_t lng = rand.nextLong();
    treeRand.setSeed(lng);
    basePos[0] = x; basePos[1] = y; basePos[2] = z;
    if (heightLimit == 0) {
        heightLimit = 5 + treeRand.nextInt(heightLimitLimit);
    }
    if (!validTreeLocation()) return false;
    generateLeafNodeList();
    generateLeaves();
    generateTrunk();
    generateLeafNodeBases();
    return true;
}

// ============================================
// WorldGenLakes — exact port
// ============================================
WorldGenLakes::WorldGenLakes(int blockId) : liquidBlockId(blockId) {}

bool WorldGenLakes::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    x -= 8;
    for (z -= 8; y > 0 && world->getBlockId(x, y, z) == 0; --y) {}
    y -= 4;

    bool arr[2048] = {};
    int numBlobs = rand.nextInt(4) + 4;

    for (int i = 0; i < numBlobs; ++i) {
        double rx = rand.nextDouble() * 6.0 + 3.0;
        double ry = rand.nextDouble() * 4.0 + 2.0;
        double rz = rand.nextDouble() * 6.0 + 3.0;
        double cx = rand.nextDouble() * (16.0 - rx - 2.0) + 1.0 + rx / 2.0;
        double cy = rand.nextDouble() * (8.0 - ry - 4.0) + 2.0 + ry / 2.0;
        double cz = rand.nextDouble() * (16.0 - rz - 2.0) + 1.0 + rz / 2.0;

        for (int bx = 1; bx < 15; ++bx) {
            for (int bz = 1; bz < 15; ++bz) {
                for (int by = 1; by < 7; ++by) {
                    double dx = ((double)bx - cx) / (rx / 2.0);
                    double dy = ((double)by - cy) / (ry / 2.0);
                    double dz = ((double)bz - cz) / (rz / 2.0);
                    if (dx * dx + dy * dy + dz * dz < 1.0) {
                        arr[(bx * 16 + bz) * 8 + by] = true;
                    }
                }
            }
        }
    }

    // Check edges for liquids/solids
    for (int bx = 0; bx < 16; ++bx) {
        for (int bz = 0; bz < 16; ++bz) {
            for (int by = 0; by < 8; ++by) {
                bool isEdge = !arr[(bx * 16 + bz) * 8 + by] &&
                    ((bx < 15 && arr[((bx + 1) * 16 + bz) * 8 + by]) ||
                     (bx > 0 && arr[((bx - 1) * 16 + bz) * 8 + by]) ||
                     (bz < 15 && arr[(bx * 16 + bz + 1) * 8 + by]) ||
                     (bz > 0 && arr[(bx * 16 + (bz - 1)) * 8 + by]) ||
                     (by < 7 && arr[(bx * 16 + bz) * 8 + by + 1]) ||
                     (by > 0 && arr[(bx * 16 + bz) * 8 + (by - 1)]));
                if (isEdge) {
                    int bid = world->getBlockId(x + bx, y + by, z + bz);
                    if (by >= 4 && (bid == 8 || bid == 9 || bid == 10 || bid == 11)) return false;
                    bool solid = (bid != 0 && bid != 8 && bid != 9 && bid != 10 && bid != 11);
                    if (by < 4 && !solid && world->getBlockId(x + bx, y + by, z + bz) != liquidBlockId) return false;
                }
            }
        }
    }

    // Place blocks
    for (int bx = 0; bx < 16; ++bx) {
        for (int bz = 0; bz < 16; ++bz) {
            for (int by = 0; by < 8; ++by) {
                if (arr[(bx * 16 + bz) * 8 + by]) {
                    world->setBlock(x + bx, y + by, z + bz, by >= 4 ? 0 : liquidBlockId);
                }
            }
        }
    }

    // Grass conversion
    for (int bx = 0; bx < 16; ++bx) {
        for (int bz = 0; bz < 16; ++bz) {
            for (int by = 4; by < 8; ++by) {
                if (arr[(bx * 16 + bz) * 8 + by] && world->getBlockId(x + bx, y + by - 1, z + bz) == 3) {
                    world->setBlock(x + bx, y + by - 1, z + bz, 2); // grass
                }
            }
        }
    }

    return true;
}

// ============================================
// WorldGenFlowers — exact port
// ============================================
bool WorldGenFlowers::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    for (int i = 0; i < 64; ++i) {
        int fx = x + rand.nextInt(8) - rand.nextInt(8);
        int fy = y + rand.nextInt(4) - rand.nextInt(4);
        int fz = z + rand.nextInt(8) - rand.nextInt(8);
        if (world->getBlockId(fx, fy, fz) == 0 &&
            world->getBlockId(fx, fy - 1, fz) == 2) { // on grass
            world->setBlock(fx, fy, fz, plantBlockId);
        }
    }
    return true;
}

// ============================================
// WorldGenClay — exact port
// ============================================
bool WorldGenClay::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    // Check if we're in water
    int bid = world->getBlockId(x, y, z);
    if (bid != 8 && bid != 9) return false; // not water

    float var6 = rand.nextFloat() * (float)M_PI;
    double var7 = (double)((float)(x + 8) + std::sin(var6) * (float)numberOfBlocks / 8.0F);
    double var9 = (double)((float)(x + 8) - std::sin(var6) * (float)numberOfBlocks / 8.0F);
    double var11 = (double)((float)(z + 8) + std::cos(var6) * (float)numberOfBlocks / 8.0F);
    double var13 = (double)((float)(z + 8) - std::cos(var6) * (float)numberOfBlocks / 8.0F);
    double var15 = (double)(y + rand.nextInt(3) + 2);
    double var17 = (double)(y + rand.nextInt(3) + 2);

    for (int var19 = 0; var19 <= numberOfBlocks; ++var19) {
        double var20 = var7 + (var9 - var7) * (double)var19 / (double)numberOfBlocks;
        double var22 = var15 + (var17 - var15) * (double)var19 / (double)numberOfBlocks;
        double var24 = var11 + (var13 - var11) * (double)var19 / (double)numberOfBlocks;
        double var26 = rand.nextDouble() * (double)numberOfBlocks / 16.0;
        double var28 = (double)(std::sin((float)var19 * (float)M_PI / (float)numberOfBlocks) + 1.0F) * var26 + 1.0;
        double var30 = (double)(std::sin((float)var19 * (float)M_PI / (float)numberOfBlocks) + 1.0F) * var26 + 1.0;

        for (int var32 = (int)(var20 - var28 / 2.0); var32 <= (int)(var20 + var28 / 2.0); ++var32) {
            for (int var33 = (int)(var22 - var30 / 2.0); var33 <= (int)(var22 + var30 / 2.0); ++var33) {
                for (int var34 = (int)(var24 - var28 / 2.0); var34 <= (int)(var24 + var28 / 2.0); ++var34) {
                    double var35 = ((double)var32 + 0.5 - var20) / (var28 / 2.0);
                    double var37 = ((double)var33 + 0.5 - var22) / (var30 / 2.0);
                    double var39 = ((double)var34 + 0.5 - var24) / (var28 / 2.0);
                    if (var35 * var35 + var37 * var37 + var39 * var39 < 1.0) {
                        if (world->getBlockId(var32, var33, var34) == 12) { // sand
                            world->setBlock(var32, var33, var34, clayBlockId);
                        }
                    }
                }
            }
        }
    }
    return true;
}

// ============================================
// WorldGenDungeons — simplified (no TileEntity support)
// ============================================
bool WorldGenDungeons::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    int halfX = rand.nextInt(2) + 2;
    int halfZ = rand.nextInt(2) + 2;
    int height = 3;
    int solidCount = 0;

    // Check walls
    for (int dx = x - halfX - 1; dx <= x + halfX + 1; ++dx) {
        for (int dy = y - 1; dy <= y + height + 1; ++dy) {
            for (int dz = z - halfZ - 1; dz <= z + halfZ + 1; ++dz) {
                int bid = world->getBlockId(dx, dy, dz);
                bool solid = (bid != 0 && bid != 8 && bid != 9 && bid != 10 && bid != 11);
                if (dy == y - 1 && !solid) return false;
                if (dy == y + height + 1 && !solid) return false;
                if ((dx == x - halfX - 1 || dx == x + halfX + 1 || dz == z - halfZ - 1 || dz == z + halfZ + 1) &&
                    dy == y && world->getBlockId(dx, dy, dz) == 0 && world->getBlockId(dx, dy + 1, dz) == 0) {
                    ++solidCount;
                }
            }
        }
    }

    if (solidCount >= 1 && solidCount <= 5) {
        // Hollow out and place walls
        for (int dx = x - halfX - 1; dx <= x + halfX + 1; ++dx) {
            for (int dy = y + height; dy >= y - 1; --dy) {
                for (int dz = z - halfZ - 1; dz <= z + halfZ + 1; ++dz) {
                    if (dx != x - halfX - 1 && dy != y - 1 && dz != z - halfZ - 1 &&
                        dx != x + halfX + 1 && dy != y + height + 1 && dz != z + halfZ + 1) {
                        world->setBlock(dx, dy, dz, 0); // air
                    } else {
                        int bid = world->getBlockId(dx, dy, dz);
                        bool solid = (bid != 0 && bid != 8 && bid != 9);
                        if (dy >= 0 && !solid) {
                            world->setBlock(dx, dy, dz, 0);
                        } else if (solid) {
                            if (dy == y - 1 && rand.nextInt(4) != 0) {
                                world->setBlock(dx, dy, dz, 48); // mossy cobblestone
                            } else {
                                world->setBlock(dx, dy, dz, 4); // cobblestone
                            }
                        }
                    }
                }
            }
        }
        // Place spawner
        world->setBlock(x, y, z, 52); // mob spawner
        return true;
    }
    return false;
}

// ============================================
// WorldGenReed — exact port
// ============================================
bool WorldGenReed::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    for (int i = 0; i < 20; ++i) {
        int rx = x + rand.nextInt(4) - rand.nextInt(4);
        int ry = y;
        int rz = z + rand.nextInt(4) - rand.nextInt(4);
        if (world->getBlockId(rx, ry, rz) == 0) {
            // Check for adjacent water
            bool hasWater = (world->getBlockId(rx - 1, ry - 1, rz) == 8 || world->getBlockId(rx - 1, ry - 1, rz) == 9 ||
                             world->getBlockId(rx + 1, ry - 1, rz) == 8 || world->getBlockId(rx + 1, ry - 1, rz) == 9 ||
                             world->getBlockId(rx, ry - 1, rz - 1) == 8 || world->getBlockId(rx, ry - 1, rz - 1) == 9 ||
                             world->getBlockId(rx, ry - 1, rz + 1) == 8 || world->getBlockId(rx, ry - 1, rz + 1) == 9);
            if (hasWater) {
                int height = 2 + rand.nextInt(rand.nextInt(3) + 1);
                for (int h = 0; h < height; ++h) {
                    // canBlockStay: needs sand/dirt/grass below or reed below
                    int below = world->getBlockId(rx, ry + h - 1, rz);
                    if (h == 0) {
                        if (below != 2 && below != 3 && below != 12) break; // grass, dirt, sand
                    } else {
                        if (below != 83) break; // reed
                    }
                    if (world->getBlockId(rx, ry + h, rz) == 0) {
                        world->setBlock(rx, ry + h, rz, 83); // reed
                    }
                }
            }
        }
    }
    return true;
}

// ============================================
// WorldGenCactus — exact port
// ============================================
bool WorldGenCactus::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    for (int i = 0; i < 10; ++i) {
        int cx = x + rand.nextInt(8) - rand.nextInt(8);
        int cy = y + rand.nextInt(4) - rand.nextInt(4);
        int cz = z + rand.nextInt(8) - rand.nextInt(8);
        if (world->getBlockId(cx, cy, cz) == 0) {
            int height = 1 + rand.nextInt(rand.nextInt(3) + 1);
            for (int h = 0; h < height; ++h) {
                // canBlockStay: needs sand below, no adjacent blocks
                int below = world->getBlockId(cx, cy + h - 1, cz);
                if (h == 0) {
                    if (below != 12) break; // sand
                } else {
                    if (below != 81) break; // cactus
                }
                // Check no adjacent blocks
                if (world->getBlockId(cx - 1, cy + h, cz) != 0 ||
                    world->getBlockId(cx + 1, cy + h, cz) != 0 ||
                    world->getBlockId(cx, cy + h, cz - 1) != 0 ||
                    world->getBlockId(cx, cy + h, cz + 1) != 0) break;
                if (world->getBlockId(cx, cy + h, cz) == 0) {
                    world->setBlock(cx, cy + h, cz, 81); // cactus
                }
            }
        }
    }
    return true;
}

// ============================================
// WorldGenPumpkin — exact port
// ============================================
bool WorldGenPumpkin::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    for (int i = 0; i < 64; ++i) {
        int px = x + rand.nextInt(8) - rand.nextInt(8);
        int py = y + rand.nextInt(4) - rand.nextInt(4);
        int pz = z + rand.nextInt(8) - rand.nextInt(8);
        if (world->getBlockId(px, py, pz) == 0 && world->getBlockId(px, py - 1, pz) == 2) { // on grass
            world->setBlockAndMetadataWithNotify(px, py, pz, 86, rand.nextInt(4)); // pumpkin with random rotation
        }
    }
    return true;
}

// ============================================
// WorldGenLiquids — underground springs, exact port
// ============================================
bool WorldGenLiquids::generate(World* world, JavaRandom& rand, int x, int y, int z) {
    if (world->getBlockId(x, y + 1, z) != 1) return false; // stone above
    if (world->getBlockId(x, y - 1, z) != 1) return false; // stone below
    int current = world->getBlockId(x, y, z);
    if (current != 0 && current != 1) return false;

    int stoneCount = 0;
    if (world->getBlockId(x - 1, y, z) == 1) ++stoneCount;
    if (world->getBlockId(x + 1, y, z) == 1) ++stoneCount;
    if (world->getBlockId(x, y, z - 1) == 1) ++stoneCount;
    if (world->getBlockId(x, y, z + 1) == 1) ++stoneCount;

    int airCount = 0;
    if (world->getBlockId(x - 1, y, z) == 0) ++airCount;
    if (world->getBlockId(x + 1, y, z) == 0) ++airCount;
    if (world->getBlockId(x, y, z - 1) == 0) ++airCount;
    if (world->getBlockId(x, y, z + 1) == 0) ++airCount;

    if (stoneCount == 3 && airCount == 1) {
        world->setBlock(x, y, z, liquidBlockId);
    }
    return true;
}
