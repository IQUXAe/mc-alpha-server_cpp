#pragma once

#include "Entity.h"
#include "../core/Material.h"
#include "../core/NBT.h"
#include "../core/RustBridge.h"
#include "../world/World.h"
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <string>

// Heading-driver trampolines live in Entity.cpp (single definition).
class EntityLiving;
void setHeadingEntity(EntityLiving* entity);
RustBridge::HeadingWorld headingWorld();

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
        if (Entity* rider = getRiddenByEntity()) {
            rider->mountEntity(nullptr);
        }
        if (getRidingEntity()) {
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
        health = RustBridge::livingHeal(health, maxHealth, amount, isDead);
    }

    void attackEntityFrom(Entity* attacker, int amount) override {
        RustBridge::AttackResult r{};
        const bool hasAttacker = attacker != nullptr;
        const double atkX = hasAttacker ? attacker->posX : 0.0;
        const double atkZ = hasAttacker ? attacker->posZ : 0.0;
        if (!RustBridge::livingAttack(health, hurtResistantTime, maxHurtResistantTime, lastDamage,
                                      hurtTime, attackTime, isDead, amount, hasAttacker,
                                      posX, posZ, atkX, atkZ, motionX, motionY, motionZ, &r)) {
            return;
        }
        health = r.health;
        lastDamage = r.last_damage;
        hurtResistantTime = r.hurt_resist;
        hurtTime = r.hurt_time;
        attackTime = r.attack_time;
        if (r.knocked) {
            motionX = r.kmx;
            motionY = r.kmy;
            motionZ = r.kmz;
        }
        if (r.send_status && worldObj) {
            worldObj->sendEntityStatus(this, 2);
        }
        if (r.died) {
            onDeath();
        }
    }

    float getEyeHeight() const override {
        return height * 0.85f;
    }

    void onFall(float distance) override {
        const int damage = RustBridge::livingFallDamage(distance);
        if (damage > 0) {
            attackEntityFrom(nullptr, damage);
        }
    }

    void tick() override {
        Entity::tick();
        const RustBridge::LivingTick t = RustBridge::livingTick(
            isEntityAlive(), isInsideOpaqueBlock(), isInsideMaterial(&Material::water),
            air, hurtTime, attackTime, hurtResistantTime);
        air = t.air;
        hurtTime = t.hurt_time;
        attackTime = t.attack_time;
        hurtResistantTime = t.hurt_resist;
        if (t.suffocate) {
            attackEntityFrom(nullptr, 1);
        }
        if (t.drown) {
            attackEntityFrom(nullptr, 2);
        }
    }

    // Movement helpers (shared by all living entities)
    bool isTouchingLiquid() const {
        if (!worldObj) return false;
        const int x = MathHelper::floor_double(posX);
        const int y = MathHelper::floor_double(boundingBox.minY);
        const int z = MathHelper::floor_double(posZ);
        auto* mat1 = worldObj->getBlockMaterialNoChunkLoad(x, y, z);
        auto* mat2 = worldObj->getBlockMaterialNoChunkLoad(x, y + 1, z);
        return (mat1 && mat1->getIsLiquid()) || (mat2 && mat2->getIsLiquid());
    }

    void moveEntityWithHeading(float strafe, float forward) {
        struct HeadingGuard {
            explicit HeadingGuard(EntityLiving* self) { setHeadingEntity(self); }
            ~HeadingGuard() { setHeadingEntity(nullptr); }
        };
        HeadingGuard guard(this);
        RustBridge::HeadingIo io{motionX, motionY, motionZ, fallDistance};
        const RustBridge::HeadingWorld world = headingWorld();
        if (!RustBridge::livingHeading(&world, strafe, forward,
                                       isJumping_, onGround, rotationYaw, &io)) {
            return;
        }
        motionX = io.motion_x;
        motionY = io.motion_y;
        motionZ = io.motion_z;
        fallDistance = io.fall_distance;
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
    int score = 0;
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
