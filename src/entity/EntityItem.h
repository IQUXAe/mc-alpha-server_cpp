#pragma once

#include "Entity.h"
#include "../world/World.h"
#include "../block/Block.h"
#include "../core/MathHelper.h"
#include "../core/RustBridge.h"
#include <cstdlib>

class ItemStack;

class EntityItem : public Entity {
public:
    int itemID;
    int count;
    int metadata;
    int age = 0;
    int pickupDelay = 10;

    EntityItem(int id, int cnt, int meta) : itemID(id), count(cnt), metadata(meta) {
        width = 0.25f;
        height = 0.25f;
        yOffset = height / 2.0f; // 0.125 — matches Java: this.yOffset = this.height / 2.0F
    }

    void tick() override {
        Entity::tick();
        if (pickupDelay > 0) pickupDelay--;
        age++;

        motionY -= 0.04;
        pushOutOfBlocks();
        moveEntity(motionX, motionY, motionZ);

        // Java: var1 = 0.98, if onGround: var1 = slipperiness * 0.98 (default slipperiness = 0.6)
        RustBridge::ItemMotion motion{motionX, motionY, motionZ};
        RustBridge::itemDamp(onGround, &motion);
        motionX = motion.mx;
        motionY = motion.my;
        motionZ = motion.mz;

        if (age >= 6000) isDead = true;
    }

private:
    void pushOutOfBlocks() {
        if (!worldObj) {
            return;
        }

        const int x = MathHelper::floor_double(posX);
        const int y = MathHelper::floor_double(posY);
        const int z = MathHelper::floor_double(posZ);
        const double localX = posX - static_cast<double>(x);
        const double localY = posY - static_cast<double>(y);
        const double localZ = posZ - static_cast<double>(z);

        if (!worldObj->isBlockSolidNoChunkLoad(x, y, z)) {
            return;
        }

        const int8_t bestSide = RustBridge::itemPushSide(
            !worldObj->isBlockSolidNoChunkLoad(x - 1, y, z),
            !worldObj->isBlockSolidNoChunkLoad(x + 1, y, z),
            !worldObj->isBlockSolidNoChunkLoad(x, y - 1, z),
            !worldObj->isBlockSolidNoChunkLoad(x, y + 1, z),
            !worldObj->isBlockSolidNoChunkLoad(x, y, z - 1),
            !worldObj->isBlockSolidNoChunkLoad(x, y, z + 1),
            localX, localY, localZ);
        if (bestSide < 0) {
            return;
        }

        const double impulse = RustBridge::rngNextDouble() * 0.2 + 0.1;
        if (bestSide == 0) motionX = -impulse;
        if (bestSide == 1) motionX = impulse;
        if (bestSide == 2) motionY = -impulse;
        if (bestSide == 3) motionY = impulse;
        if (bestSide == 4) motionZ = -impulse;
        if (bestSide == 5) motionZ = impulse;
    }
};
