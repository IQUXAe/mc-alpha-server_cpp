#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

typedef struct FfiPlayerSlot {
    uint8_t slot;
    int16_t item_id;
    int8_t count;
    int16_t damage;
} FfiPlayerSlot;

typedef struct AlphaPlayerData {
    double pos_x;
    double pos_y;
    double pos_z;
    double motion_x;
    double motion_y;
    double motion_z;
    float rotation_yaw;
    float rotation_pitch;
    float fall_distance;
    int16_t fire;
    int16_t air;
    bool on_ground;
    int16_t health;
    int16_t hurt_time;
    int16_t death_time;
    int16_t attack_time;
    int32_t dimension;
    int32_t score;
    int32_t held_item_id;
    FfiPlayerSlot slots[64];
    size_t slots_count;
} AlphaPlayerData;

bool alpha_player_storage_save(const char* filepath, const AlphaPlayerData* data);
bool alpha_player_storage_load(const char* filepath, AlphaPlayerData* out_data);

AlphaBuffer alpha_gzip_compress(const uint8_t* input, size_t input_len, int level);
AlphaBuffer alpha_gzip_decompress(const uint8_t* input, size_t input_len);
AlphaBuffer alpha_zlib_compress(const uint8_t* input, size_t input_len, int level);
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

// ItemStack and TileEntity types (shared by ChunkLoader)
typedef struct FfiItemStack {
    int32_t stack_size;
    int32_t animations_to_go;
    int32_t item_id;
    int32_t item_damage;
} FfiItemStack;

typedef struct FfiFurnaceState {
    FfiItemStack slots[3];
    int16_t burn_time;
    int16_t cook_time;
    int16_t current_item_burn_time;
} FfiFurnaceState;

typedef struct FfiTileEntityFurnaceData {
    int32_t x;
    int32_t y;
    int32_t z;
    FfiFurnaceState state;
} FfiTileEntityFurnaceData;

typedef struct FfiChestState {
    FfiItemStack slots[27];
} FfiChestState;

typedef struct FfiTileEntityChestData {
    int32_t x;
    int32_t y;
    int32_t z;
    FfiChestState state;
} FfiTileEntityChestData;

typedef struct FfiSignState {
    uint8_t lines[4][16];
} FfiSignState;

typedef struct FfiTileEntitySignData {
    int32_t x;
    int32_t y;
    int32_t z;
    FfiSignState state;
} FfiTileEntitySignData;

typedef struct FfiEntityItemData {
    int32_t item_id;
    int32_t count;
    int32_t meta;
    int32_t age;
    int32_t delay;
    double x;
    double y;
    double z;
} FfiEntityItemData;

typedef struct FfiEntityAnimalData {
    const char* id;
    double x;
    double y;
    double z;
    double motion_x;
    double motion_y;
    double motion_z;
    float rotation_yaw;
    float rotation_pitch;
    int16_t health;
    int16_t max_health;
    bool saddled;
    bool sheared;
    int32_t egg_lay_time;
} FfiEntityAnimalData;

typedef struct FfiEntityMonsterData {
    const char* id;
    double x;
    double y;
    double z;
    double motion_x;
    double motion_y;
    double motion_z;
    float rotation_yaw;
    float rotation_pitch;
    int16_t health;
    int16_t max_health;
} FfiEntityMonsterData;

typedef struct FfiEntityBoatData {
    double x;
    double y;
    double z;
    double motion_x;
    double motion_y;
    double motion_z;
    float rotation_yaw;
    float rotation_pitch;
    int32_t time_since_hit;
    int32_t damage_taken;
    int32_t forward_direction;
} FfiEntityBoatData;

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
    FfiTileEntityFurnaceData* furnaces;
    size_t furnaces_count;
    FfiTileEntityChestData* chests;
    size_t chests_count;
    FfiTileEntitySignData* signs;
    size_t signs_count;
    FfiEntityItemData* items;
    size_t items_count;
    FfiEntityAnimalData* animals;
    size_t animals_count;
    FfiEntityMonsterData* monsters;
    size_t monsters_count;
    FfiEntityBoatData* boats;
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
AlphaBuffer alpha_chunk_nbt_serialize(
    const AlphaChunkData* chunk_data,
    bool use_zstd
);
AlphaChunkData* alpha_chunk_nbt_deserialize(
    const uint8_t* data,
    size_t len,
    bool is_zstd,
    int chunk_x,
    int chunk_z
);

// ItemStack FFI
bool item_stack_damage(FfiItemStack* stack, int32_t damage, int32_t max_damage);

// TileEntityFurnace FFI
typedef struct FurnaceTickResult {
    bool changed;
    bool needs_block_update;
} FurnaceTickResult;

FfiFurnaceState furnace_create(void);
FurnaceTickResult furnace_tick(FfiFurnaceState* state, int32_t fuel_burn_time_from_cpp);

// TileEntityChest FFI
FfiChestState chest_create(void);

// TileEntitySign FFI
FfiSignState sign_create(void);
void sign_set_line(FfiSignState* state, int32_t line, const char* text);

// Player Combat FFI
typedef struct FfiCombatResult {
    int32_t damage_after_armor;
    int32_t new_armor_damage_carry;
    int32_t scaled_damage;
} FfiCombatResult;

FfiCombatResult alpha_combat_calculate_damage(
    int32_t raw_damage,
    bool attacker_is_player,
    int32_t difficulty,
    int32_t armor_value,
    int32_t armor_damage_carry
);
int32_t alpha_combat_get_weapon_damage(int32_t item_id);

// Player Movement FFI
typedef struct FfiMovementInput {
    double from_x;
    double from_y;
    double from_z;
    double to_x;
    double to_y;
    double to_z;
    double stance;
    bool on_ground;
    bool is_in_water;
    float fall_distance;
} FfiMovementInput;

typedef struct FfiMovementResult {
    uint8_t status;
    float new_fall_distance;
    int32_t fall_damage;
    double move_sq;
} FfiMovementResult;

FfiMovementResult alpha_movement_validate(const FfiMovementInput* input);

// Player Inventory & Crafting FFI
int32_t alpha_inventory_max_stack_size(int32_t item_id);
int32_t alpha_inventory_add_item(
    FfiItemStack* slots,
    size_t slots_len,
    FfiItemStack* stack,
    int32_t stack_limit
);
int32_t alpha_inventory_calc_armor(
    const FfiItemStack* armor_slots,
    size_t armor_len
);
void alpha_inventory_damage_armor(
    FfiItemStack* armor_slots,
    size_t armor_len,
    int32_t damage_amount
);
FfiItemStack alpha_inventory_craft_2x2(const FfiItemStack* grid);
void alpha_inventory_consume_craft_2x2(FfiItemStack* grid);

// Player Mining FFI
bool alpha_mining_can_harvest(int32_t block_id, int32_t held_item_id);
float alpha_mining_get_str_vs_block(int32_t block_id, int32_t held_item_id);
float alpha_mining_check_hardness(
    int32_t block_id,
    int32_t held_item_id,
    bool in_water,
    bool on_ground
);
int32_t alpha_mining_get_destroy_ticks(
    int32_t block_id,
    int32_t held_item_id,
    bool in_water,
    bool on_ground
);

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

typedef struct RustPacket4UpdateTime {
    int64_t time;
} RustPacket4UpdateTime;

typedef struct RustPacket6SpawnPosition {
    int32_t x;
    int32_t y;
    int32_t z;
} RustPacket6SpawnPosition;

typedef struct RustPacket8UpdateHealth {
    int8_t health;
} RustPacket8UpdateHealth;

typedef struct RustPacket9Respawn {
    uint8_t dummy;
} RustPacket9Respawn;

typedef struct RustPacket17AddToInventory {
    int16_t item_id;
    int8_t count;
    int16_t damage;
} RustPacket17AddToInventory;

typedef struct RustPacket20NamedEntitySpawn {
    int32_t entity_id;
    const char* name;
    int32_t x;
    int32_t y;
    int32_t z;
    int8_t rotation;
    int8_t pitch;
    int16_t current_item;
} RustPacket20NamedEntitySpawn;

typedef struct RustPacket22Collect {
    int32_t collected_entity_id;
    int32_t collector_entity_id;
} RustPacket22Collect;

typedef struct RustPacket23VehicleSpawn {
    int32_t entity_id;
    int8_t vehicle_type;
    int32_t x;
    int32_t y;
    int32_t z;
} RustPacket23VehicleSpawn;

typedef struct RustPacket24MobSpawn {
    int32_t entity_id;
    uint8_t mob_type;
    int32_t x;
    int32_t y;
    int32_t z;
    int8_t yaw;
    int8_t pitch;
} RustPacket24MobSpawn;

typedef struct RustPacket28EntityVelocity {
    int32_t entity_id;
    int16_t motion_x;
    int16_t motion_y;
    int16_t motion_z;
} RustPacket28EntityVelocity;

typedef struct RustPacket29DestroyEntity {
    int32_t entity_id;
} RustPacket29DestroyEntity;

typedef struct RustPacket30Entity {
    int32_t entity_id;
} RustPacket30Entity;

typedef struct RustPacket31RelEntityMove {
    int32_t entity_id;
    int8_t dx;
    int8_t dy;
    int8_t dz;
} RustPacket31RelEntityMove;

typedef struct RustPacket32EntityLook {
    int32_t entity_id;
    int8_t yaw;
    int8_t pitch;
} RustPacket32EntityLook;

typedef struct RustPacket33RelEntityMoveLook {
    int32_t entity_id;
    int8_t dx;
    int8_t dy;
    int8_t dz;
    int8_t yaw;
    int8_t pitch;
} RustPacket33RelEntityMoveLook;

typedef struct RustPacket34EntityTeleport {
    int32_t entity_id;
    int32_t x;
    int32_t y;
    int32_t z;
    int8_t yaw;
    int8_t pitch;
} RustPacket34EntityTeleport;

typedef struct RustPacket38EntityStatus {
    int32_t entity_id;
    int8_t status;
} RustPacket38EntityStatus;

typedef struct RustPacket39AttachEntity {
    int32_t entity_id;
    int32_t vehicle_id;
} RustPacket39AttachEntity;

typedef struct RustPacket50PreChunk {
    int32_t x;
    int32_t z;
    bool mode;
} RustPacket50PreChunk;

typedef struct RustPacket53BlockChange {
    int32_t x;
    int8_t y;
    int32_t z;
    uint8_t block_type;
    uint8_t metadata;
} RustPacket53BlockChange;

typedef struct RustPacket255KickDisconnect {
    const char* reason;
} RustPacket255KickDisconnect;

typedef union RustPacketUnion {
    RustPacket1Login login;
    RustPacket2Handshake handshake;
    RustPacket3Chat chat;
    RustPacket4UpdateTime update_time;
    RustPacket5PlayerInventory inventory;
    RustPacket6SpawnPosition spawn_position;
    RustPacket7UseEntity use_entity;
    RustPacket8UpdateHealth update_health;
    RustPacket9Respawn respawn;
    RustPacket10Flying flying;
    RustPacket11PlayerPosition position;
    RustPacket12PlayerLook look;
    RustPacket13PlayerLookMove look_move;
    RustPacket14BlockDig block_dig;
    RustPacket15Place place;
    RustPacket16BlockItemSwitch item_switch;
    RustPacket17AddToInventory add_to_inventory;
    RustPacket18ArmAnimation arm_anim;
    RustPacket20NamedEntitySpawn named_entity_spawn;
    RustPacket21PickupSpawn pickup_spawn;
    RustPacket22Collect collect;
    RustPacket23VehicleSpawn vehicle_spawn;
    RustPacket24MobSpawn mob_spawn;
    RustPacket28EntityVelocity entity_velocity;
    RustPacket29DestroyEntity destroy_entity;
    RustPacket30Entity entity;
    RustPacket31RelEntityMove rel_entity_move;
    RustPacket32EntityLook entity_look;
    RustPacket33RelEntityMoveLook rel_entity_move_look;
    RustPacket34EntityTeleport entity_teleport;
    RustPacket38EntityStatus entity_status;
    RustPacket39AttachEntity attach_entity;
    RustPacket50PreChunk pre_chunk;
    RustPacket53BlockChange block_change;
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
bool rust_network_manager_send_packet(
    RustNetworkManager* manager,
    const RustPacket* packet,
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

// HTTPS session check via Rust (uses ureq/rustls)
bool rust_session_check(const char* username, const char* server_id, char* out_buf, size_t out_max);

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

// Thread-safe RNG backed by Rust's global Mutex<JavaRandom> seeded from /dev/urandom.
int32_t alpha_rng_next_int(int32_t bound);
float   alpha_rng_next_float(void);
double  alpha_rng_next_double(void);

// Player digging state machine (player_digging.rs).
// Owns progressive-digging state; C++ keeps only world access.
typedef struct FfiDigState {
    float cur_damage;
    float block_damage;
    int32_t initial_cooldown;
    int32_t target_x;
    int32_t target_y;
    int32_t target_z;
    bool has_target;
} FfiDigState;

typedef struct FfiDigInput {
    int32_t block_id;
    int32_t held_item_id;
    bool in_water;
    bool on_ground;
} FfiDigInput;

FfiDigState alpha_dig_state_new(void);
void alpha_dig_cancel(FfiDigState* state);
bool alpha_dig_on_click(FfiDigInput input);
bool alpha_dig_on_tick(FfiDigState* state, int32_t x, int32_t y, int32_t z, FfiDigInput input);

// Entity tracker math (tracker_math.rs). Pure functions, no allocation.
int32_t alpha_tracker_encode_pos(double pos);
int8_t alpha_tracker_encode_rot(float degrees);
uint8_t alpha_tracker_move_kind(int32_t dx, int32_t dy, int32_t dz, bool moved, bool turned);
bool alpha_tracker_velocity_changed(double motion_x, double motion_y, double motion_z,
                                    double last_x, double last_y, double last_z,
                                    bool send_velocity);
bool alpha_tracker_in_range(double player_x, double player_z,
                            int32_t last_fixed_x, int32_t last_fixed_z,
                            int32_t tracking_range);

// Mob spawning math (mob_spawning.rs). Pure functions, no allocation.
int32_t alpha_spawn_max_count(int32_t num_eligible_chunks, int32_t budget_per_256);
int32_t alpha_spawn_pack_offset(int32_t first, int32_t second);
bool alpha_spawn_too_close_to_spawn(float fx, float fy, float fz,
                                    int32_t spawn_x, int32_t spawn_y, int32_t spawn_z);

// Mob spawning batch drivers (mob_spawning.rs). SpawnerWorld carries
// C++ callbacks for RNG (World::rand stream), block queries, and spawning.
typedef struct SpawnerWorld {
    int32_t (*next_int)(int32_t bound);
    float (*next_uniform_float)(float lo, float hi);
    bool (*chunk_exists)(int32_t x, int32_t z);
    bool (*is_solid)(int32_t x, int32_t y, int32_t z);
    bool (*is_air)(int32_t x, int32_t y, int32_t z);
    bool (*is_liquid)(int32_t x, int32_t y, int32_t z);
    int32_t (*try_spawn)(uint8_t kind, float fx, float fy, float fz, float yaw, int32_t* out_max_in_chunk);
    bool (*spawn_jockey)(float fx, float fy, float fz, float yaw, int32_t host_id);
} SpawnerWorld;

int32_t rust_world_spawn_hostile(const SpawnerWorld* world,
                                 const double* player_x, const double* player_y, const double* player_z,
                                 size_t num_players, int32_t current_count,
                                 int32_t spawn_x, int32_t spawn_y, int32_t spawn_z,
                                 int32_t world_height);
int32_t rust_world_spawn_passive(const SpawnerWorld* world,
                                 const double* player_x, const double* player_y, const double* player_z,
                                 size_t num_players, int32_t current_count,
                                 int32_t spawn_x, int32_t spawn_y, int32_t spawn_z,
                                 int32_t world_height);

// Block behaviors (block_ticks.rs). BlockTickWorld carries C++ callbacks
// for RNG draws (World::rand stream), block storage/light, scheduling,
// and entity spawning. siteAction: 0 = none, 1 = grow tree (seed set).
typedef struct BlockTickWorld {
    int32_t (*next_int)(int32_t bound);
    float (*next_float01)(void);
    uint64_t (*next_u64)(void);
    uint8_t (*get_block_id)(int32_t x, int32_t y, int32_t z);
    uint8_t (*get_block_id_nc)(int32_t x, int32_t y, int32_t z);
    uint8_t (*get_block_meta)(int32_t x, int32_t y, int32_t z);
    void (*set_block)(int32_t x, int32_t y, int32_t z, uint8_t id);
    void (*set_block_meta)(int32_t x, int32_t y, int32_t z, uint8_t meta);
    void (*set_block_notify)(int32_t x, int32_t y, int32_t z, uint8_t id);
    void (*set_block_update)(int32_t x, int32_t y, int32_t z, uint8_t id);
    void (*set_block_meta_notify)(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t meta);
    void (*set_block_and_meta)(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t meta);
    int32_t (*get_block_light)(int32_t x, int32_t y, int32_t z);
    bool (*can_see_sky)(int32_t x, int32_t y, int32_t z);
    bool (*attach_world)(int32_t x, int32_t y, int32_t z);
    bool (*attach_torch)(int32_t x, int32_t y, int32_t z);
    bool (*is_solid)(int32_t x, int32_t y, int32_t z);
    bool (*is_solid_nc)(int32_t x, int32_t y, int32_t z);
    bool (*is_water_or_lava)(int32_t x, int32_t y, int32_t z);
    bool (*is_water)(int32_t x, int32_t y, int32_t z);
    bool (*block_registered)(uint8_t id);
    bool (*collidable_box)(int32_t x, int32_t y, int32_t z);
    void (*schedule_update)(int32_t x, int32_t y, int32_t z, uint8_t block_id, int32_t delay);
    void (*mark_update)(int32_t x, int32_t y, int32_t z);
    void (*notify_neighbors)(int32_t x, int32_t y, int32_t z, uint8_t block_id);
    void (*spawn_drop)(int32_t item_id, int32_t count, int32_t damage, double fx, double fy, double fz, double spread, double up);
    void (*spawn_falling)(uint8_t block_id, double fx, double fy, double fz);
    void (*drop_occupant)(int32_t x, int32_t y, int32_t z);
} BlockTickWorld;

typedef struct TickAction {
    uint8_t kind;
    uint64_t seed;
} TickAction;

void block_sand_added(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
void block_sand_neighbor(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
void block_sand_tick(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
void block_fluid_added(const BlockTickWorld* world, uint8_t block_id, int32_t tick_rate, int32_t x, int32_t y, int32_t z);
void block_fluid_neighbor(const BlockTickWorld* world, uint8_t block_id, int32_t tick_rate, int32_t x, int32_t y, int32_t z);
void block_fluid_tick(const BlockTickWorld* world, uint8_t block_id, bool is_lava, int32_t x, int32_t y, int32_t z);
bool block_flower_can_stay(const BlockTickWorld* world, int32_t x, int32_t y, int32_t z);
void block_flower_neighbor(const BlockTickWorld* world, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
void block_flower_tick(const BlockTickWorld* world, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
void block_tallgrass_drop(const BlockTickWorld* world, int32_t seeds_id, float chance, int32_t x, int32_t y, int32_t z);
bool block_mushroom_can_stay(const BlockTickWorld* world, int32_t x, int32_t y, int32_t z);
void block_mushroom_neighbor(const BlockTickWorld* world, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
uint8_t block_torch_attach_meta(const BlockTickWorld* world, int32_t side, int32_t x, int32_t y, int32_t z);
void block_torch_added(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
bool block_torch_can_stay(const BlockTickWorld* world, int32_t x, int32_t y, int32_t z);
void block_torch_neighbor(const BlockTickWorld* world, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
bool block_cactus_can_stay(const BlockTickWorld* world, int32_t x, int32_t y, int32_t z);
bool block_reed_can_stay(const BlockTickWorld* world, int32_t x, int32_t y, int32_t z);
void block_cactus_added(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
void block_reed_added(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
void block_cactus_neighbor(const BlockTickWorld* world, uint8_t block_id, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
void block_reed_neighbor(const BlockTickWorld* world, uint8_t block_id, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
void block_cactus_tick(const BlockTickWorld* world, uint8_t block_id, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
void block_reed_tick(const BlockTickWorld* world, uint8_t block_id, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
void block_leaves_added(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
void block_leaves_neighbor(const BlockTickWorld* world, uint8_t block_id, uint8_t leaves_id, int32_t* guard, int32_t x, int32_t y, int32_t z);
void block_leaves_tick(const BlockTickWorld* world, uint8_t block_id, uint8_t leaves_id, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t* guard, int32_t x, int32_t y, int32_t z);
void block_leaves_drop(const BlockTickWorld* world, int32_t sapling_id, float chance, int32_t x, int32_t y, int32_t z);
void block_sapling_added(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
bool block_sapling_can_stay(const BlockTickWorld* world, int32_t x, int32_t y, int32_t z);
void block_sapling_neighbor(const BlockTickWorld* world, uint8_t block_id, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
TickAction block_sapling_tick(const BlockTickWorld* world, uint8_t block_id, int32_t drop_id, int32_t drop_count, int32_t drop_damage, int32_t x, int32_t y, int32_t z);
void block_crops_added(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
bool block_crops_can_stay(const BlockTickWorld* world, uint8_t crop_id, int32_t x, int32_t y, int32_t z);
void block_crops_neighbor(const BlockTickWorld* world, uint8_t block_id, uint8_t crop_id, int32_t wheat_id, int32_t seeds_id, int32_t x, int32_t y, int32_t z);
void block_crops_drop_ffi(const BlockTickWorld* world, int32_t wheat_id, int32_t seeds_id, int32_t x, int32_t y, int32_t z, uint8_t metadata, float chance);
void block_crops_tick(const BlockTickWorld* world, uint8_t block_id, uint8_t crop_id, int32_t wheat_id, int32_t seeds_id, int32_t x, int32_t y, int32_t z);
void block_soil_added(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
void block_soil_tick(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
void block_soil_walking(const BlockTickWorld* world, int32_t x, int32_t y, int32_t z);
void block_soil_neighbor(const BlockTickWorld* world, uint8_t block_id, int32_t x, int32_t y, int32_t z);
bool block_base_drop(const BlockTickWorld* world, int32_t item_id, int32_t count, int32_t damage, int32_t x, int32_t y, int32_t z, float chance);

// Fire behavior (block_fire.rs). FireWorld pairs the shared tick table
// with the TNT-detonation hook (virtual dispatch, stays in C++).
typedef struct FireWorld {
    const BlockTickWorld* base;
    void (*detonate_tnt)(int32_t x, int32_t y, int32_t z);
} FireWorld;

void block_fire_tick(const FireWorld* world, uint8_t fire_id, int32_t tick_rate,
                     int32_t x, int32_t y, int32_t z);
bool block_fire_can_place(const FireWorld* world, int32_t x, int32_t y, int32_t z);
void block_fire_neighbor(const FireWorld* world, int32_t x, int32_t y, int32_t z);
void block_fire_added(const FireWorld* world, uint8_t fire_id, int32_t tick_rate,
                      int32_t x, int32_t y, int32_t z);

// Container blocks (block_container.rs). ScatterWorld carries global-RNG
// draws plus the item-spawn hook; slot iteration stays in C++.
typedef struct ScatterWorld {
    int32_t (*next_int)(int32_t bound);
    float (*next_float01)(void);
    double (*next_float64_01)(void);
    void (*spawn_item)(int32_t item_id, int32_t count, int32_t damage,
                       double fx, double fy, double fz, double mx, double my, double mz);
} ScatterWorld;

int32_t block_chest_scatter_stack(const ScatterWorld* world, int32_t item_id, int32_t count,
                                  int32_t damage, int32_t x, int32_t y, int32_t z);
void block_furnace_scatter_stack(const ScatterWorld* world, int32_t item_id, int32_t count,
                                 int32_t damage, int32_t x, int32_t y, int32_t z);
bool block_chest_can_place(uint8_t (*get_block_id)(int32_t x, int32_t y, int32_t z),
                           uint8_t chest_id, int32_t x, int32_t y, int32_t z);

// Small-entity kernels (entity_misc.rs). Pointers and world mutation stay in C++.
typedef struct ItemMotion {
    double mx;
    double my;
    double mz;
} ItemMotion;

int8_t alpha_item_push_side(bool free_w, bool free_e, bool free_d, bool free_u,
                            bool free_n, bool free_s, double lx, double ly, double lz);
bool alpha_item_damp(bool on_ground, ItemMotion* io);
uint8_t alpha_falling_land(int32_t block_id, bool on_ground, int32_t by, int32_t land_id,
                           bool land_replaceable, bool have_block, int32_t fall_time);
double alpha_boat_water_fraction(double min_x, double min_y, double min_z,
                                 double max_x, double max_y, double max_z,
                                 bool (*is_water)(int32_t x, int32_t y, int32_t z));
bool alpha_boat_steer(double delta_x, double delta_z, float cur_yaw, float* out_yaw);
bool alpha_boat_rider_offset(float yaw, double* out_x, double* out_z);
bool alpha_arrow_face_velocity(double mx, double my, double mz, float* out_yaw, float* out_pitch);

// Player helpers (entity_player.rs). Name formatting stays in C++.
uint8_t alpha_player_death_cause(bool has_attacker, uint8_t attacker_kind, float fall_distance,
                                 bool on_cactus, bool drowning, bool in_lava, bool on_fire);

typedef struct DropVelocity {
    double mx;
    double my;
    double mz;
} DropVelocity;

DropVelocity alpha_player_drop_velocity(double ra, double rb, double rc);

// Server admin (server_admin.rs). Name lists are a tested API for the
// coming Rust server; the chat driver is live via ChatWorld callbacks.
typedef struct ChatWorld {
    bool (*is_op)(void);
    bool (*block_registered)(int32_t id);
    void (*give_item)(int32_t item_id, int32_t count, int32_t damage);
    void (*teleport)(double x, double y, double z, float yaw, float pitch);
    void (*send_chat)(const uint8_t* msg_ptr, size_t msg_len);
} ChatWorld;

void rust_chat_command(const ChatWorld* world, const uint8_t* msg_ptr, size_t msg_len,
                       float player_yaw, float player_pitch);

// Item-use kernels (item_use.rs). Verbs stay in C++.
uint8_t alpha_item_furnace_facing(float yaw);
uint8_t alpha_item_sign_yaw_meta(float yaw);

typedef struct FoodBite {
    int32_t new_count;
    int32_t heal;
} FoodBite;

FoodBite alpha_item_food_bite(int32_t count, int32_t heal_amount);

// Item verbs (item_verbs.rs). Stack bookkeeping stays in C++.
typedef struct ItemUseWorld {
    int32_t (*next_int)(int32_t bound);
    double (*next_float64_01)(void);
    uint8_t (*get_block_id)(int32_t x, int32_t y, int32_t z);
    bool (*set_block_notify)(int32_t x, int32_t y, int32_t z, uint8_t id);
    bool (*set_block_meta_notify)(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t meta);
    bool (*set_block_quiet)(int32_t x, int32_t y, int32_t z, uint8_t id);
    void (*set_block_meta)(int32_t x, int32_t y, int32_t z, uint8_t meta);
    bool (*does_attach)(int32_t x, int32_t y, int32_t z);
    bool (*material_burning)(int32_t x, int32_t y, int32_t z);
    bool (*material_solid)(int32_t x, int32_t y, int32_t z);
    bool (*collidable_box)(int32_t x, int32_t y, int32_t z);
    bool (*block_can_stay)(uint8_t id, int32_t x, int32_t y, int32_t z);
    bool (*placement_clear)(uint8_t id, int32_t x, int32_t y, int32_t z);
    void (*block_placed)(uint8_t id, int32_t x, int32_t y, int32_t z, int32_t side);
    bool (*have_block)(uint8_t id);
    void (*spawn_item)(int32_t item_id, int32_t count, int32_t damage,
                       double fx, double fy, double fz, double mx, double my, double mz);
    void (*send_te_packet)(int32_t x, int32_t y, int32_t z);
    bool (*ray_trace)(double sx, double sy, double sz, double ex, double ey, double ez,
                      int32_t* out_x, int32_t* out_y, int32_t* out_z);
} ItemUseWorld;

typedef struct FlintOut {
    bool placed;
    int32_t new_damage;
    bool broke;
} FlintOut;

typedef struct BoatThrow {
    double lx;
    double ly;
    double lz;
    double sx;
    double sy;
    double sz;
    double ex;
    double ey;
    double ez;
} BoatThrow;

bool item_hoe_use(const ItemUseWorld* world, int32_t seeds_id, int32_t x, int32_t y, int32_t z);
bool item_seeds_use(const ItemUseWorld* world, int32_t x, int32_t y, int32_t z, int32_t side);
bool item_flint_use(const ItemUseWorld* world, int32_t damage_in, int32_t max_damage,
                    int32_t x, int32_t y, int32_t z, int32_t side, FlintOut* out);
bool item_sign_use(const ItemUseWorld* world, int32_t x, int32_t y, int32_t z, int32_t side, float yaw);
bool item_block_use(const ItemUseWorld* world, uint8_t block_id, int32_t stack_count,
                    int32_t x, int32_t y, int32_t z, int32_t side, float yaw);
bool item_boat_aim(float prev_yaw, float yaw, float prev_pitch, float pitch,
                   double prev_x, double x, double prev_y, double y,
                   double prev_z, double z, double y_offset, BoatThrow* out);
bool item_boat_throw(const ItemUseWorld* world, double sx, double sy, double sz,
                     double ex, double ey, double ez,
                     int32_t* out_x, int32_t* out_y, int32_t* out_z);

// Tile-inventory slot ops (tile_inventory.rs). NBT translation and
// ItemStack allocation stay in C++.
typedef struct TileTaken {
    bool has_item;
    int32_t item_id;
    int32_t count;
    int32_t damage;
} TileTaken;

bool tile_slot_take(FfiItemStack* slots, size_t len, int32_t idx, int32_t amount, TileTaken* out);
bool tile_slot_store(FfiItemStack* slots, size_t len, int32_t idx, bool has_item,
                     int32_t item_id, int32_t count, int32_t damage, int32_t limit);
bool tile_slots_clear(FfiItemStack* slots, size_t len);

// Entity physics kernel (entity_physics.rs). Pure collision/fall/push math;
// the world-dependent half (box gathering, onFall, velocity) stays in C++.
typedef struct FfiAabb {
    double min_x;
    double min_y;
    double min_z;
    double max_x;
    double max_y;
    double max_z;
} FfiAabb;

typedef struct ResolvedMove {
    FfiAabb box_;
    double dx;
    double dy;
    double dz;
} ResolvedMove;

typedef struct PushOut {
    double dvx1;
    double dvz1;
    double dvx2;
    double dvz2;
} PushOut;

bool alpha_entity_resolve_move(const FfiAabb* box_, double dx, double dy, double dz,
                               const FfiAabb* boxes, size_t num_boxes, ResolvedMove* out);
float alpha_entity_fall_step(bool on_ground, double dy, float fall_distance, float* out_fall_event);
bool alpha_entity_push(double x1, double z1, double x2, double z2,
                       bool pushable1, bool pushable2, PushOut* out);

// Living-entity logic (entity_living.rs). Damage math, knockback, tick
// timers, steering, and fall damage; virtual dispatch stays in C++.
typedef struct AttackResult {
    int16_t health;
    int32_t last_damage;
    int32_t hurt_resist;
    int32_t hurt_time;
    int32_t attack_time;
    bool knocked;
    double kmx;
    double kmy;
    double kmz;
    bool send_status;
    bool died;
} AttackResult;

typedef struct LivingTick {
    int32_t air;
    int32_t hurt_time;
    int32_t attack_time;
    int32_t hurt_resist;
    bool suffocate;
    bool drown;
} LivingTick;

typedef struct MoveFeedback {
    bool on_ground;
    bool collided_vert;
    double pos_y;
} MoveFeedback;

typedef struct HeadingWorld {
    bool (*touching_liquid)(void);
    bool (*on_ladder)(void);
    bool (*do_move)(double dx, double dy, double dz, MoveFeedback* out);
} HeadingWorld;

typedef struct HeadingIo {
    double motion_x;
    double motion_y;
    double motion_z;
    float fall_distance;
} HeadingIo;

int16_t alpha_living_heal(int16_t health, int16_t max_health, int32_t amount, bool dead);
bool alpha_living_attack(double (*next_f01)(void),
                         int16_t health, int32_t hurt_resist, int32_t max_hurt_resist,
                         int32_t last_damage, int32_t hurt_time_in, int32_t attack_time_in,
                         bool dead, int32_t amount, bool has_attacker,
                         double self_x, double self_z, double atk_x, double atk_z,
                         double motion_x, double motion_y, double motion_z,
                         AttackResult* out);
LivingTick alpha_living_tick(bool alive, bool inside_opaque, bool in_water,
                             int32_t air, int32_t hurt_time, int32_t attack_time, int32_t hurt_resist);
int32_t alpha_living_fall_damage(float distance);
bool alpha_living_heading(const HeadingWorld* world, float strafe, float forward,
                          bool jumping, bool on_ground, float yaw, HeadingIo* io);

// Creature steering math (entity_ai.rs). Closed-form angles only;
// phases and path objects stay in C++.
typedef struct SteerOut {
    float new_yaw;
    float strafe;
    float forward;
    bool jump;
} SteerOut;

float alpha_ai_clamp_angle(float current, float target, float max_delta);
bool alpha_ai_face_angles(double dx, double dz, double dy, float cur_yaw, float cur_pitch,
                          float max_turn, float* out_yaw, float* out_pitch);
bool alpha_ai_steer_to_point(double dx, double dz, double dy, float cur_yaw,
                             bool is_attacking, bool has_target,
                             double tgt_dx, double tgt_dz, float forward_in, SteerOut* out);


#ifdef __cplusplus
}
#endif


