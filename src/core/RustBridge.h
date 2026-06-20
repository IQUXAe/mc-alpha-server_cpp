#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>

#include "../../rust/alpha_bridge/alpha_bridge.h" // AlphaBlockProperties

class Material;

namespace RustBridge {

using FfiItemStack = ::FfiItemStack;

FfiItemStack itemStackCreate(int32_t itemID, int32_t stackSize, int32_t itemDamage);
FfiItemStack itemStackCopy(const FfiItemStack* stack);
bool itemStackDamage(FfiItemStack* stack, int32_t damage, int32_t maxDamage);

using FfiFurnaceState = ::FfiFurnaceState;

struct FurnaceTickResult {
    bool changed;
    bool needsBlockUpdate;
};
static_assert(sizeof(FurnaceTickResult) == 2, "FurnaceTickResult must be 2 bytes (2 bools)");
static_assert(alignof(FurnaceTickResult) == 1, "FurnaceTickResult must have byte alignment");

FfiFurnaceState furnaceCreate();
FurnaceTickResult furnaceTick(FfiFurnaceState* state, int32_t fuelBurnTime);

using FfiChestState = ::FfiChestState;

FfiChestState chestCreate();

using FfiSignState = ::FfiSignState;

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

// Block properties from Rust data table
AlphaBlockProperties blockProperties(uint32_t id);
Material* materialFromId(uint8_t materialId);

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
