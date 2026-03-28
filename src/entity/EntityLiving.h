#pragma once

#include "Entity.h"
#include "../core/Material.h"
#include "../core/NBT.h"
#include "../world/World.h"
#include <cmath>
#include <cstdlib>
#include <string>

class EntityLiving : public Entity {
public:
    int16_t health = 20;
    int16_t maxHealth = 20;
    int hurtTime = 0;
    int deathTime = 0;
    int attackTime = 0;
    int hurtResistantTime = 0;
    int maxHurtResistantTime = 20;
    int lastDamage = 0;
    float moveSpeed = 0.7f;
    bool isSneaking = false;

    virtual void onDeath() {
        if (worldObj) {
            worldObj->sendEntityStatus(this, 3);
        }
        isDead = true;
    }

    virtual int getMobTypeId() const { return 0; }
    virtual std::string getEntityStringId() const { return {}; }
    virtual int getTrackingRange() const { return 160; }
    virtual int getTrackingRate() const { return 3; }
    virtual bool shouldSendVelocity() const { return false; }
    virtual void writeToNBT(NBTCompound&) const {}
    virtual void readFromNBT(const NBTCompound&) {}

    virtual void damageEntity(int amount) { attackEntityFrom(nullptr, amount); }

    void attackEntityFrom(Entity* attacker, int amount) override {
        if (amount <= 0 || isDead || health <= 0) {
            return;
        }

        bool applyKnockback = true;
        if (hurtResistantTime > maxHurtResistantTime / 2) {
            if (amount <= lastDamage) {
                return;
            }

            health -= static_cast<int16_t>(amount - lastDamage);
            lastDamage = amount;
            applyKnockback = false;
        } else {
            lastDamage = amount;
            hurtResistantTime = maxHurtResistantTime;
            hurtTime = 10;
            attackTime = 10;
            health -= static_cast<int16_t>(amount);
        }

        if (applyKnockback && attacker) {
            double dx = attacker->posX - posX;
            double dz = attacker->posZ - posZ;
            while (dx * dx + dz * dz < 1.0E-4) {
                dx = (static_cast<double>(std::rand()) / RAND_MAX - static_cast<double>(std::rand()) / RAND_MAX) * 0.01;
                dz = (static_cast<double>(std::rand()) / RAND_MAX - static_cast<double>(std::rand()) / RAND_MAX) * 0.01;
            }

            const double dist = MathHelper::sqrt_double(dx * dx + dz * dz);
            motionX *= 0.5;
            motionY *= 0.5;
            motionZ *= 0.5;
            motionX -= dx / dist * 0.4;
            motionY += 0.4;
            motionZ -= dz / dist * 0.4;
            if (motionY > 0.4) {
                motionY = 0.4;
            }
        }

        if (applyKnockback && worldObj) {
            worldObj->sendEntityStatus(this, 2);
        }

        if (health <= 0) {
            onDeath();
        }
    }

    float getEyeHeight() const override {
        return height * 0.85f;
    }

    void onFall(float distance) override {
        const int damage = static_cast<int>(std::ceil(distance - 3.0f));
        if (damage > 0) {
            attackEntityFrom(nullptr, damage);
        }
    }

    void tick() override {
        Entity::tick();
        if (isEntityAlive() && isInsideOpaqueBlock()) {
            attackEntityFrom(nullptr, 1);
        }

        if (isEntityAlive() && isInsideMaterial(&Material::water)) {
            --air;
            if (air <= -20) {
                air = 0;
                attackEntityFrom(nullptr, 2);
            }
        } else {
            air = 300;
        }

        if (hurtTime > 0) hurtTime--;
        if (attackTime > 0) attackTime--;
        if (hurtResistantTime > 0) hurtResistantTime--;
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
        yOffset = 0.0f;
        fireResistance = 20;
    }

    void tick() override {
        EntityLiving::tick();
    }

    float getEyeHeight() const override {
        return 1.62f;
    }
};
