#include "RustBridge.h"
#include "../../rust/alpha_bridge/alpha_bridge.h"
#include "Logger.h"
#include <cstring>

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

} // namespace RustBridge
