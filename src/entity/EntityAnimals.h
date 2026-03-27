#pragma once

#include "EntityLiving.h"
#include "EntityItem.h"
#include "../core/Item.h"
#include "../core/MathHelper.h"
#include "../core/NBT.h"
#include "../core/Material.h"
#include "../block/Block.h"
#include "../world/World.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <optional>

class EntityAnimals : public EntityLiving {
public:
    explicit EntityAnimals(World* world) {
        worldObj = world;
        stepHeight = 1.0f;
    }

    int getTrackingRange() const override { return 160; }
    int getTrackingRate() const override { return 3; }
    bool shouldSendVelocity() const override { return false; }
    virtual int getMaxSpawnedInChunk() const { return 4; }

    void tick() override {
        EntityLiving::tick();
        tickWander();
        tickMovement();
    }

    virtual int getMobTypeId() const override = 0;
    virtual std::string getEntityStringId() const override = 0;

    virtual void writeToNBT(NBTCompound& nbt) const override {
        nbt.setString("id", getEntityStringId());
        nbt.setDouble("PosX", posX);
        nbt.setDouble("PosY", posY);
        nbt.setDouble("PosZ", posZ);
        nbt.setDouble("MotionX", motionX);
        nbt.setDouble("MotionY", motionY);
        nbt.setDouble("MotionZ", motionZ);
        nbt.setFloat("RotationYaw", rotationYaw);
        nbt.setFloat("RotationPitch", rotationPitch);
        nbt.setShort("Health", health);
    }

    virtual void readFromNBT(const NBTCompound& nbt) override {
        setPositionAndRotation(
            nbt.getDouble("PosX"),
            nbt.getDouble("PosY"),
            nbt.getDouble("PosZ"),
            nbt.getFloat("RotationYaw"),
            nbt.getFloat("RotationPitch"));
        motionX = nbt.getDouble("MotionX");
        motionY = nbt.getDouble("MotionY");
        motionZ = nbt.getDouble("MotionZ");
        health = nbt.getShort("Health");
        if (health <= 0) {
            health = maxHealth;
        }
    }

    virtual bool getCanSpawnHere() const;

protected:
    double wanderTargetX_ = 0.0;
    double wanderTargetY_ = 0.0;
    double wanderTargetZ_ = 0.0;
    int wanderCooldown_ = 0;
    int jumpCooldown_ = 0;

    virtual float getBlockPathWeight(int x, int y, int z) const;
    virtual float getBaseMoveSpeed() const { return 0.7f; }
    virtual int getDropItemId() const { return 0; }
    virtual int getDropCount() const { return 0; }
    virtual void tickExtra() {}

    void onDeath() override;

private:
    void pickWanderTarget();
    void tickWander();
    void tickMovement();
};

inline bool EntityAnimals::getCanSpawnHere() const {
    if (!worldObj) return false;
    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(boundingBox.minY);
    const int z = MathHelper::floor_double(posZ);
    if (worldObj->getBlockId(x, y - 1, z) != 2) { // grass
        return false;
    }
    return worldObj->getBlockLightValue(x, y, z) > 8;
}

inline float EntityAnimals::getBlockPathWeight(int x, int y, int z) const {
    if (!worldObj) return 0.0f;
    return worldObj->getBlockId(x, y - 1, z) == 2
        ? 10.0f
        : static_cast<float>(worldObj->getBlockLightValue(x, y, z)) - 0.5f;
}

inline void EntityAnimals::pickWanderTarget() {
    if (!worldObj) return;

    bool found = false;
    int bestX = 0;
    int bestY = 0;
    int bestZ = 0;
    float bestWeight = -99999.0f;

    for (int i = 0; i < 10; ++i) {
        const int x = MathHelper::floor_double(posX) + (std::rand() % 13) - 6;
        const int y = MathHelper::floor_double(posY) + (std::rand() % 7) - 3;
        const int z = MathHelper::floor_double(posZ) + (std::rand() % 13) - 6;
        const float weight = getBlockPathWeight(x, y, z);
        if (weight > bestWeight) {
            bestWeight = weight;
            bestX = x;
            bestY = y;
            bestZ = z;
            found = true;
        }
    }

    if (found) {
        wanderTargetX_ = static_cast<double>(bestX) + 0.5;
        wanderTargetY_ = static_cast<double>(bestY);
        wanderTargetZ_ = static_cast<double>(bestZ) + 0.5;
        wanderCooldown_ = 80 + (std::rand() % 40);
    }
}

inline void EntityAnimals::tickWander() {
    tickExtra();
    if (jumpCooldown_ > 0) {
        --jumpCooldown_;
    }
    if (wanderCooldown_ > 0) {
        --wanderCooldown_;
    }

    const double dx = wanderTargetX_ - posX;
    const double dz = wanderTargetZ_ - posZ;
    const double distSq = dx * dx + dz * dz;
    if (wanderCooldown_ <= 0 || distSq < 1.0 || distSq > 256.0) {
        pickWanderTarget();
    }
}

inline void EntityAnimals::tickMovement() {
    const double dx = wanderTargetX_ - posX;
    const double dz = wanderTargetZ_ - posZ;
    const double dy = wanderTargetY_ - posY;
    const double distSq = dx * dx + dz * dz;

    if (distSq > 1.0e-4) {
        const float targetYaw = static_cast<float>(
            std::atan2(dz, dx) * 180.0 / std::numbers::pi_v<double>) - 90.0f;
        float delta = targetYaw - rotationYaw;
        while (delta < -180.0f) delta += 360.0f;
        while (delta >= 180.0f) delta -= 360.0f;
        delta = std::clamp(delta, -30.0f, 30.0f);
        rotationYaw += delta;

        const float speed = getBaseMoveSpeed() * (onGround ? 0.1f : 0.02f);
        const float radians = rotationYaw * static_cast<float>(std::numbers::pi_v<double> / 180.0);
        motionX += -MathHelper::sin(radians) * speed;
        motionZ +=  MathHelper::cos(radians) * speed;
        if (dy > 0.5 && onGround && jumpCooldown_ == 0) {
            motionY = 0.42;
            jumpCooldown_ = 10;
        }
    }

    motionY -= 0.08;
    moveEntity(motionX, motionY, motionZ);

    float friction = 0.91f;
    if (onGround) {
        friction = 0.546f;
        const int below = worldObj
            ? worldObj->getBlockId(MathHelper::floor_double(posX),
                                   MathHelper::floor_double(boundingBox.minY) - 1,
                                   MathHelper::floor_double(posZ))
            : 0;
        if (below > 0 && Block::blocksList[below]) {
            friction = 0.91f;
        }
    }

    motionX *= friction;
    motionY *= 0.98;
    motionZ *= friction;
}

inline void EntityAnimals::onDeath() {
    const int dropId = getDropItemId();
    const int dropCount = getDropCount();
    if (worldObj && dropId > 0 && dropCount > 0) {
        for (int i = 0; i < dropCount; ++i) {
            auto item = std::make_unique<EntityItem>(dropId, 1, 0);
            item->setPosition(posX, posY, posZ);
            item->pickupDelay = 10;
            worldObj->spawnEntityInWorld(std::move(item));
        }
    }
    EntityLiving::onDeath();
}

class EntityPig : public EntityAnimals {
public:
    bool saddled = false;

    explicit EntityPig(World* world) : EntityAnimals(world) {
        width = 0.9f;
        height = 0.9f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 90; }
    std::string getEntityStringId() const override { return "Pig"; }
    void writeToNBT(NBTCompound& nbt) const override {
        EntityAnimals::writeToNBT(nbt);
        nbt.setByte("Saddle", saddled ? 1 : 0);
    }
    void readFromNBT(const NBTCompound& nbt) override {
        EntityAnimals::readFromNBT(nbt);
        saddled = nbt.getByte("Saddle") != 0;
    }
protected:
    int getDropItemId() const override { return Item::porkRaw ? Item::porkRaw->itemID : 0; }
    int getDropCount() const override { return 1 + (std::rand() % 3); }
};

class EntitySheep : public EntityAnimals {
public:
    bool sheared = false;

    explicit EntitySheep(World* world) : EntityAnimals(world) {
        width = 0.9f;
        height = 1.3f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 91; }
    std::string getEntityStringId() const override { return "Sheep"; }
    void writeToNBT(NBTCompound& nbt) const override {
        EntityAnimals::writeToNBT(nbt);
        nbt.setByte("Sheared", sheared ? 1 : 0);
    }
    void readFromNBT(const NBTCompound& nbt) override {
        EntityAnimals::readFromNBT(nbt);
        sheared = nbt.getByte("Sheared") != 0;
    }
protected:
    int getDropItemId() const override { return sheared ? 0 : 35; }
    int getDropCount() const override { return sheared ? 0 : (1 + (std::rand() % 3)); }
};

class EntityCow : public EntityAnimals {
public:
    explicit EntityCow(World* world) : EntityAnimals(world) {
        width = 0.9f;
        height = 1.3f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 92; }
    std::string getEntityStringId() const override { return "Cow"; }
protected:
    int getDropItemId() const override { return Item::leather ? Item::leather->itemID : 0; }
    int getDropCount() const override { return 1 + (std::rand() % 3); }
};

class EntityChicken : public EntityAnimals {
public:
    int eggLayTime = 6000 + (std::rand() % 6000);

    explicit EntityChicken(World* world) : EntityAnimals(world) {
        width = 0.3f;
        height = 0.4f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 93; }
    std::string getEntityStringId() const override { return "Chicken"; }
    void writeToNBT(NBTCompound& nbt) const override {
        EntityAnimals::writeToNBT(nbt);
        nbt.setInt("EggLayTime", eggLayTime);
    }
    void readFromNBT(const NBTCompound& nbt) override {
        EntityAnimals::readFromNBT(nbt);
        eggLayTime = nbt.getInt("EggLayTime");
        if (eggLayTime <= 0) {
            eggLayTime = 6000 + (std::rand() % 6000);
        }
    }
protected:
    int getDropItemId() const override { return Item::feather ? Item::feather->itemID : 0; }
    int getDropCount() const override { return 1 + (std::rand() % 3); }
    void tickExtra() override {
        if (motionY < 0.0 && !onGround) {
            motionY *= 0.6;
        }
        if (--eggLayTime <= 0) {
            if (worldObj && Item::egg) {
                auto item = std::make_unique<EntityItem>(Item::egg->itemID, 1, 0);
                item->setPosition(posX, posY, posZ);
                item->pickupDelay = 10;
                worldObj->spawnEntityInWorld(std::move(item));
            }
            eggLayTime = 6000 + (std::rand() % 6000);
        }
    }
};
