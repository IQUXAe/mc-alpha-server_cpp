#include "RustBridge.h"
#include "Material.h"
#include "../../rust/alpha_bridge/alpha_bridge.h"
#include "Logger.h"
#include <cstring>
#include <new>

namespace {

std::vector<uint8_t> fromRustBuffer(AlphaBuffer buffer) {
    if (!buffer.data || buffer.len == 0) {
        alpha_buffer_free(buffer);
        return {};
    }

    std::vector<uint8_t> bytes(buffer.len);
    std::memcpy(bytes.data(), buffer.data, buffer.len);
    alpha_buffer_free(buffer);
    return bytes;
}

} // namespace

namespace RustBridge {

float rngNextFloat() {
    return ::alpha_rng_next_float();
}

double rngNextDouble() {
    return ::alpha_rng_next_double();
}

bool itemStackDamage(FfiItemStack* stack, int32_t damage, int32_t maxDamage) {
    return ::item_stack_damage(reinterpret_cast<::FfiItemStack*>(stack), damage, maxDamage);
}

// end ItemStack FFI

FfiFurnaceState furnaceCreate() {
    FfiFurnaceState result;
    ::FfiFurnaceState raw = ::furnace_create();
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}

FurnaceTickResult furnaceTick(FfiFurnaceState* state, int32_t fuelBurnTime) {
    ::FurnaceTickResult raw = ::furnace_tick(
        reinterpret_cast<::FfiFurnaceState*>(state), fuelBurnTime);
    return FurnaceTickResult{raw.changed, raw.needs_block_update};
}

FfiChestState chestCreate() {
    FfiChestState result;
    ::FfiChestState raw = ::chest_create();
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}

FfiSignState signCreate() {
    FfiSignState result;
    ::FfiSignState raw = ::sign_create();
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}

void signSetLine(FfiSignState* state, int32_t line, const char* text) {
    ::sign_set_line(reinterpret_cast<::FfiSignState*>(state), line, text);
}

std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& input, int level) {
    return fromRustBuffer(alpha_gzip_compress(input.data(), input.size(), level));
}

std::vector<uint8_t> gzipDecompress(const uint8_t* input, size_t len) {
    return fromRustBuffer(alpha_gzip_decompress(input, len));
}

std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& input, int level) {
    return fromRustBuffer(alpha_zlib_compress(input.data(), input.size(), level));
}

std::vector<uint8_t> encodeLevelDat(const LevelDatData& level) {
    AlphaLevelDat ffiLevel{
        .random_seed = level.randomSeed,
        .spawn_x = level.spawnX,
        .spawn_y = level.spawnY,
        .spawn_z = level.spawnZ,
        .world_time = level.worldTime,
        .size_on_disk = level.sizeOnDisk,
        .version = level.version,
        .level_name = level.levelName.c_str(),
    };
    return fromRustBuffer(alpha_level_dat_encode(&ffiLevel));
}

bool decodeLevelDat(const std::vector<uint8_t>& input, LevelDatData& outLevel) {
    AlphaLevelDat ffiLevel{};
    if (alpha_level_dat_decode(input.data(), input.size(), &ffiLevel) != 0) {
        return false;
    }
    outLevel.randomSeed = ffiLevel.random_seed;
    outLevel.spawnX = ffiLevel.spawn_x;
    outLevel.spawnY = ffiLevel.spawn_y;
    outLevel.spawnZ = ffiLevel.spawn_z;
    outLevel.worldTime = ffiLevel.world_time;
    outLevel.sizeOnDisk = ffiLevel.size_on_disk;
    outLevel.version = ffiLevel.version;
    outLevel.levelName = ffiLevel.level_name ? ffiLevel.level_name : "world";
    alpha_level_dat_free(&ffiLevel);
    return true;
}

AlphaBlockProperties blockProperties(uint32_t id) {
    return alpha_block_properties_get(id);
}

Material* materialFromId(uint8_t materialId) {
    static Material* map[] = {
        &Material::air,      // 0
        &Material::ground,   // 1
        &Material::wood,     // 2
        &Material::rock,     // 3
        &Material::iron,     // 4
        &Material::water,    // 5
        &Material::lava,     // 6
        &Material::leaves,   // 7
        &Material::plants,   // 8
        &Material::sponge,   // 9
        &Material::cloth,    // 10
        &Material::fire,     // 11
        &Material::sand,     // 12
        &Material::circuits, // 13
        &Material::glass,    // 14
        &Material::tnt,      // 15
        &Material::unused,   // 16
        &Material::ice,      // 17
        &Material::snow,     // 18
        &Material::builtSnow,// 19
        &Material::cactus,   // 20
        &Material::clay,     // 21
        &Material::pumpkin,  // 22
        &Material::portal,   // 23
        &Material::web,      // 24
    };
    if (materialId >= sizeof(map) / sizeof(map[0])) {
        return &Material::air;
    }
    return map[materialId];
}

bool savePlayerData(const std::string& filepath, const AlphaPlayerData& data) {
    return ::alpha_player_storage_save(filepath.c_str(), &data);
}

bool loadPlayerData(const std::string& filepath, AlphaPlayerData& outData) {
    return ::alpha_player_storage_load(filepath.c_str(), &outData);
}

int32_t inventoryAddItem(FfiItemStack* slots, size_t slotsLen, FfiItemStack* stack, int32_t stackLimit) {
    return ::alpha_inventory_add_item(slots, slotsLen, stack, stackLimit);
}

int32_t inventoryCalcArmor(const FfiItemStack* armorSlots, size_t armorLen) {
    return ::alpha_inventory_calc_armor(armorSlots, armorLen);
}

void inventoryDamageArmor(FfiItemStack* armorSlots, size_t armorLen, int32_t damageAmount) {
    ::alpha_inventory_damage_armor(armorSlots, armorLen, damageAmount);
}

FfiItemStack inventoryCraft2x2(const FfiItemStack* grid) {
    return ::alpha_inventory_craft_2x2(grid);
}

void inventoryConsumeCraft2x2(FfiItemStack* grid) {
    ::alpha_inventory_consume_craft_2x2(grid);
}

FfiCombatResult calculateCombatDamage(int32_t rawDamage, bool attackerIsPlayer, int32_t difficulty, int32_t armorValue, int32_t armorDamageCarry) {
    return ::alpha_combat_calculate_damage(rawDamage, attackerIsPlayer, difficulty, armorValue, armorDamageCarry);
}

int32_t getWeaponDamage(int32_t itemId) {
    return ::alpha_combat_get_weapon_damage(itemId);
}

FfiMovementResult validateMovement(const FfiMovementInput& input) {
    return ::alpha_movement_validate(&input);
}

bool miningCanHarvest(int32_t blockId, int32_t heldItemId) {
    return ::alpha_mining_can_harvest(blockId, heldItemId);
}

float miningGetStrVsBlock(int32_t blockId, int32_t heldItemId) {
    return ::alpha_mining_get_str_vs_block(blockId, heldItemId);
}

float miningCheckHardness(int32_t blockId, int32_t heldItemId, bool inWater, bool onGround) {
    return ::alpha_mining_check_hardness(blockId, heldItemId, inWater, onGround);
}

int32_t miningGetDestroyTicks(int32_t blockId, int32_t heldItemId, bool inWater, bool onGround) {
    return ::alpha_mining_get_destroy_ticks(blockId, heldItemId, inWater, onGround);
}

FfiDigState digStateNew() {
    return ::alpha_dig_state_new();
}

void digCancel(FfiDigState* state) {
    ::alpha_dig_cancel(state);
}

bool digOnClick(const FfiDigInput& input) {
    return ::alpha_dig_on_click(input);
}

bool digOnTick(FfiDigState* state, int32_t x, int32_t y, int32_t z, const FfiDigInput& input) {
    return ::alpha_dig_on_tick(state, x, y, z, input);
}

int32_t trackerEncodePos(double pos) {
    return ::alpha_tracker_encode_pos(pos);
}

int8_t trackerEncodeRot(float degrees) {
    return ::alpha_tracker_encode_rot(degrees);
}

uint8_t trackerMoveKind(int32_t dx, int32_t dy, int32_t dz, bool moved, bool turned) {
    return ::alpha_tracker_move_kind(dx, dy, dz, moved, turned);
}

bool trackerVelocityChanged(double motionX, double motionY, double motionZ,
                            double lastX, double lastY, double lastZ,
                            bool sendVelocity) {
    return ::alpha_tracker_velocity_changed(motionX, motionY, motionZ, lastX, lastY, lastZ, sendVelocity);
}

bool trackerInRange(double playerX, double playerZ,
                    int32_t lastFixedX, int32_t lastFixedZ,
                    int32_t trackingRange) {
    return ::alpha_tracker_in_range(playerX, playerZ, lastFixedX, lastFixedZ, trackingRange);
}

int32_t spawnMaxCount(int32_t numEligibleChunks, int32_t budgetPer256) {
    return ::alpha_spawn_max_count(numEligibleChunks, budgetPer256);
}

int32_t spawnPackOffset(int32_t first, int32_t second) {
    return ::alpha_spawn_pack_offset(first, second);
}

bool spawnTooCloseToSpawn(float fx, float fy, float fz,
                          int32_t spawnX, int32_t spawnY, int32_t spawnZ) {
    return ::alpha_spawn_too_close_to_spawn(fx, fy, fz, spawnX, spawnY, spawnZ);
}

int32_t spawnHostile(const SpawnerWorld* world,
                     const double* playerX, const double* playerY, const double* playerZ,
                     size_t numPlayers, int32_t currentCount,
                     int32_t spawnX, int32_t spawnY, int32_t spawnZ,
                     int32_t worldHeight) {
    return ::rust_world_spawn_hostile(world, playerX, playerY, playerZ, numPlayers,
                                      currentCount, spawnX, spawnY, spawnZ, worldHeight);
}

int32_t spawnPassive(const SpawnerWorld* world,
                     const double* playerX, const double* playerY, const double* playerZ,
                     size_t numPlayers, int32_t currentCount,
                     int32_t spawnX, int32_t spawnY, int32_t spawnZ,
                     int32_t worldHeight) {
    return ::rust_world_spawn_passive(world, playerX, playerY, playerZ, numPlayers,
                                      currentCount, spawnX, spawnY, spawnZ, worldHeight);
}

} // namespace RustBridge
