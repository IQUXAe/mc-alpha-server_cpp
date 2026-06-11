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

FfiItemStack itemStackCreate(int32_t itemID, int32_t stackSize, int32_t itemDamage) {
    FfiItemStack result;
    ::FfiItemStack raw = ::item_stack_create(itemID, stackSize, itemDamage);
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}

FfiItemStack itemStackCopy(const FfiItemStack* stack) {
    FfiItemStack result;
    ::FfiItemStack raw = ::item_stack_copy(reinterpret_cast<const ::FfiItemStack*>(stack));
    std::memcpy(&result, &raw, sizeof(result));
    return result;
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

bool enabled() {
    return true;
}

std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& input, int level) {
    return fromRustBuffer(alpha_gzip_compress(input.data(), input.size(), level));
}

std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& input) {
    return fromRustBuffer(alpha_gzip_decompress(input.data(), input.size()));
}

std::vector<uint8_t> zstdCompress(const std::vector<uint8_t>& input, int level) {
    return fromRustBuffer(alpha_zstd_compress(input.data(), input.size(), level));
}

std::vector<uint8_t> zstdDecompress(const std::vector<uint8_t>& input) {
    return fromRustBuffer(alpha_zstd_decompress(input.data(), input.size()));
}

std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& input, int level) {
    return fromRustBuffer(alpha_zlib_compress(input.data(), input.size(), level));
}

std::vector<uint8_t> zlibDecompress(const std::vector<uint8_t>& input) {
    return fromRustBuffer(alpha_zlib_decompress(input.data(), input.size()));
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
    return false;
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

} // namespace RustBridge
