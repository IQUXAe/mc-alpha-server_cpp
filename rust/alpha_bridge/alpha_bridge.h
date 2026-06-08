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

// Cave & Decorator Functions
typedef struct WorldAccessor {
    uint8_t (*get_block_id)(int32_t x, int32_t y, int32_t z);
    void (*set_block_id)(int32_t x, int32_t y, int32_t z, uint8_t id);
    uint8_t (*get_block_meta)(int32_t x, int32_t y, int32_t z);
    void (*set_block_meta)(int32_t x, int32_t y, int32_t z, uint8_t meta);
    bool (*allows_attachment)(int32_t x, int32_t y, int32_t z);
    bool (*is_block_solid)(int32_t x, int32_t y, int32_t z);
    int32_t (*get_height_value)(int32_t x, int32_t z);
} WorldAccessor;

void alpha_caves_generate(
    int64_t world_seed,
    int32_t chunk_x,
    int32_t chunk_z,
    uint8_t* blocks,
    size_t blocks_len
);

void alpha_decorate_chunk(
    WorldAccessor accessor,
    int64_t seed,
    int32_t chunk_x,
    int32_t chunk_z,
    int32_t biome_type_raw,
    NoiseGeneratorOctaves* noise_gen_713,
    const double* temperatures
);

bool alpha_generate_tree(
    WorldAccessor accessor,
    int64_t seed,
    int32_t x,
    int32_t y,
    int32_t z
);

bool alpha_generate_big_tree(
    WorldAccessor accessor,
    int64_t seed,
    int32_t x,
    int32_t y,
    int32_t z
);

// Opaque types for NBT
typedef struct NbtCompound NbtCompound;
typedef struct NbtList NbtList;

// NBT FFI Functions
NbtCompound* alpha_nbt_compound_create();
void alpha_nbt_compound_free(NbtCompound* ptr);
void alpha_nbt_compound_set_byte(NbtCompound* comp, const char* name, int8_t val);
void alpha_nbt_compound_set_short(NbtCompound* comp, const char* name, int16_t val);
void alpha_nbt_compound_set_int(NbtCompound* comp, const char* name, int32_t val);
void alpha_nbt_compound_set_long(NbtCompound* comp, const char* name, int64_t val);
void alpha_nbt_compound_set_float(NbtCompound* comp, const char* name, float val);
void alpha_nbt_compound_set_double(NbtCompound* comp, const char* name, double val);
void alpha_nbt_compound_set_string(NbtCompound* comp, const char* name, const char* val);
void alpha_nbt_compound_set_byte_array(NbtCompound* comp, const char* name, const uint8_t* val_ptr, size_t val_len);
void alpha_nbt_compound_set_compound(NbtCompound* comp, const char* name, NbtCompound* child);
void alpha_nbt_compound_set_list(NbtCompound* comp, const char* name, NbtList* child);

uint8_t alpha_nbt_compound_get_tag_type(const NbtCompound* comp, const char* name);
int8_t alpha_nbt_compound_get_byte(const NbtCompound* comp, const char* name);
int16_t alpha_nbt_compound_get_short(const NbtCompound* comp, const char* name);
int32_t alpha_nbt_compound_get_int(const NbtCompound* comp, const char* name);
int64_t alpha_nbt_compound_get_long(const NbtCompound* comp, const char* name);
float alpha_nbt_compound_get_float(const NbtCompound* comp, const char* name);
double alpha_nbt_compound_get_double(const NbtCompound* comp, const char* name);
AlphaBuffer alpha_nbt_compound_get_string(const NbtCompound* comp, const char* name);
AlphaBuffer alpha_nbt_compound_get_byte_array(const NbtCompound* comp, const char* name);
NbtCompound* alpha_nbt_compound_get_compound(const NbtCompound* comp, const char* name);
NbtList* alpha_nbt_compound_get_list(const NbtCompound* comp, const char* name);
AlphaBuffer alpha_nbt_compound_get_keys(const NbtCompound* comp);

NbtList* alpha_nbt_list_create(uint8_t tag_type);
void alpha_nbt_list_free(NbtList* ptr);
uint8_t alpha_nbt_list_get_type(const NbtList* list);
size_t alpha_nbt_list_get_size(const NbtList* list);

void alpha_nbt_list_add_byte(NbtList* list, int8_t val);
void alpha_nbt_list_add_short(NbtList* list, int16_t val);
void alpha_nbt_list_add_int(NbtList* list, int32_t val);
void alpha_nbt_list_add_long(NbtList* list, int64_t val);
void alpha_nbt_list_add_float(NbtList* list, float val);
void alpha_nbt_list_add_double(NbtList* list, double val);
void alpha_nbt_list_add_string(NbtList* list, const char* val);
void alpha_nbt_list_add_byte_array(NbtList* list, const uint8_t* val_ptr, size_t val_len);
void alpha_nbt_list_add_compound(NbtList* list, NbtCompound* child);
void alpha_nbt_list_add_list(NbtList* list, NbtList* child);

int8_t alpha_nbt_list_get_byte(const NbtList* list, size_t idx);
int16_t alpha_nbt_list_get_short(const NbtList* list, size_t idx);
int32_t alpha_nbt_list_get_int(const NbtList* list, size_t idx);
int64_t alpha_nbt_list_get_long(const NbtList* list, size_t idx);
float alpha_nbt_list_get_float(const NbtList* list, size_t idx);
double alpha_nbt_list_get_double(const NbtList* list, size_t idx);
AlphaBuffer alpha_nbt_list_get_string(const NbtList* list, size_t idx);
AlphaBuffer alpha_nbt_list_get_byte_array(const NbtList* list, size_t idx);
NbtCompound* alpha_nbt_list_get_compound(const NbtList* list, size_t idx);
NbtList* alpha_nbt_list_get_list(const NbtList* list, size_t idx);

AlphaBuffer alpha_nbt_compound_serialize_root(const NbtCompound* comp, const char* name);
NbtCompound* alpha_nbt_compound_deserialize_root(const uint8_t* data, size_t len, char** out_name, size_t* out_bytes_read);
void alpha_nbt_free_name(char* name);

// ChunkLoader FFI
typedef struct AlphaChunkData {
    int32_t x_pos;
    int32_t z_pos;
    int64_t last_update;
    uint8_t* blocks;
    size_t blocks_len;
    uint8_t* data;
    size_t data_len;
    uint8_t* sky_light;
    size_t sky_light_len;
    uint8_t* block_light;
    size_t block_light_len;
    uint8_t* height_map;
    size_t height_map_len;
    bool terrain_populated;
    NbtCompound** tile_entities;
    size_t tile_entities_count;
    NbtCompound** items;
    size_t items_count;
    NbtCompound** animals;
    size_t animals_count;
    NbtCompound** monsters;
    size_t monsters_count;
    NbtCompound** boats;
    size_t boats_count;
} AlphaChunkData;

void alpha_chunk_data_free(AlphaChunkData* data);
bool alpha_chunk_loader_save(
    const char* world_dir,
    bool create_dirs,
    const AlphaChunkData* chunk_data
);
AlphaChunkData* alpha_chunk_loader_load(
    const char* world_dir,
    int chunk_x,
    int chunk_z
);

// RustNetworkManager FFI
typedef struct RustNetworkManager RustNetworkManager;

RustNetworkManager* rust_network_manager_create(int socket_fd);
void rust_network_manager_destroy(RustNetworkManager* manager);
void rust_network_manager_send(
    RustNetworkManager* manager,
    const uint8_t* data,
    size_t len,
    bool is_chunk_data
);
bool rust_network_manager_poll(
    RustNetworkManager* manager,
    uint8_t* out_packet_id,
    uint8_t** out_payload,
    size_t* out_payload_len
);
void rust_network_manager_free_buffer(uint8_t* ptr, size_t len);
void rust_network_manager_shutdown(RustNetworkManager* manager, const char* reason);
bool rust_network_manager_is_running(RustNetworkManager* manager);
bool rust_network_manager_is_terminating(RustNetworkManager* manager);
void rust_network_manager_get_termination_reason(
    RustNetworkManager* manager,
    char* out_buf,
    size_t max_len
);
size_t rust_network_manager_get_send_queue_length(RustNetworkManager* manager);
void rust_network_manager_server_shutdown(RustNetworkManager* manager);

// ChunkProviderGenerate FFI
typedef struct RustChunkProviderGenerate RustChunkProviderGenerate;

typedef struct RustChunkData {
    uint8_t* blocks;
    uint8_t* metadata;
    int32_t x;
    int32_t z;
} RustChunkData;

typedef struct RustChunkDataBatch {
    RustChunkData chunks[4];
} RustChunkDataBatch;

RustChunkProviderGenerate* rust_chunk_provider_generate_create(int64_t seed);
void rust_chunk_provider_generate_destroy(RustChunkProviderGenerate* ptr);
void rust_chunk_provider_generate_chunk(
    RustChunkProviderGenerate* ptr,
    int32_t chunk_x,
    int32_t chunk_z,
    uint8_t* out_blocks,
    MobSpawnerBase* out_biomes,
    double* out_temps,
    double* out_humids
);
void rust_chunk_provider_populate_batch(
    RustChunkProviderGenerate* generator,
    const RustChunkDataBatch* batch,
    WorldAccessor accessor,
    int32_t chunk_x,
    int32_t chunk_z,
    int32_t biome_type_raw,
    const double* temperatures
);

#ifdef __cplusplus
}
#endif
