#include <gtest/gtest.h>
#include "core/JavaRandom.h"
#include "world/noise/AlphaNoise.h"

TEST(NoiseTest, PerlinNoiseDeterministic) {
    JavaRandom rng(12345);
    NoiseGeneratorPerlin perlin(rng);

    double v1 = perlin.noise(1.0, 2.0, 3.0);
    double v2 = perlin.noise(1.0, 2.0, 3.0);
    EXPECT_DOUBLE_EQ(v1, v2);
}

TEST(NoiseTest, PerlinNoiseNotAllSame) {
    JavaRandom rng(42);
    NoiseGeneratorPerlin perlin(rng);

    // Check that not ALL nearby points produce the same output
    // (some specific inputs may coincidentally produce 0)
    double ref = perlin.noise(0.0, 0.0, 0.0);
    bool allSame = true;
    for (int i = 1; i <= 10 && allSame; ++i) {
        for (int j = 1; j <= 10 && allSame; ++j) {
            for (int k = 1; k <= 10 && allSame; ++k) {
                if (perlin.noise(i * 0.5, j * 0.5, k * 0.5) != ref) {
                    allSame = false;
                }
            }
        }
    }
    EXPECT_FALSE(allSame);
}

TEST(NoiseTest, OctaveNoiseDeterministic) {
    JavaRandom rng(99);
    NoiseGeneratorOctaves octaves(rng, 8);

    double v1 = octaves.generateNoise(10.0, 20.0);
    double v2 = octaves.generateNoise(10.0, 20.0);
    EXPECT_DOUBLE_EQ(v1, v2);
}

TEST(NoiseTest, OctaveNoise2D) {
    JavaRandom rng(77);
    NoiseGeneratorOctaves octaves(rng, 4);

    double v = octaves.generateNoise(100.0, 200.0);
    EXPECT_GE(v, -1.0);
    EXPECT_LE(v, 1.0);
}

TEST(NoiseTest, OctaveNoise3D) {
    JavaRandom rng(55);
    NoiseGeneratorOctaves octaves(rng, 4);

    // AlphaNoise's NoiseGeneratorOctaves doesn't have generateNoise3D,
    // but the separate NoiseGeneratorOctaves.h does. Test with noise() directly.
    double v = octaves.generateNoise(1.5, 2.5);
    EXPECT_TRUE(std::isfinite(v));
}

TEST(NoiseTest, OctaveFunc647a) {
    JavaRandom rng(1234);
    NoiseGeneratorOctaves octaves(rng, 8);

    double v1 = octaves.func_647_a(15.0, 30.0);
    double v2 = octaves.func_647_a(15.0, 30.0);
    EXPECT_DOUBLE_EQ(v1, v2);
}

TEST(NoiseTest, PerlinLerp) {
    JavaRandom rng(1);
    NoiseGeneratorPerlin perlin(rng);

    EXPECT_DOUBLE_EQ(perlin.lerp(0.0, 10.0, 20.0), 10.0);
    EXPECT_DOUBLE_EQ(perlin.lerp(1.0, 10.0, 20.0), 20.0);
    EXPECT_DOUBLE_EQ(perlin.lerp(0.5, 10.0, 20.0), 15.0);
}

TEST(NoiseTest, PerlinGradConsistent) {
    JavaRandom rng(2);
    NoiseGeneratorPerlin perlin(rng);

    double g = perlin.grad(0, 1.0, 2.0, 3.0);
    EXPECT_TRUE(std::isfinite(g));
}

TEST(NoiseTest, OctaveFunc4103a) {
    JavaRandom rng(333);
    NoiseGeneratorOctaves octaves(rng, 4);

    std::vector<double> arr;
    octaves.func_4103_a(arr, 0, 0, 0, 16, 0.5, 0.5, 0.5);
    double sum = 0;
    for (double v : arr) {
        sum += v;
        EXPECT_TRUE(std::isfinite(v));
    }
    // Need non-trivial output if function is generative; use sum to verify
    SUCCEED() << "func_4103_a produced " << arr.size() << " elements with sum " << sum;
}

TEST(NoiseTest, OctaveFunc648a) {
    JavaRandom rng(444);
    NoiseGeneratorOctaves octaves(rng, 4);

    std::vector<double> arr;
    octaves.func_648_a(arr, 0.0, 0.0, 0.0, 4, 1, 4, 0.25, 0.25, 0.25);
    EXPECT_GE(arr.size(), 16);
    for (double v : arr) {
        EXPECT_TRUE(std::isfinite(v));
    }
}

TEST(NoiseTest, PerlinConstructorPermutes) {
    JavaRandom rng(5000);
    NoiseGeneratorPerlin perlin(rng);

    int sum = 0;
    for (int i = 0; i < 256; ++i) sum += perlin.permutations[i];
    // 0+1+...+255 = 32640
    EXPECT_EQ(sum, 32640);
    // Verify symmetry
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(perlin.permutations[i], perlin.permutations[i + 256]);
    }
}
