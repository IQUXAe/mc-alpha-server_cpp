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
AlphaBuffer alpha_level_dat_encode(const AlphaLevelDat* level);
int alpha_level_dat_decode(const uint8_t* input, size_t input_len, AlphaLevelDat* out_level);
void alpha_level_dat_free(AlphaLevelDat* level);
void alpha_buffer_free(AlphaBuffer buffer);

#ifdef __cplusplus
}
#endif
