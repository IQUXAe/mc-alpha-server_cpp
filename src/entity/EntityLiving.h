#pragma once

#include "Entity.h"
#include <string>

class EntityLiving : public Entity {
public:
    int16_t health = 20;
    int16_t maxHealth = 20;
    int hurtTime = 0;
    int deathTime = 0;
    float moveSpeed = 0.7f;

    virtual void onDeath() {
        isDead = true;
    }

    virtual void damageEntity(int amount) {
        health -= amount;
        if (health <= 0) {
            onDeath();
        }
    }

    void tick() override {
        Entity::tick();
        if (hurtTime > 0) hurtTime--;
    }
};

class EntityPlayer : public EntityLiving {
public:
    std::string username;
    // Inventory inventory; // TODO
    // ItemStack heldItem; // TODO
    bool sleeping = false;
    float foodLevel = 20.0f;

    EntityPlayer() {
        height = 1.8f;
        width = 0.6f;
        yOffset = 1.62f;
    }

    void tick() override {
        EntityLiving::tick();
    }
};
