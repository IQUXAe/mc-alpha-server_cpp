#pragma once

#include <cstdint>
#include <string>

// Exact port of Java's MobSpawnerBase biome system from Alpha 1.2.6
enum class BiomeType {
    RAINFOREST,
    SWAMPLAND,
    SEASONAL_FOREST,
    FOREST,
    SAVANNA,
    SHRUBLAND,
    TAIGA,
    DESERT,
    PLAINS,
    ICE_DESERT,
    TUNDRA,
    HELL
};

struct MobSpawnerBase {
    BiomeType type;
    uint8_t topBlock;
    uint8_t fillerBlock;

    MobSpawnerBase() : type(BiomeType::PLAINS), topBlock(2), fillerBlock(3) {} // grass, dirt
    MobSpawnerBase(BiomeType t, uint8_t top, uint8_t filler)
        : type(t), topBlock(top), fillerBlock(filler) {}

    bool operator==(const MobSpawnerBase& other) const { return type == other.type; }

    // Predefined biome instances
    static MobSpawnerBase rainforest;
    static MobSpawnerBase swampland;
    static MobSpawnerBase seasonalForest;
    static MobSpawnerBase forest;
    static MobSpawnerBase savanna;
    static MobSpawnerBase shrubland;
    static MobSpawnerBase taiga;
    static MobSpawnerBase desert;
    static MobSpawnerBase plains;
    static MobSpawnerBase iceDesert;
    static MobSpawnerBase tundra;
    static MobSpawnerBase hell;

    // 64x64 biome lookup table
    static MobSpawnerBase biomeLookupTable[4096];
    static bool lookupInitialized;

    // Exact port of Java's getBiome - NOTE: Java uses float here for biome interpolation
    // But we use double for better precision to match Java's internal calculations
    static MobSpawnerBase& getBiome(double temp, double humid) {
        humid *= temp;
        if (temp < 0.1) return tundra;
        if (humid < 0.2) {
            if (temp < 0.5) return tundra;
            if (temp < 0.95) return savanna;
            return desert;
        }
        if (humid > 0.5 && temp < 0.7) return swampland;
        if (temp < 0.5) return taiga;
        if (temp < 0.97) {
            if (humid < 0.35) return shrubland;
            return forest;
        }
        if (humid < 0.45) return plains;
        if (humid < 0.9) return seasonalForest;
        return rainforest;
    }

    static void generateBiomeLookup() {
        if (lookupInitialized) return;
        for (int i = 0; i < 64; ++i) {
            for (int j = 0; j < 64; ++j) {
                biomeLookupTable[i + j * 64] = getBiome((double)i / 63.0, (double)j / 63.0);
            }
        }
        // Set desert and iceDesert to sand
        desert.topBlock = desert.fillerBlock = 12;  // sand
        iceDesert.topBlock = iceDesert.fillerBlock = 12;  // sand
        lookupInitialized = true;
    }

    static MobSpawnerBase& getBiomeFromLookup(double temp, double humid) {
        int t = (int)(temp * 63.0);
        int h = (int)(humid * 63.0);
        if (t < 0) t = 0;
        if (t > 63) t = 63;
        if (h < 0) h = 0;
        if (h > 63) h = 63;
        return biomeLookupTable[t + h * 64];
    }
};

// Static member definitions
inline MobSpawnerBase MobSpawnerBase::rainforest      {BiomeType::RAINFOREST,       2, 3};
inline MobSpawnerBase MobSpawnerBase::swampland        {BiomeType::SWAMPLAND,        2, 3};
inline MobSpawnerBase MobSpawnerBase::seasonalForest   {BiomeType::SEASONAL_FOREST,  2, 3};
inline MobSpawnerBase MobSpawnerBase::forest           {BiomeType::FOREST,           2, 3};
inline MobSpawnerBase MobSpawnerBase::savanna          {BiomeType::SAVANNA,          2, 3};
inline MobSpawnerBase MobSpawnerBase::shrubland        {BiomeType::SHRUBLAND,        2, 3};
inline MobSpawnerBase MobSpawnerBase::taiga            {BiomeType::TAIGA,            2, 3};
inline MobSpawnerBase MobSpawnerBase::desert           {BiomeType::DESERT,           2, 3}; // overridden in generateBiomeLookup
inline MobSpawnerBase MobSpawnerBase::plains           {BiomeType::PLAINS,           2, 3};
inline MobSpawnerBase MobSpawnerBase::iceDesert        {BiomeType::ICE_DESERT,       2, 3}; // overridden in generateBiomeLookup
inline MobSpawnerBase MobSpawnerBase::tundra           {BiomeType::TUNDRA,           2, 3};
inline MobSpawnerBase MobSpawnerBase::hell             {BiomeType::HELL,             2, 3};

inline MobSpawnerBase MobSpawnerBase::biomeLookupTable[4096] = {};
inline bool MobSpawnerBase::lookupInitialized = false;
