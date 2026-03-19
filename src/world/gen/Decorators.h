#pragma once
#include <vector>
#include <cstdint>
#include "../../core/JavaRandom.h"
#include "../World.h"

class ChunkProviderGenerate;

// ============================================
// MapGenBase — exact port from Java Alpha 1.2.6
// ============================================
class MapGenBase {
protected:
    int field_947_a = 8;
    JavaRandom field_946_b;
public:
    MapGenBase() = default;
    virtual ~MapGenBase() = default;
    void func_667_a(ChunkProviderGenerate* provider, World* world, int chunkX, int chunkZ, std::vector<uint8_t>& blocks);
    virtual void func_666_a(World* world, int x, int z, int chunkX, int chunkZ, std::vector<uint8_t>& blocks);
};

// ============================================
// MapGenCaves
// ============================================
class MapGenCaves : public MapGenBase {
protected:
    void func_669_a(int var1, int var2, std::vector<uint8_t>& var3, double var4, double var6, double var8);
    void func_668_a(int var1, int var2, std::vector<uint8_t>& var3, double var4, double var6, double var8, float var10, float var11, float var12, int var13, int var14, double var15);
    void func_666_a(World* world, int x, int z, int chunkX, int chunkZ, std::vector<uint8_t>& blocks) override;
};

// ============================================
// WorldGenMinable — ore veins
// ============================================
class WorldGenMinable {
private:
    int minableBlockId;
    int numberOfBlocks;
public:
    WorldGenMinable(int blockId, int count);
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenTrees — standard small tree
// ============================================
class WorldGenTrees {
public:
    WorldGenTrees() = default;
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenBigTree — large tree
// ============================================
class WorldGenBigTree {
private:
    JavaRandom treeRand;
    World* worldObj = nullptr;
    int basePos[3] = {0, 0, 0};
    int heightLimit = 0;
    int height;
    double heightAttenuation = 0.618;
    double branchSlope = 0.381;
    double scaleWidth = 1.0;
    double leafDensity = 1.0;
    int trunkSize = 1;
    int heightLimitLimit = 12;
    int leafDistanceLimit = 4;
    int** leafNodes = nullptr;
    int leafNodeCount = 0;

    static const int8_t otherCoordPairs[6];

    void generateLeafNodeList();
    void genTreeLayer(int x, int y, int z, float radius, int8_t axis, int blockId);
    float layerSize(int y);
    float leafSize(int y);
    void generateLeafNodeBases();
    void generateLeaves();
    void generateTrunk();
    void generateLeafNode(int x, int y, int z);
    void placeBlockLine(int* from, int* to, int blockId);
    int checkBlockLine(int* from, int* to);
    bool validTreeLocation();

public:
    WorldGenBigTree() = default;
    ~WorldGenBigTree();
    void func_420_a(double a, double b, double c);
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenLakes — surface lakes
// ============================================
class WorldGenLakes {
private:
    int liquidBlockId;
public:
    WorldGenLakes(int blockId);
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenFlowers — flowers, mushrooms
// ============================================
class WorldGenFlowers {
private:
    int plantBlockId;
public:
    WorldGenFlowers(int blockId) : plantBlockId(blockId) {}
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenClay
// ============================================
class WorldGenClay {
private:
    int clayBlockId = 82;
    int numberOfBlocks;
public:
    WorldGenClay(int count) : numberOfBlocks(count) {}
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenDungeons
// ============================================
class WorldGenDungeons {
public:
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenReed — sugar cane
// ============================================
class WorldGenReed {
public:
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenCactus
// ============================================
class WorldGenCactus {
public:
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenPumpkin
// ============================================
class WorldGenPumpkin {
public:
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};

// ============================================
// WorldGenLiquids — underground springs
// ============================================
class WorldGenLiquids {
private:
    int liquidBlockId;
public:
    WorldGenLiquids(int blockId) : liquidBlockId(blockId) {}
    bool generate(World* world, JavaRandom& rand, int x, int y, int z);
};
