#pragma once

#include "Entity.h"
#include "../world/World.h"
#include "../block/Block.h"
#include "../core/MathHelper.h"

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
        float var1 = 0.98f;
        if (onGround) {
            var1 = 0.6f * 0.98f; // default block slipperiness
            // TODO: read actual block slipperiness from block below
        }

        motionX *= var1;
        motionY *= 0.98;
        motionZ *= var1;
        if (onGround) {
            motionY *= -0.5;
        }

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

        const bool freeWest = !worldObj->isBlockSolidNoChunkLoad(x - 1, y, z);
        const bool freeEast = !worldObj->isBlockSolidNoChunkLoad(x + 1, y, z);
        const bool freeDown = !worldObj->isBlockSolidNoChunkLoad(x, y - 1, z);
        const bool freeUp = !worldObj->isBlockSolidNoChunkLoad(x, y + 1, z);
        const bool freeNorth = !worldObj->isBlockSolidNoChunkLoad(x, y, z - 1);
        const bool freeSouth = !worldObj->isBlockSolidNoChunkLoad(x, y, z + 1);

        int bestSide = -1;
        double bestDistance = 9999.0;

        if (freeWest && localX < bestDistance) {
            bestDistance = localX;
            bestSide = 0;
        }
        if (freeEast && 1.0 - localX < bestDistance) {
            bestDistance = 1.0 - localX;
            bestSide = 1;
        }
        if (freeDown && localY < bestDistance) {
            bestDistance = localY;
            bestSide = 2;
        }
        if (freeUp && 1.0 - localY < bestDistance) {
            bestDistance = 1.0 - localY;
            bestSide = 3;
        }
        if (freeNorth && localZ < bestDistance) {
            bestDistance = localZ;
            bestSide = 4;
        }
        if (freeSouth && 1.0 - localZ < bestDistance) {
            bestDistance = 1.0 - localZ;
            bestSide = 5;
        }

        if (bestSide < 0) {
            return;
        }

        const double impulse = (static_cast<double>(std::rand()) / static_cast<double>(RAND_MAX)) * 0.2 + 0.1;
        if (bestSide == 0) motionX = -impulse;
        if (bestSide == 1) motionX = impulse;
        if (bestSide == 2) motionY = -impulse;
        if (bestSide == 3) motionY = impulse;
        if (bestSide == 4) motionZ = -impulse;
        if (bestSide == 5) motionZ = impulse;
    }
};
