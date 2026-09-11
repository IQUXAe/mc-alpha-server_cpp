#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>

#include "../../rust/alpha_bridge/alpha_bridge.h" // AlphaBlockProperties

class Material;

namespace RustBridge {

using FfiItemStack = ::FfiItemStack;

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

std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& input, int level = -1);
std::vector<uint8_t> gzipDecompress(const uint8_t* input, size_t len);

std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& input, int level = -1);

std::vector<uint8_t> encodeLevelDat(const LevelDatData& level);
bool decodeLevelDat(const std::vector<uint8_t>& input, LevelDatData& outLevel);

using AlphaPlayerData = ::AlphaPlayerData;
using FfiPlayerSlot = ::FfiPlayerSlot;

bool savePlayerData(const std::string& filepath, const AlphaPlayerData& data);
bool loadPlayerData(const std::string& filepath, AlphaPlayerData& outData);

// Thread-safe RNG — backed by Rust's global Mutex<JavaRandom> seeded from /dev/urandom.
int32_t rngNextInt(int32_t bound);
float   rngNextFloat();
double  rngNextDouble();

using FfiCombatResult = ::FfiCombatResult;
using FfiMovementInput = ::FfiMovementInput;
using FfiMovementResult = ::FfiMovementResult;

// Player Inventory & Crafting
int32_t inventoryAddItem(FfiItemStack* slots, size_t slotsLen, FfiItemStack* stack, int32_t stackLimit = 64);
int32_t inventoryCalcArmor(const FfiItemStack* armorSlots, size_t armorLen);
void inventoryDamageArmor(FfiItemStack* armorSlots, size_t armorLen, int32_t damageAmount);
FfiItemStack inventoryCraft2x2(const FfiItemStack* grid);
void inventoryConsumeCraft2x2(FfiItemStack* grid);

// Player Combat
FfiCombatResult calculateCombatDamage(int32_t rawDamage, bool attackerIsPlayer, int32_t difficulty, int32_t armorValue, int32_t armorDamageCarry);
int32_t getWeaponDamage(int32_t itemId);

// Player Movement
FfiMovementResult validateMovement(const FfiMovementInput& input);

// Player Mining
bool miningCanHarvest(int32_t blockId, int32_t heldItemId);
float miningGetStrVsBlock(int32_t blockId, int32_t heldItemId);
float miningCheckHardness(int32_t blockId, int32_t heldItemId, bool inWater, bool onGround);
int32_t miningGetDestroyTicks(int32_t blockId, int32_t heldItemId, bool inWater, bool onGround);

// Player digging state machine (owns progressive-dig state in Rust)
using FfiDigState = ::FfiDigState;
using FfiDigInput = ::FfiDigInput;
FfiDigState digStateNew();
void digCancel(FfiDigState* state);
bool digOnClick(const FfiDigInput& input);
bool digOnTick(FfiDigState* state, int32_t x, int32_t y, int32_t z, const FfiDigInput& input);

// Entity tracker math (pure functions, no allocation)
int32_t trackerEncodePos(double pos);
int8_t trackerEncodeRot(float degrees);
uint8_t trackerMoveKind(int32_t dx, int32_t dy, int32_t dz, bool moved, bool turned);
bool trackerVelocityChanged(double motionX, double motionY, double motionZ,
                            double lastX, double lastY, double lastZ,
                            bool sendVelocity);
bool trackerInRange(double playerX, double playerZ,
                    int32_t lastFixedX, int32_t lastFixedZ,
                    int32_t trackingRange);

// Mob spawning math (pure functions, no allocation)
int32_t spawnMaxCount(int32_t numEligibleChunks, int32_t budgetPer256);
int32_t spawnPackOffset(int32_t first, int32_t second);
bool spawnTooCloseToSpawn(float fx, float fy, float fz,
                          int32_t spawnX, int32_t spawnY, int32_t spawnZ);

// Mob spawning batch drivers (Rust owns control flow; C++ owns RNG/world)
using SpawnerWorld = ::SpawnerWorld;
int32_t spawnHostile(const SpawnerWorld* world,
                     const double* playerX, const double* playerY, const double* playerZ,
                     size_t numPlayers, int32_t currentCount,
                     int32_t spawnX, int32_t spawnY, int32_t spawnZ,
                     int32_t worldHeight);
int32_t spawnPassive(const SpawnerWorld* world,
                     const double* playerX, const double* playerY, const double* playerZ,
                     size_t numPlayers, int32_t currentCount,
                     int32_t spawnX, int32_t spawnY, int32_t spawnZ,
                     int32_t worldHeight);

// Block behaviors (Rust owns decisions; C++ owns registry/objects)
using BlockTickWorld = ::BlockTickWorld;
using FireWorld = ::FireWorld;
using TickAction = ::TickAction;
void blockFireTick(const FireWorld* w, uint8_t fireId, int32_t tickRate, int32_t x, int32_t y, int32_t z);
bool blockFireCanPlace(const FireWorld* w, int32_t x, int32_t y, int32_t z);
void blockFireNeighbor(const FireWorld* w, int32_t x, int32_t y, int32_t z);
void blockFireAdded(const FireWorld* w, uint8_t fireId, int32_t tickRate, int32_t x, int32_t y, int32_t z);

// Container blocks (slot iteration stays in C++)
using ScatterWorld = ::ScatterWorld;
int32_t blockChestScatterStack(const ScatterWorld* w, int32_t itemId, int32_t count,
                               int32_t damage, int32_t x, int32_t y, int32_t z);
void blockFurnaceScatterStack(const ScatterWorld* w, int32_t itemId, int32_t count,
                              int32_t damage, int32_t x, int32_t y, int32_t z);
bool blockChestCanPlace(uint8_t (*getBlockId)(int32_t x, int32_t y, int32_t z),
                        uint8_t chestId, int32_t x, int32_t y, int32_t z);

// Small-entity kernels (pointers stay in C++)
using ItemMotion = ::ItemMotion;
int8_t itemPushSide(bool freeW, bool freeE, bool freeD, bool freeU,
                    bool freeN, bool freeS, double lx, double ly, double lz);
bool itemDamp(bool onGround, ItemMotion* io);
uint8_t fallingLand(int32_t blockId, bool onGround, int32_t by, int32_t landId,
                    bool landReplaceable, bool haveBlock, int32_t fallTime);
double boatWaterFraction(double minX, double minY, double minZ,
                         double maxX, double maxY, double maxZ,
                         bool (*isWater)(int32_t x, int32_t y, int32_t z));
bool boatSteer(double deltaX, double deltaZ, float curYaw, float* outYaw);
bool boatRiderOffset(float yaw, double* outX, double* outZ);
bool arrowFaceVelocity(double mx, double my, double mz, float* outYaw, float* outPitch);
void blockSandAdded(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
void blockSandNeighbor(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
void blockSandTick(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
void blockFluidAdded(const BlockTickWorld* w, uint8_t id, int32_t rate, int32_t x, int32_t y, int32_t z);
void blockFluidNeighbor(const BlockTickWorld* w, uint8_t id, int32_t rate, int32_t x, int32_t y, int32_t z);
void blockFluidTick(const BlockTickWorld* w, uint8_t id, bool lava, int32_t x, int32_t y, int32_t z);
bool blockFlowerCanStay(const BlockTickWorld* w, int32_t x, int32_t y, int32_t z);
void blockFlowerNeighbor(const BlockTickWorld* w, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
void blockFlowerTick(const BlockTickWorld* w, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
void blockTallgrassDrop(const BlockTickWorld* w, int32_t seedsId, float chance, int32_t x, int32_t y, int32_t z);
bool blockMushroomCanStay(const BlockTickWorld* w, int32_t x, int32_t y, int32_t z);
void blockMushroomNeighbor(const BlockTickWorld* w, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
uint8_t blockTorchAttachMeta(const BlockTickWorld* w, int32_t side, int32_t x, int32_t y, int32_t z);
void blockTorchAdded(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
bool blockTorchCanStay(const BlockTickWorld* w, int32_t x, int32_t y, int32_t z);
void blockTorchNeighbor(const BlockTickWorld* w, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
bool blockCactusCanStay(const BlockTickWorld* w, int32_t x, int32_t y, int32_t z);
bool blockReedCanStay(const BlockTickWorld* w, int32_t x, int32_t y, int32_t z);
void blockCactusAdded(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
void blockReedAdded(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
void blockCactusNeighbor(const BlockTickWorld* w, uint8_t id, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
void blockReedNeighbor(const BlockTickWorld* w, uint8_t id, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
void blockCactusTick(const BlockTickWorld* w, uint8_t id, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
void blockReedTick(const BlockTickWorld* w, uint8_t id, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
void blockLeavesAdded(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
void blockLeavesNeighbor(const BlockTickWorld* w, uint8_t id, uint8_t leavesId, int32_t* guard, int32_t x, int32_t y, int32_t z);
void blockLeavesTick(const BlockTickWorld* w, uint8_t id, uint8_t leavesId, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t* guard, int32_t x, int32_t y, int32_t z);
void blockLeavesDrop(const BlockTickWorld* w, int32_t saplingId, float chance, int32_t x, int32_t y, int32_t z);
void blockSaplingAdded(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
bool blockSaplingCanStay(const BlockTickWorld* w, int32_t x, int32_t y, int32_t z);
void blockSaplingNeighbor(const BlockTickWorld* w, uint8_t id, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
TickAction blockSaplingTick(const BlockTickWorld* w, uint8_t id, int32_t dropId, int32_t dropCount, int32_t dropDamage, int32_t x, int32_t y, int32_t z);
void blockCropsAdded(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
bool blockCropsCanStay(const BlockTickWorld* w, uint8_t cropId, int32_t x, int32_t y, int32_t z);
void blockCropsNeighbor(const BlockTickWorld* w, uint8_t id, uint8_t cropId, int32_t wheatId, int32_t seedsId, int32_t x, int32_t y, int32_t z);
void blockCropsDrop(const BlockTickWorld* w, int32_t wheatId, int32_t seedsId, int32_t x, int32_t y, int32_t z, uint8_t meta, float chance);
void blockCropsTick(const BlockTickWorld* w, uint8_t id, uint8_t cropId, int32_t wheatId, int32_t seedsId, int32_t x, int32_t y, int32_t z);
void blockSoilAdded(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
void blockSoilTick(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
void blockSoilWalking(const BlockTickWorld* w, int32_t x, int32_t y, int32_t z);
void blockSoilNeighbor(const BlockTickWorld* w, uint8_t id, int32_t x, int32_t y, int32_t z);
bool blockBaseDrop(const BlockTickWorld* w, int32_t itemId, int32_t count, int32_t damage, int32_t x, int32_t y, int32_t z, float chance);

// Entity physics kernel (world gathers boxes; Rust resolves)
using FfiAabb = ::FfiAabb;
using ResolvedMove = ::ResolvedMove;
using PushOut = ::PushOut;
bool entityResolveMove(const FfiAabb& box, double dx, double dy, double dz,
                       const FfiAabb* boxes, size_t numBoxes, ResolvedMove* out);
float entityFallStep(bool onGround, double dy, float fallDistance, float* outFallEvent);
bool entityPush(double x1, double z1, double x2, double z2,
                bool pushable1, bool pushable2, PushOut* out);

// Living-entity logic (damage/tick/steering/fall; virtuals stay in C++)
using AttackResult = ::AttackResult;
using LivingTick = ::LivingTick;
using HeadingWorld = ::HeadingWorld;
using HeadingIo = ::HeadingIo;
using MoveFeedback = ::MoveFeedback;
int16_t livingHeal(int16_t health, int16_t maxHealth, int32_t amount, bool dead);
bool livingAttack(int16_t health, int32_t hurtResist, int32_t maxHurtResist,
                  int32_t lastDamage, int32_t hurtTimeIn, int32_t attackTimeIn,
                  bool dead, int32_t amount, bool hasAttacker,
                  double selfX, double selfZ, double atkX, double atkZ,
                  double motionX, double motionY, double motionZ,
                  AttackResult* out);
LivingTick livingTick(bool alive, bool insideOpaque, bool inWater,
                      int32_t air, int32_t hurtTime, int32_t attackTime, int32_t hurtResist);
int32_t livingFallDamage(float distance);
bool livingHeading(const HeadingWorld* world, float strafe, float forward,
                   bool jumping, bool onGround, float yaw, HeadingIo* io);
double livingNextF01();

// Creature steering math (phases stay in C++)
using SteerOut = ::SteerOut;
float aiClampAngle(float current, float target, float maxDelta);
bool aiFaceAngles(double dx, double dz, double dy, float curYaw, float curPitch,
                  float maxTurn, float* outYaw, float* outPitch);
bool aiSteerToPoint(double dx, double dz, double dy, float curYaw,
                    bool isAttacking, bool hasTarget,
                    double tgtDx, double tgtDz, float forwardIn, SteerOut* out);

} // namespace RustBridge
