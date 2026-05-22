#include <gtest/gtest.h>
#include "../rust/alpha_bridge/alpha_bridge.h"
#include <cmath>
#include <vector>

TEST(NoiseTest, OctaveNoiseDeterministic) {
    NoiseGeneratorOctaves* octaves = alpha_noise_octaves_create(99, 8);
    ASSERT_NE(octaves, nullptr);

    double v1 = alpha_noise_octaves_func_647_a(octaves, 10.0, 20.0);
    double v2 = alpha_noise_octaves_func_647_a(octaves, 10.0, 20.0);
    EXPECT_DOUBLE_EQ(v1, v2);

    alpha_noise_octaves_free(octaves);
}

TEST(NoiseTest, OctaveNoise2D) {
    NoiseGeneratorOctaves* octaves = alpha_noise_octaves_create(77, 4);
    ASSERT_NE(octaves, nullptr);

    double v = alpha_noise_octaves_func_647_a(octaves, 100.0, 200.0);
    EXPECT_TRUE(std::isfinite(v));

    alpha_noise_octaves_free(octaves);
}

TEST(NoiseTest, OctaveFunc647a) {
    NoiseGeneratorOctaves* octaves = alpha_noise_octaves_create(1234, 8);
    ASSERT_NE(octaves, nullptr);

    double v1 = alpha_noise_octaves_func_647_a(octaves, 15.0, 30.0);
    double v2 = alpha_noise_octaves_func_647_a(octaves, 15.0, 30.0);
    EXPECT_DOUBLE_EQ(v1, v2);

    alpha_noise_octaves_free(octaves);
}

TEST(NoiseTest, OctaveFunc4103a) {
    NoiseGeneratorOctaves* octaves = alpha_noise_octaves_create(333, 4);
    ASSERT_NE(octaves, nullptr);

    std::vector<double> arr(16 * 16);
    alpha_noise_octaves_func_4103_a(octaves, arr.data(), arr.size(), 0, 0, 16, 16, 0.5, 0.5);
    for (double v : arr) {
        EXPECT_TRUE(std::isfinite(v));
    }
    EXPECT_GT(arr.size(), 0);
    alpha_noise_octaves_free(octaves);
}

TEST(NoiseTest, OctaveFunc648a) {
    NoiseGeneratorOctaves* octaves = alpha_noise_octaves_create(444, 4);
    ASSERT_NE(octaves, nullptr);

    std::vector<double> arr(4 * 1 * 4);
    alpha_noise_octaves_func_648_a(octaves, arr.data(), arr.size(), 0.0, 0.0, 0.0, 4, 1, 4, 0.25, 0.25, 0.25);
    for (double v : arr) {
        EXPECT_TRUE(std::isfinite(v));
    }
    alpha_noise_octaves_free(octaves);
}
