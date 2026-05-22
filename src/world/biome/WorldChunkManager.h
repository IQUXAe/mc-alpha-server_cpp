#pragma once

#include <vector>
#include <cstdint>
#include "../../../rust/alpha_bridge/alpha_bridge.h"
#include "MobSpawnerBase.h"

// Exact port of Java's WorldChunkManager from Alpha 1.2.6 using Rust FFI
class WorldChunkManager {
private:
    NoiseGeneratorOctaves2* tempNoiseGen;    // field_4255_e
    NoiseGeneratorOctaves2* humidNoiseGen;   // field_4254_f
    NoiseGeneratorOctaves2* noiseGen3;       // field_4253_g

public:
    std::vector<double> temperature;
    std::vector<double> humidity;
    std::vector<double> field_4257_c;   // noise blend array
    std::vector<MobSpawnerBase> biomes; // field_4256_d

    WorldChunkManager(int64_t worldSeed) {
        tempNoiseGen = alpha_noise_octaves2_create(worldSeed * 9871LL, 4);
        humidNoiseGen = alpha_noise_octaves2_create(worldSeed * 39811LL, 4);
        noiseGen3 = alpha_noise_octaves2_create(worldSeed * 543321LL, 2);
        MobSpawnerBase::generateBiomeLookup();
    }

    ~WorldChunkManager() {
        alpha_noise_octaves2_free(tempNoiseGen);
        alpha_noise_octaves2_free(humidNoiseGen);
        alpha_noise_octaves2_free(noiseGen3);
    }

    MobSpawnerBase func_4067_a(int x, int z) {
        auto result = loadBlockGeneratorData(x, z, 1, 1);
        return result[0];
    }

    // getTemperatures - exact port of Java
    std::vector<double>& getTemperatures(std::vector<double>& temps, int x, int z, int xSize, int zSize) {
        if (temps.size() < (size_t)(xSize * zSize)) {
            temps.resize(xSize * zSize);
        }

        alpha_biome_get_temperatures(
            tempNoiseGen,
            noiseGen3,
            temps.data(),
            temps.size(),
            x,
            z,
            xSize,
            zSize
        );
        return temps;
    }

    // loadBlockGeneratorData - exact port of Java
    std::vector<MobSpawnerBase> loadBlockGeneratorData(int x, int z, int xSize, int zSize) {
        std::vector<MobSpawnerBase> result(xSize * zSize);
        if (temperature.size() < (size_t)(xSize * zSize)) {
            temperature.resize(xSize * zSize);
        }
        if (humidity.size() < (size_t)(xSize * zSize)) {
            humidity.resize(xSize * zSize);
        }

        alpha_biome_load_block_generator_data(
            tempNoiseGen,
            humidNoiseGen,
            noiseGen3,
            result.data(),
            temperature.data(),
            humidity.data(),
            result.size(),
            x,
            z,
            xSize,
            zSize
        );
        return result;
    }
};
