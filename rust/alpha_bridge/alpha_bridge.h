#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AlphaBuffer {
    uint8_t* data;
    size_t len;
    size_t capacity;
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
NbtCompound* alpha_nbt_compound_clone(const NbtCompound* ptr);
NbtList* alpha_nbt_list_clone(const NbtList* ptr);
void alpha_nbt_compound_remove(NbtCompound* comp, const char* name);
bool alpha_nbt_compound_has_key(const NbtCompound* comp, const char* name);

NbtList* alpha_nbt_list_create(uint8_t tag_type);
void alpha_nbt_list_free(NbtList* ptr);
uint8_t alpha_nbt_list_get_type(const NbtList* list);
size_t alpha_nbt_list_get_size(const NbtList* list);
void alpha_nbt_list_set_type(NbtList* list, uint8_t tag_type);

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
    size_t blocks_capacity;
    uint8_t* data;
    size_t data_len;
    size_t data_capacity;
    uint8_t* sky_light;
    size_t sky_light_len;
    size_t sky_light_capacity;
    uint8_t* block_light;
    size_t block_light_len;
    size_t block_light_capacity;
    uint8_t* height_map;
    size_t height_map_len;
    size_t height_map_capacity;
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
void alpha_chunk_data_free_except_arrays(AlphaChunkData* data);
void alpha_chunk_alloc_arrays(
    uint8_t** blocks,
    uint8_t** data,
    uint8_t** skylight,
    uint8_t** blocklight,
    uint8_t** height_map
);
void alpha_chunk_free_arrays(
    uint8_t* blocks,
    uint8_t* data,
    uint8_t* skylight,
    uint8_t* blocklight,
    uint8_t* height_map
);
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

// ItemStack FFI
typedef struct FfiItemStack {
    int32_t stack_size;
    int32_t animations_to_go;
    int32_t item_id;
    int32_t item_damage;
} FfiItemStack;

FfiItemStack item_stack_create(int32_t item_id, int32_t stack_size, int32_t item_damage);
FfiItemStack item_stack_copy(const FfiItemStack* stack);
bool item_stack_damage(FfiItemStack* stack, int32_t damage, int32_t max_damage);

// TileEntityFurnace FFI
typedef struct FfiFurnaceState {
    FfiItemStack slots[3];
    int16_t burn_time;
    int16_t cook_time;
    int16_t current_item_burn_time;
} FfiFurnaceState;

typedef struct FurnaceTickResult {
    bool changed;
    bool needs_block_update;
} FurnaceTickResult;

FfiFurnaceState furnace_create(void);
FurnaceTickResult furnace_tick(FfiFurnaceState* state, int32_t fuel_burn_time_from_cpp);

// TileEntityChest FFI
typedef struct FfiChestState {
    FfiItemStack slots[27];
} FfiChestState;

FfiChestState chest_create(void);

// TileEntitySign FFI
typedef struct FfiSignState {
    uint8_t lines[4][16];
} FfiSignState;

FfiSignState sign_create(void);
void sign_set_line(FfiSignState* state, int32_t line, const char* text);

// RustNetworkManager FFI
typedef struct RustNetworkManager RustNetworkManager;

typedef struct FfiSlotData {
    int16_t item_id;
    int8_t count;
    int16_t damage;
} FfiSlotData;

typedef struct RustPacket1Login {
    int32_t protocol_version;
    const char* username;
    const char* password;
    int64_t map_seed;
    int8_t dimension;
} RustPacket1Login;

typedef struct RustPacket2Handshake {
    const char* username;
} RustPacket2Handshake;

typedef struct RustPacket3Chat {
    const char* message;
} RustPacket3Chat;

typedef struct RustPacket5PlayerInventory {
    int32_t type;
    int16_t item_count;
    const FfiSlotData* slots;
} RustPacket5PlayerInventory;

typedef struct RustPacket7UseEntity {
    int32_t player_entity_id;
    int32_t target_entity_id;
    bool is_left_click;
} RustPacket7UseEntity;

typedef struct RustPacket10Flying {
    bool on_ground;
} RustPacket10Flying;

typedef struct RustPacket11PlayerPosition {
    double x;
    double y;
    double stance;
    double z;
    bool on_ground;
} RustPacket11PlayerPosition;

typedef struct RustPacket12PlayerLook {
    float yaw;
    float pitch;
    bool on_ground;
} RustPacket12PlayerLook;

typedef struct RustPacket13PlayerLookMove {
    double x;
    double y;
    double stance;
    double z;
    float yaw;
    float pitch;
    bool on_ground;
} RustPacket13PlayerLookMove;

typedef struct RustPacket14BlockDig {
    int8_t status;
    int32_t x;
    int8_t y;
    int32_t z;
    int8_t face;
} RustPacket14BlockDig;

typedef struct RustPacket15Place {
    int16_t item_id;
    int32_t x;
    int8_t y;
    int32_t z;
    int8_t direction;
} RustPacket15Place;

typedef struct RustPacket16BlockItemSwitch {
    int32_t entity_id;
    int16_t item_id;
} RustPacket16BlockItemSwitch;

typedef struct RustPacket18ArmAnimation {
    int32_t entity_id;
    int8_t animate;
} RustPacket18ArmAnimation;

typedef struct RustPacket21PickupSpawn {
    int32_t entity_id;
    int16_t item_id;
    int8_t count;
    int32_t x;
    int32_t y;
    int32_t z;
    int8_t rotation;
    int8_t pitch;
    int8_t roll;
} RustPacket21PickupSpawn;

typedef struct RustPacket59ComplexEntity {
    int32_t x;
    int16_t y;
    int32_t z;
    const uint8_t* nbt_data;
    size_t nbt_len;
} RustPacket59ComplexEntity;

typedef struct RustPacket255KickDisconnect {
    const char* reason;
} RustPacket255KickDisconnect;

typedef union RustPacketUnion {
    RustPacket1Login login;
    RustPacket2Handshake handshake;
    RustPacket3Chat chat;
    RustPacket5PlayerInventory inventory;
    RustPacket7UseEntity use_entity;
    RustPacket10Flying flying;
    RustPacket11PlayerPosition position;
    RustPacket12PlayerLook look;
    RustPacket13PlayerLookMove look_move;
    RustPacket14BlockDig block_dig;
    RustPacket15Place place;
    RustPacket16BlockItemSwitch item_switch;
    RustPacket18ArmAnimation arm_anim;
    RustPacket21PickupSpawn pickup_spawn;
    RustPacket59ComplexEntity complex_entity;
    RustPacket255KickDisconnect kick;
} RustPacketUnion;

typedef struct RustPacket {
    uint8_t packet_id;
    RustPacketUnion data;
} RustPacket;

RustNetworkManager* rust_network_manager_create(int socket_fd);
void rust_network_manager_destroy(RustNetworkManager* manager);
void rust_network_manager_send(
    RustNetworkManager* manager,
    const uint8_t* data,
    size_t len,
    bool is_chunk_data
);
RustPacket* rust_network_manager_poll_parsed(RustNetworkManager* manager);
void rust_network_manager_free_packet(RustPacket* packet);
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

typedef struct PathfinderWorldAccessor {
    void* world;
    bool (*is_liquid)(void* world, int32_t x, int32_t y, int32_t z);
    bool (*blocks_movement)(void* world, int32_t x, int32_t y, int32_t z);
} PathfinderWorldAccessor;

typedef struct FfiPathPoint {
    int32_t x;
    int32_t y;
    int32_t z;
} FfiPathPoint;

int32_t rust_pathfinder_find_path(
    PathfinderWorldAccessor accessor,
    double start_x, double start_y, double start_z,
    double target_x, double target_y, double target_z,
    float entity_width,
    float entity_height,
    float max_distance,
    FfiPathPoint* out_points,
    int32_t max_points
);

typedef enum ConsoleCommandTag {
    ConsoleCommand_Help,
    ConsoleCommand_List,
    ConsoleCommand_Stop,
    ConsoleCommand_SaveAll,
    ConsoleCommand_Op,
    ConsoleCommand_Deop,
    ConsoleCommand_BanIp,
    ConsoleCommand_PardonIp,
    ConsoleCommand_Ban,
    ConsoleCommand_Pardon,
    ConsoleCommand_Kick,
    ConsoleCommand_Tp,
    ConsoleCommand_Summon,
    ConsoleCommand_Say,
    ConsoleCommand_Tell,
    ConsoleCommand_Unknown
} ConsoleCommandTag;

typedef struct FfiString {
    const char* ptr;
    size_t len;
} FfiString;

typedef struct RustParsedCommand {
    ConsoleCommandTag tag;
    FfiString arg1;
    FfiString arg2;
    int32_t count;
} RustParsedCommand;

RustParsedCommand rust_parse_console_command(const char* cmd, size_t cmd_len);

enum BlockMaterialId {
    BLOCK_MATERIAL_AIR = 0,
    BLOCK_MATERIAL_GROUND = 1,
    BLOCK_MATERIAL_WOOD = 2,
    BLOCK_MATERIAL_ROCK = 3,
    BLOCK_MATERIAL_IRON = 4,
    BLOCK_MATERIAL_WATER = 5,
    BLOCK_MATERIAL_LAVA = 6,
    BLOCK_MATERIAL_LEAVES = 7,
    BLOCK_MATERIAL_PLANTS = 8,
    BLOCK_MATERIAL_SPONGE = 9,
    BLOCK_MATERIAL_CLOTH = 10,
    BLOCK_MATERIAL_FIRE = 11,
    BLOCK_MATERIAL_SAND = 12,
    BLOCK_MATERIAL_CIRCUITS = 13,
    BLOCK_MATERIAL_GLASS = 14,
    BLOCK_MATERIAL_TNT = 15,
    BLOCK_MATERIAL_UNUSED = 16,
    BLOCK_MATERIAL_ICE = 17,
    BLOCK_MATERIAL_SNOW = 18,
    BLOCK_MATERIAL_BUILT_SNOW = 19,
    BLOCK_MATERIAL_CACTUS = 20,
    BLOCK_MATERIAL_CLAY = 21,
    BLOCK_MATERIAL_PUMPKIN = 22,
    BLOCK_MATERIAL_PORTAL = 23,
    BLOCK_MATERIAL_WEB = 24,
};

enum BlockTypeId {
    BLOCK_TYPE_NORMAL = 0,
    BLOCK_TYPE_SAND = 1,
    BLOCK_TYPE_FLUID = 2,
    BLOCK_TYPE_FLOWER = 3,
    BLOCK_TYPE_TALL_GRASS = 4,
    BLOCK_TYPE_MUSHROOM = 5,
    BLOCK_TYPE_TORCH = 6,
    BLOCK_TYPE_CACTUS = 7,
    BLOCK_TYPE_REED = 8,
    BLOCK_TYPE_LEAVES = 9,
    BLOCK_TYPE_SAPLING = 10,
    BLOCK_TYPE_CROPS = 11,
    BLOCK_TYPE_SOIL = 12,
    BLOCK_TYPE_FIRE = 13,
    BLOCK_TYPE_ORE = 14,
};

typedef struct AlphaBlockProperties {
    float hardness;
    float resistance;
    int32_t light_opacity;
    int32_t light_value;
    uint8_t tick_on_load;
    uint8_t is_block_container;
    uint8_t allows_attachment;
    uint8_t material;
    uint8_t block_type;
    int32_t id_dropped;
    int32_t quantity_dropped;
    uint8_t can_harvest_block;
    float min_x;
    float min_y;
    float min_z;
    float max_x;
    float max_y;
    float max_z;
} AlphaBlockProperties;

AlphaBlockProperties alpha_block_properties_get(uint32_t id);

#ifdef __cplusplus
}
#endif


