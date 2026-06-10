#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>

namespace RustBridge {

struct FfiItemStack {
    int32_t stackSize;
    int32_t animationsToGo;
    int32_t itemID;
    int32_t itemDamage;
};
static_assert(std::is_standard_layout_v<FfiItemStack>, "FfiItemStack must be standard layout");

FfiItemStack itemStackCreate(int32_t itemID, int32_t stackSize, int32_t itemDamage);
FfiItemStack itemStackCopy(const FfiItemStack* stack);
bool itemStackDamage(FfiItemStack* stack, int32_t damage, int32_t maxDamage);

struct LevelDatData {
    int64_t randomSeed = 0;
    int32_t spawnX = 0;
    int32_t spawnY = 64;
    int32_t spawnZ = 0;
    int64_t worldTime = 0;
    int64_t sizeOnDisk = 0;
    int32_t version = 19132;
    std::string levelName = "world";
};

bool enabled();

std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& input, int level = -1);
std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& input);

std::vector<uint8_t> zstdCompress(const std::vector<uint8_t>& input, int level = 1);
std::vector<uint8_t> zstdDecompress(const std::vector<uint8_t>& input);

std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& input, int level = -1);
std::vector<uint8_t> zlibDecompress(const std::vector<uint8_t>& input);

std::vector<uint8_t> encodeLevelDat(const LevelDatData& level);
bool decodeLevelDat(const std::vector<uint8_t>& input, LevelDatData& outLevel);

} // namespace RustBridge
