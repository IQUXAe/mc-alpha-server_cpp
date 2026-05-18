#pragma once

#include "Entity.h"
#include "../core/Material.h"
#include "../core/NBT.h"
#include "../world/World.h"
#include <cmath>
#include <cstdlib>
#include <numbers>
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
        if (riddenByEntity) {
            riddenByEntity->mountEntity(nullptr);
        }
        if (ridingEntity) {
            mountEntity(nullptr);
        }
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
    bool preventsEntitySpawning() const override { return true; }
    virtual void writeToNBT(NBTCompound&) const {}
    virtual void readFromNBT(const NBTCompound&) {}

    virtual void damageEntity(int amount) { attackEntityFrom(nullptr, amount); }
    virtual void heal(int amount) {
        if (amount <= 0 || isDead || health <= 0) {
            return;
        }
        health = static_cast<int16_t>(std::min<int>(maxHealth, health + amount));
    }

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

    // Movement helpers (shared by all living entities)
    bool isTouchingLiquid() const {
        if (!worldObj) return false;
        const int x = MathHelper::floor_double(posX);
        const int y = MathHelper::floor_double(boundingBox.minY);
        const int z = MathHelper::floor_double(posZ);
        return worldObj->getBlockMaterialNoChunkLoad(x, y, z)->getIsLiquid()
            || worldObj->getBlockMaterialNoChunkLoad(x, y + 1, z)->getIsLiquid();
    }

    void moveFlying(float strafe, float forward, float acceleration) {
        float magnitude = strafe * strafe + forward * forward;
        if (magnitude < 1.0e-4f) return;
        magnitude = MathHelper::sqrt_float(magnitude);
        if (magnitude < 1.0f) magnitude = 1.0f;
        magnitude = acceleration / magnitude;
        strafe *= magnitude;
        forward *= magnitude;
        const float radians = rotationYaw * static_cast<float>(std::numbers::pi_v<double> / 180.0);
        const float sinYaw = MathHelper::sin(radians);
        const float cosYaw = MathHelper::cos(radians);
        motionX += strafe * cosYaw - forward * sinYaw;
        motionZ += forward * cosYaw + strafe * sinYaw;
    }

    void moveEntityWithHeading(float strafe, float forward) {
        const bool touchingLiquid = isTouchingLiquid();

        if (isJumping_) {
            if (touchingLiquid) {
                motionY += 0.04;
            } else if (onGround) {
                motionY = 0.42;
            }
        }

        if (touchingLiquid) {
            const double startY = posY;
            moveFlying(strafe, forward, 0.02f);
            moveEntity(motionX, motionY, motionZ);
            motionX *= 0.8;
            motionY *= 0.8;
            motionZ *= 0.8;
            motionY -= 0.02;
            if ((onGround || posY <= startY) && isJumping_) {
                motionY = 0.3;
            }
            return;
        }

        float friction = 0.91f;
        if (onGround) {
            friction = 0.546f;
        }

        const float accelerationScale = 0.16277136f / (friction * friction * friction);
        moveFlying(strafe, forward, onGround ? 0.1f * accelerationScale : 0.02f);

        // Ladder / wall-climbing (func_144_E)
        if (isOnLadder()) {
            fallDistance = 0.0f;
            if (motionY < -0.15) {
                motionY = -0.15;
            }
        }

        moveEntity(motionX, motionY, motionZ);

        if (collidedVertically && isOnLadder()) {
            motionY = 0.2;
        }

        motionY -= 0.08;
        motionY *= 0.98;
        motionX *= friction;
        motionZ *= friction;
    }

    // Check if on a ladder (func_144_E)
    virtual bool isOnLadder() const {
        if (!worldObj) return false;
        const int x = MathHelper::floor_double(posX);
        const int y = MathHelper::floor_double(boundingBox.minY);
        const int z = MathHelper::floor_double(posZ);
        const uint8_t b0 = worldObj->getBlockId(x, y, z);
        const uint8_t b1 = worldObj->getBlockId(x, y + 1, z);
        // Block 65 = ladder
        return b0 == 65 || b1 == 65;
    }

protected:
    bool isJumping_ = false;
};

class EntityPlayer : public EntityLiving {
public:
    std::string username;
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
