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

struct FfiFurnaceState {
    FfiItemStack slots[3];
    int16_t burnTime;
    int16_t cookTime;
    int16_t currentItemBurnTime;
};
static_assert(std::is_standard_layout_v<FfiFurnaceState>, "FfiFurnaceState must be standard layout");

struct FurnaceTickResult {
    bool changed;
    bool needsBlockUpdate;
};
static_assert(sizeof(FurnaceTickResult) == 2, "FurnaceTickResult must be 2 bytes (2 bools)");
static_assert(alignof(FurnaceTickResult) == 1, "FurnaceTickResult must have byte alignment");

FfiFurnaceState furnaceCreate();
FurnaceTickResult furnaceTick(FfiFurnaceState* state, int32_t fuelBurnTime);

struct FfiChestState {
    FfiItemStack slots[27];
};
static_assert(std::is_standard_layout_v<FfiChestState>, "FfiChestState must be standard layout");

FfiChestState chestCreate();

struct FfiSignState {
    uint8_t lines[4][16];
};
static_assert(std::is_standard_layout_v<FfiSignState>, "FfiSignState must be standard layout");

FfiSignState signCreate();
void signSetLine(FfiSignState* state, int32_t line, const char* text);

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
