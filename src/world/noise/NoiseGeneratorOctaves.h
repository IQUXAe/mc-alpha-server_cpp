#pragma once

#include <vector>
#include <cmath>
#include <cstdint>

class NoiseGeneratorPerlin {
public:
    double xCoord, yCoord, zCoord;
    int permutations[512];

    explicit NoiseGeneratorPerlin(JavaRandom& rand) {
        xCoord = rand.nextDouble() * 256.0;
        yCoord = rand.nextDouble() * 256.0;
        zCoord = rand.nextDouble() * 256.0;

        for (int i = 0; i < 256; ++i) {
            permutations[i] = i;
        }

        for (int i = 0; i < 256; ++i) {
            int j = rand.nextInt(256 - i) + i;
            int temp = permutations[i];
            permutations[i] = permutations[j];
            permutations[j] = temp;
            permutations[i + 256] = permutations[i];
        }
    }

    double lerp(double t, double a, double b) const { return a + t * (b - a); }
    double grad(int hash, double x, double y, double z) const {
        int h = hash & 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    double noise(double x, double y, double z) const {
        x += xCoord;
        y += yCoord;
        z += zCoord;

        double floorX = std::floor(x);
        double floorY = std::floor(y);
        double floorZ = std::floor(z);

        int X = static_cast<int>(floorX) & 255;
        int Y = static_cast<int>(floorY) & 255;
        int Z = static_cast<int>(floorZ) & 255;

        x -= floorX;
        y -= floorY;
        z -= floorZ;

        double u = x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
        double v = y * y * y * (y * (y * 6.0 - 15.0) + 10.0);
        double w = z * z * z * (z * (z * 6.0 - 15.0) + 10.0);

        int A = permutations[X] + Y;
        int AA = permutations[A] + Z;
        int AB = permutations[A + 1] + Z;
        int B = permutations[X + 1] + Y;
        int BA = permutations[B] + Z;
        int BB = permutations[B + 1] + Z;

        return lerp(w, lerp(v, lerp(u, grad(permutations[AA], x, y, z),
                                        grad(permutations[BA], x - 1, y, z)),
                               lerp(u, grad(permutations[AB], x, y - 1, z),
                                        grad(permutations[BB], x - 1, y - 1, z))),
                       lerp(v, lerp(u, grad(permutations[AA + 1], x, y, z - 1),
                                        grad(permutations[BA + 1], x - 1, y, z - 1)),
                               lerp(u, grad(permutations[AB + 1], x, y - 1, z - 1),
                                        grad(permutations[BB + 1], x - 1, y - 1, z - 1))));
    }
};

class NoiseGeneratorOctaves {
private:
    std::vector<NoiseGeneratorPerlin> generatorCollection;
    int octaves;

public:
    NoiseGeneratorOctaves(JavaRandom& rand, int octaves) : octaves(octaves) {
        for (int i = 0; i < octaves; ++i) {
            generatorCollection.push_back(NoiseGeneratorPerlin(rand));
        }
    }

    double generateNoise(double x, double z) const {
        double result = 0.0;
        double amplitude = 1.0;
        double frequency = 1.0;

        for (int i = 0; i < octaves; ++i) {
            result += generatorCollection[i].noise(x * frequency, 0.0, z * frequency) * amplitude;
            amplitude *= 0.5;
            frequency *= 2.0;
        }
        return result;
    }
    
    double generateNoise3D(double x, double y, double z) const {
        double result = 0.0;
        double amplitude = 1.0;
        double frequency = 1.0;

        for (int i = 0; i < octaves; ++i) {
            result += generatorCollection[i].noise(x * frequency, y * frequency, z * frequency) * amplitude;
            amplitude /= 2.0;
            frequency *= 2.0;
        }
        return result;
    }
};
