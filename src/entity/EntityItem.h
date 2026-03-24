#pragma once

#include "Entity.h"

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
};
