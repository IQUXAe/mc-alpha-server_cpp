#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AlphaBuffer {
    uint8_t* data;
    size_t len;
} AlphaBuffer;

typedef struct AlphaLevelDat {
    int64_t random_seed;
    int32_t spawn_x;
    int32_t spawn_y;
    int32_t spawn_z;
    int64_t world_time;
    int64_t size_on_disk;
    int32_t version;
    const char* level_name;
} AlphaLevelDat;

AlphaBuffer alpha_gzip_compress(const uint8_t* input, size_t input_len, int level);
AlphaBuffer alpha_gzip_decompress(const uint8_t* input, size_t input_len);
AlphaBuffer alpha_zstd_compress(const uint8_t* input, size_t input_len, int level);
AlphaBuffer alpha_zstd_decompress(const uint8_t* input, size_t input_len);
AlphaBuffer alpha_zlib_compress(const uint8_t* input, size_t input_len, int level);
AlphaBuffer alpha_zlib_decompress(const uint8_t* input, size_t input_len);
AlphaBuffer alpha_level_dat_encode(const AlphaLevelDat* level);
int alpha_level_dat_decode(const uint8_t* input, size_t input_len, AlphaLevelDat* out_level);
void alpha_level_dat_free(AlphaLevelDat* level);
void alpha_buffer_free(AlphaBuffer buffer);

// Opaque types for noise generators
typedef struct NoiseGeneratorOctaves NoiseGeneratorOctaves;
typedef struct NoiseGeneratorOctaves2 NoiseGeneratorOctaves2;

// Biome structures
#ifdef __cplusplus
enum class BiomeType;
struct MobSpawnerBase;
#else
typedef enum BiomeType {
    RAINFOREST = 0,
    SWAMPLAND = 1,
    SEASONAL_FOREST = 2,
    FOREST = 3,
    SAVANNA = 4,
    SHRUBLAND = 5,
    TAIGA = 6,
    DESERT = 7,
    PLAINS = 8,
    ICE_DESERT = 9,
    TUNDRA = 10,
    HELL = 11
} BiomeType;

typedef struct MobSpawnerBase {
    BiomeType type;
    uint8_t topBlock;
    uint8_t fillerBlock;
} MobSpawnerBase;
#endif

// Noise Generator Functions
NoiseGeneratorOctaves* alpha_noise_octaves_create(int64_t seed, int octaves);
void alpha_noise_octaves_create_all(
    int64_t seed,
    NoiseGeneratorOctaves** out_705,
    NoiseGeneratorOctaves** out_704,
    NoiseGeneratorOctaves** out_703,
    NoiseGeneratorOctaves** out_702,
    NoiseGeneratorOctaves** out_701,
    NoiseGeneratorOctaves** out_715,
    NoiseGeneratorOctaves** out_714,
    NoiseGeneratorOctaves** out_713
);
void alpha_noise_octaves_free(NoiseGeneratorOctaves* ptr);
void alpha_noise_octaves_func_648_a(
    NoiseGeneratorOctaves* ptr,
    double* out_buf,
    size_t out_len,
    double x,
    double y,
    double z,
    int x_size,
    int y_size,
    int z_size,
    double x_scale,
    double y_scale,
    double z_scale
);
void alpha_noise_octaves_func_4103_a(
    NoiseGeneratorOctaves* ptr,
    double* out_buf,
    size_t out_len,
    int x,
    int z,
    int x_size,
    int z_size,
    double x_scale,
    double z_scale
);
double alpha_noise_octaves_func_647_a(NoiseGeneratorOctaves* ptr, double x, double z);

NoiseGeneratorOctaves2* alpha_noise_octaves2_create(int64_t seed, int octaves);
void alpha_noise_octaves2_free(NoiseGeneratorOctaves2* ptr);
void alpha_noise_octaves2_func_4101_a(
    NoiseGeneratorOctaves2* ptr,
    double* out_buf,
    size_t out_len,
    double x,
    double y,
    int x_size,
    int y_size,
    double x_scale,
    double y_scale,
    double amplitude
);

// Biome Functions
void alpha_biome_get_temperatures(
    NoiseGeneratorOctaves2* temp_noise_gen,
    NoiseGeneratorOctaves2* noise_gen3,
    double* out_temps,
    size_t out_len,
    int x,
    int z,
    int x_size,
    int z_size
);
void alpha_biome_load_block_generator_data(
    NoiseGeneratorOctaves2* temp_noise_gen,
    NoiseGeneratorOctaves2* humid_noise_gen,
    NoiseGeneratorOctaves2* noise_gen3,
    MobSpawnerBase* out_biomes,
    double* out_temps,
    double* out_humids,
    size_t out_len,
    int x,
    int z,
    int x_size,
    int z_size
);

// Density Field Functions
void alpha_density_generate_field(
    double* out_field,
    size_t out_len,
    int var2,
    int var3,
    int var4,
    int var5,
    int var6,
    int var7,
    const double* temperatures,
    const double* humidities,
    NoiseGeneratorOctaves* field_715_a,
    NoiseGeneratorOctaves* field_714_b,
    NoiseGeneratorOctaves* field_703_m,
    NoiseGeneratorOctaves* field_705_k,
    NoiseGeneratorOctaves* field_704_l
);

#ifdef __cplusplus
}
#endif
