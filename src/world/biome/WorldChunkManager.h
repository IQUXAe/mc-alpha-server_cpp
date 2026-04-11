#pragma once

#include <vector>
#include <cstdint>
#include "../noise/NoiseGeneratorOctaves2.h"
#include "MobSpawnerBase.h"
#include "../../core/JavaRandom.h"

// Exact port of Java's WorldChunkManager from Alpha 1.2.6
class WorldChunkManager {
private:
    JavaRandom tempRand;
    JavaRandom humidRand;
    JavaRandom noise3Rand;
    NoiseGeneratorOctaves2 tempNoiseGen;    // field_4255_e
    NoiseGeneratorOctaves2 humidNoiseGen;   // field_4254_f
    NoiseGeneratorOctaves2 noiseGen3;       // field_4253_g

public:
    std::vector<double> temperature;
    std::vector<double> humidity;
    std::vector<double> field_4257_c;   // noise blend array
    std::vector<MobSpawnerBase> biomes; // field_4256_d

    WorldChunkManager(int64_t worldSeed)
        : tempRand(worldSeed * 9871LL),
          humidRand(worldSeed * 39811LL),
          noise3Rand(worldSeed * 543321LL),
          tempNoiseGen(tempRand, 4),
          humidNoiseGen(humidRand, 4),
          noiseGen3(noise3Rand, 2) {
        MobSpawnerBase::generateBiomeLookup();
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

        temps = tempNoiseGen.func_4101_a(temps, (double)x, (double)z, xSize, xSize,
                                          0.025, 0.025, 0.25);
        field_4257_c = noiseGen3.func_4101_a(field_4257_c, (double)x, (double)z, xSize, xSize,
                                              0.25, 0.25, 0.5882352941176471);
        int idx = 0;
        for (int i = 0; i < xSize; ++i) {
            for (int j = 0; j < zSize; ++j) {
                double noise = field_4257_c[idx] * 1.1 + 0.5;
                double d1 = 0.01;
                double d2 = 1.0 - d1;
                double temp = (temps[idx] * 0.15 + 0.7) * d2 + noise * d1;
                temp = 1.0 - (1.0 - temp) * (1.0 - temp);
                if (temp < 0.0) temp = 0.0;
                if (temp > 1.0) temp = 1.0;
                temps[idx] = temp;
                ++idx;
            }
        }
        return temps;
    }

    // loadBlockGeneratorData - exact port of Java
    std::vector<MobSpawnerBase> loadBlockGeneratorData(int x, int z, int xSize, int zSize) {
        std::vector<MobSpawnerBase> result(xSize * zSize);

        temperature = tempNoiseGen.func_4101_a(temperature, (double)x, (double)z, xSize, xSize,
                                                0.025, 0.025, 0.25);
        humidity = humidNoiseGen.func_4101_a(humidity, (double)x, (double)z, xSize, xSize,
                                              0.05, 0.05, 1.0 / 3.0);
        field_4257_c = noiseGen3.func_4101_a(field_4257_c, (double)x, (double)z, xSize, xSize,
                                              0.25, 0.25, 0.5882352941176471);

        int idx = 0;
        for (int i = 0; i < xSize; ++i) {
            for (int j = 0; j < zSize; ++j) {
                double noise = field_4257_c[idx] * 1.1 + 0.5;

                double d1 = 0.01;
                double d2 = 1.0 - d1;
                double temp = (temperature[idx] * 0.15 + 0.7) * d2 + noise * d1;

                d1 = 0.002;
                d2 = 1.0 - d1;
                double humid = (humidity[idx] * 0.15 + 0.5) * d2 + noise * d1;

                temp = 1.0 - (1.0 - temp) * (1.0 - temp);
                if (temp < 0.0) temp = 0.0;
                if (humid < 0.0) humid = 0.0;
                if (temp > 1.0) temp = 1.0;
                if (humid > 1.0) humid = 1.0;

                temperature[idx] = temp;
                humidity[idx] = humid;
                result[idx] = MobSpawnerBase::getBiomeFromLookup(temp, humid);
                ++idx;
            }
        }

        return result;
    }
};
