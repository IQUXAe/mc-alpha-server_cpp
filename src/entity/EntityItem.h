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
        yOffset = 0.0f;
    }

    void tick() override {
        Entity::tick();
        if (pickupDelay > 0) pickupDelay--;
        age++;

        // Basic gravity and friction for drops
        motionY -= 0.04;
        moveEntity(motionX, motionY, motionZ);
        motionX *= 0.98;
        motionY *= 0.98;
        motionZ *= 0.98;

        if (onGround) {
            motionY *= -0.5;
            motionX *= 0.7;
            motionZ *= 0.7;
        }

        if (age > 6000) isDead = true; // 5 minutes
    }
};
