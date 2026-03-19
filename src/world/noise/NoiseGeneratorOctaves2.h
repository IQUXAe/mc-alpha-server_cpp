#pragma once

#include <vector>
#include "NoiseGenerator2.h"
#include "../../core/JavaRandom.h"

// Exact port of Java's NoiseGeneratorOctaves2 - octave wrapper for Simplex noise
class NoiseGeneratorOctaves2 {
private:
    std::vector<NoiseGenerator2> generators;
    int numOctaves;

public:
    NoiseGeneratorOctaves2(JavaRandom& rand, int octaves) : numOctaves(octaves) {
        for (int i = 0; i < octaves; ++i) {
            generators.emplace_back(rand);
        }
    }

    std::vector<double>& func_4101_a(std::vector<double>& arr, double x, double y,
                                      int xSize, int ySize, double xScale, double yScale, double amplitude) {
        return func_4100_a(arr, x, y, xSize, ySize, xScale, yScale, amplitude, 0.5);
    }

    std::vector<double>& func_4100_a(std::vector<double>& arr, double x, double y,
                                      int xSize, int ySize, double xScale, double yScale,
                                      double amplitude, double lacunarity) {
        xScale /= 1.5;
        yScale /= 1.5;

        if (arr.size() >= (size_t)(xSize * ySize)) {
            for (size_t i = 0; i < arr.size(); ++i) {
                arr[i] = 0.0;
            }
        } else {
            arr.resize(xSize * ySize, 0.0);
        }

        double persistenceSum = 1.0;
        double freqMul = 1.0;

        for (int i = 0; i < numOctaves; ++i) {
            generators[i].func_4115_a(arr, x, y, xSize, ySize,
                                       xScale * freqMul, yScale * freqMul,
                                       0.55 / persistenceSum);
            freqMul *= amplitude;
            persistenceSum *= lacunarity;
        }

        return arr;
    }
};
