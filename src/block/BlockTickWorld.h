#pragma once

// Shared FFI trampolines for block behavior ports (block_ticks.rs,
// block_fire.rs). Rust owns the decisions; these feed it RNG draws, block
// storage/light, scheduling, spawning, and drops. The guard swaps in the
// calling world and restores the previous one (drop callbacks can nest).

#include "Block.h"
#include "../core/Material.h"
#include "../core/RustBridge.h"

#include <cstdint>

class World;

extern thread_local World* gBlockTickWorld;

struct BlockTickGuard {
    explicit BlockTickGuard(World* world) : prev_(gBlockTickWorld) { gBlockTickWorld = world; }
    ~BlockTickGuard() { gBlockTickWorld = prev_; }

private:
    World* prev_;
};

// Trampolines, defined once in Block.cpp.
extern "C" int32_t blockTickNextInt(int32_t bound);
extern "C" float blockTickNextFloat01();
extern "C" uint64_t blockTickNextU64();
extern "C" uint8_t blockTickGetId(int32_t x, int32_t y, int32_t z);
extern "C" uint8_t blockTickGetIdNc(int32_t x, int32_t y, int32_t z);
extern "C" uint8_t blockTickGetMeta(int32_t x, int32_t y, int32_t z);
extern "C" void blockTickSet(int32_t x, int32_t y, int32_t z, uint8_t id);
extern "C" void blockTickSetMeta(int32_t x, int32_t y, int32_t z, uint8_t meta);
extern "C" void blockTickSetNotify(int32_t x, int32_t y, int32_t z, uint8_t id);
extern "C" void blockTickSetUpdate(int32_t x, int32_t y, int32_t z, uint8_t id);
extern "C" void blockTickSetMetaNotify(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t meta);
extern "C" void blockTickSetAndMeta(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t meta);
extern "C" int32_t blockTickLight(int32_t x, int32_t y, int32_t z);
extern "C" bool blockTickSeeSky(int32_t x, int32_t y, int32_t z);
extern "C" bool blockTickAttachWorld(int32_t x, int32_t y, int32_t z);
extern "C" bool blockTickAttachTorch(int32_t x, int32_t y, int32_t z);
extern "C" bool blockTickIsSolid(int32_t x, int32_t y, int32_t z);
extern "C" bool blockTickIsSolidNc(int32_t x, int32_t y, int32_t z);
extern "C" bool blockTickWaterLava(int32_t x, int32_t y, int32_t z);
extern "C" bool blockTickIsWater(int32_t x, int32_t y, int32_t z);
extern "C" bool blockTickRegistered(uint8_t id);
extern "C" bool blockTickCollidableBox(int32_t x, int32_t y, int32_t z);
extern "C" void blockTickSchedule(int32_t x, int32_t y, int32_t z, uint8_t id, int32_t delay);
extern "C" void blockTickMark(int32_t x, int32_t y, int32_t z);
extern "C" void blockTickNotifyNeighbors(int32_t x, int32_t y, int32_t z, uint8_t id);
extern "C" void blockTickSpawnDrop(int32_t itemId, int32_t count, int32_t damage, double fx, double fy,
                                    double fz, double spread, double up);
extern "C" void blockTickSpawnFalling(uint8_t blockId, double fx, double fy, double fz);
extern "C" void blockTickDropOccupant(int32_t x, int32_t y, int32_t z);
extern "C" void blockTickDetonateTnt(int32_t x, int32_t y, int32_t z);

// Container scatter hooks (global RNG draws + item spawn; slot loops stay in C++).
extern "C" int32_t scatterNextInt(int32_t bound);
extern "C" float scatterNextFloat01();
extern "C" double scatterNextFloat64();
extern "C" void scatterSpawnItem(int32_t itemId, int32_t count, int32_t damage, double fx, double fy,
                                 double fz, double mx, double my, double mz);
extern "C" uint8_t scatterGetBlockId(int32_t x, int32_t y, int32_t z);

inline const RustBridge::BlockTickWorld& blockTickWorld() {
    static const RustBridge::BlockTickWorld table = {
        &blockTickNextInt,
        &blockTickNextFloat01,
        &blockTickNextU64,
        &blockTickGetId,
        &blockTickGetIdNc,
        &blockTickGetMeta,
        &blockTickSet,
        &blockTickSetMeta,
        &blockTickSetNotify,
        &blockTickSetUpdate,
        &blockTickSetMetaNotify,
        &blockTickSetAndMeta,
        &blockTickLight,
        &blockTickSeeSky,
        &blockTickAttachWorld,
        &blockTickAttachTorch,
        &blockTickIsSolid,
        &blockTickIsSolidNc,
        &blockTickWaterLava,
        &blockTickIsWater,
        &blockTickRegistered,
        &blockTickCollidableBox,
        &blockTickSchedule,
        &blockTickMark,
        &blockTickNotifyNeighbors,
        &blockTickSpawnDrop,
        &blockTickSpawnFalling,
        &blockTickDropOccupant,
    };
    return table;
}

inline const RustBridge::FireWorld& fireWorld() {
    static const RustBridge::FireWorld table = {&blockTickWorld(), &blockTickDetonateTnt};
    return table;
}

inline const RustBridge::ScatterWorld& scatterWorld() {
    static const RustBridge::ScatterWorld table = {
        &scatterNextInt,
        &scatterNextFloat01,
        &scatterNextFloat64,
        &scatterSpawnItem,
    };
    return table;
}
