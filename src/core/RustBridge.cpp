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

std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& input) {
    return fromRustBuffer(alpha_gzip_decompress(input.data(), input.size()));
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

} // namespace RustBridge
