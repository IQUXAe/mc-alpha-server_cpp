#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include "../../core/JavaRandom.h"

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
    double func_4102_a(int hash, double x, double z) const {
        int h = hash & 15;
        double u = (1 - ((h & 8) >> 3)) * x;
        double v = h < 4 ? 0.0 : (h != 12 && h != 14 ? z : x);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    double noise(double x, double y, double z) const {
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

    void func_646_a(std::vector<double>& arr, double xBase, double yBase, double zBase, int xSize, int ySize, int zSize, double xFreq, double yFreq, double zFreq, double amplitude) const {
        int index = 0;
        if (ySize == 1) {
            double invAmp = 1.0 / amplitude;
            for (int dx = 0; dx < xSize; ++dx) {
                double x = (xBase + dx) * xFreq + xCoord;
                int ix = (int)x;
                if (x < ix) --ix;
                int X = ix & 255;
                x -= ix;
                double u = x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
                for (int dz = 0; dz < zSize; ++dz) {
                    double z = (zBase + dz) * zFreq + zCoord;
                    int iz = (int)z;
                    if (z < iz) --iz;
                    int Z = iz & 255;
                    z -= iz;
                    double w = z * z * z * (z * (z * 6.0 - 15.0) + 10.0);
                    int A = permutations[X];
                    int AA = permutations[A] + Z;
                    int B = permutations[X + 1];
                    int BA = permutations[B] + Z;
                    double res1 = lerp(u, func_4102_a(permutations[AA], x, z), grad(permutations[BA], x - 1.0, 0.0, z));
                    double res2 = lerp(u, grad(permutations[AA + 1], x, 0.0, z - 1.0), grad(permutations[BA + 1], x - 1.0, 0.0, z - 1.0));
                    double finalRes = lerp(w, res1, res2);
                    arr[index++] += finalRes * invAmp;
                }
            }
        } else {
            double invAmp = 1.0 / amplitude;
            int prevY = -1;
            double resA1 = 0.0, resA2 = 0.0, resB1 = 0.0, resB2 = 0.0;
            for (int dx = 0; dx < xSize; ++dx) {
                double x = (xBase + dx) * xFreq + xCoord;
                int ix = (int)x;
                if (x < ix) --ix;
                int X = ix & 255;
                x -= ix;
                double u = x * x * x * (x * (x * 6.0 - 15.0) + 10.0);

                for (int dz = 0; dz < zSize; ++dz) {
                    double z = (zBase + dz) * zFreq + zCoord;
                    int iz = (int)z;
                    if (z < iz) --iz;
                    int Z = iz & 255;
                    z -= iz;
                    double w = z * z * z * (z * (z * 6.0 - 15.0) + 10.0);

                    for (int dy = 0; dy < ySize; ++dy) {
                        double y = (yBase + dy) * yFreq + yCoord;
                        int iy = (int)y;
                        if (y < iy) --iy;
                        int Y = iy & 255;
                        y -= iy;
                        double v = y * y * y * (y * (y * 6.0 - 15.0) + 10.0);

                        if (dy == 0 || Y != prevY) {
                            prevY = Y;
                            int A = permutations[X] + Y;
                            int AA = permutations[A] + Z;
                            int AB = permutations[A + 1] + Z;
                            int B = permutations[X + 1] + Y;
                            int BA = permutations[B] + Z;
                            int BB = permutations[B + 1] + Z;

                            resA1 = lerp(u, grad(permutations[AA], x, y, z), grad(permutations[BA], x - 1.0, y, z));
                            resA2 = lerp(u, grad(permutations[AB], x, y - 1.0, z), grad(permutations[BB], x - 1.0, y - 1.0, z));
                            resB1 = lerp(u, grad(permutations[AA + 1], x, y, z - 1.0), grad(permutations[BA + 1], x - 1.0, y, z - 1.0));
                            resB2 = lerp(u, grad(permutations[AB + 1], x, y - 1.0, z - 1.0), grad(permutations[BB + 1], x - 1.0, y - 1.0, z - 1.0));
                        }

                        double res1 = lerp(v, resA1, resA2);
                        double res2 = lerp(v, resB1, resB2);
                        double finalRes = lerp(w, res1, res2);
                        arr[index++] += finalRes * invAmp;
                    }
                }
            }
        }
    }
};

class NoiseGeneratorOctaves {
private:
    std::vector<NoiseGeneratorPerlin> generatorCollection;
    int octaves;

public:
    NoiseGeneratorOctaves(JavaRandom& rand, int octaves) : octaves(octaves) {
        for (int i = 0; i < octaves; ++i) {
            generatorCollection.emplace_back(rand);
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

    std::vector<double>& func_648_a(std::vector<double>& arr, double x, double y, double z, int xSize, int ySize, int zSize, double xFreq, double yFreq, double zFreq) {
        if (arr.size() < (size_t)(xSize * ySize * zSize)) {
            arr.resize(xSize * ySize * zSize, 0.0);
        } else {
            std::fill(arr.begin(), arr.end(), 0.0);
        }

        double amp = 1.0;

        for (int i = 0; i < octaves; ++i) {
            generatorCollection[i].func_646_a(arr, x, y, z, xSize, ySize, zSize, xFreq * amp, yFreq * amp, zFreq * amp, amp);
            amp /= 2.0;
        }
        return arr;
    }

    std::vector<double>& func_4103_a(std::vector<double>& arr, int x, int y, int z, int xSize, double var6, double var8, double var10) {
        return func_648_a(arr, (double)x, 10.0, (double)y, z, 1, xSize, var6, 1.0, var8);
    }

    // Exact port of Java's func_647_a - 2D single-point noise
    // NOTE: Java uses DECREASING frequency / INCREASING amplitude per octave
    double func_647_a(double x, double z) const {
        double result = 0.0;
        double scale = 1.0;  // var7 in Java - starts at 1.0, halves each octave
        for (int i = 0; i < octaves; ++i) {
            result += generatorCollection[i].noise(x * scale, 0.0, z * scale) / scale;
            scale /= 2.0;
        }
        return result;
    }
};
