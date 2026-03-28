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
#include <limits>
#include <numbers>
#include <vector>

class EntityAnimals : public EntityLiving {
public:
    explicit EntityAnimals(World* world) {
        worldObj = world;
        stepHeight = 1.0f;
    }

    int getTrackingRange() const override { return 160; }
    int getTrackingRate() const override { return 2; }
    bool shouldSendVelocity() const override { return false; }
    virtual int getMaxSpawnedInChunk() const { return 4; }

    void tick() override {
        EntityLiving::tick();
        updateEntityActionState();
        moveEntityWithHeading(moveStrafing_, moveForward_);

        std::vector<Entity*> nearbyEntities;
        worldObj->getEntitiesWithinAABBExcludingEntity(this, boundingBox.expand(0.2, 0.0, 0.2), nearbyEntities);
        for (Entity* nearby : nearbyEntities) {
            if (nearby && nearby->canBePushed()) {
                nearby->applyEntityCollision(this);
            }
        }
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
    virtual float getBlockPathWeight(int x, int y, int z) const;
    virtual float getBaseMoveSpeed() const { return 0.7f; }
    virtual int getDropItemId() const { return 0; }
    virtual int getDropCount() const { return 0; }
    virtual void tickExtra() {}

    void onDeath() override;
    void updateEntityActionState();
    void moveEntityWithHeading(float strafe, float forward);

private:
    bool isTouchingLiquid() const;
    void moveFlying(float strafe, float forward, float acceleration);
    float updateRotation(float current, float target, float maxTurn) const;
    void beginIdle(int durationTicks);
    bool chooseWanderDirection();
    bool findWalkableSurface(int x, int baseY, int z, int& outY) const;
    bool canStandAt(int x, int y, int z) const;
    bool canWalkToward(float yawDegrees) const;
    bool shouldJumpToward(float yawDegrees) const;

    float moveStrafing_ = 0.0f;
    float moveForward_ = 0.0f;
    float randomYawVelocity_ = 0.0f;
    float wanderYaw_ = 0.0f;
    bool isJumping_ = false;
    int idleTime_ = 35;
    int walkTime_ = 0;
    int reselectDirectionTime_ = 0;
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

inline bool EntityAnimals::isTouchingLiquid() const {
    if (!worldObj) {
        return false;
    }

    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(boundingBox.minY);
    const int z = MathHelper::floor_double(posZ);

    return worldObj->getBlockMaterialNoChunkLoad(x, y, z)->getIsLiquid()
        || worldObj->getBlockMaterialNoChunkLoad(x, y + 1, z)->getIsLiquid();
}

inline float EntityAnimals::updateRotation(float current, float target, float maxTurn) const {
    float delta = target - current;
    while (delta < -180.0f) delta += 360.0f;
    while (delta >= 180.0f) delta -= 360.0f;
    if (delta > maxTurn) delta = maxTurn;
    if (delta < -maxTurn) delta = -maxTurn;
    return current + delta;
}

inline void EntityAnimals::updateEntityActionState() {
    tickExtra();
    moveStrafing_ = 0.0f;
    moveForward_ = 0.0f;
    isJumping_ = false;

    if (!worldObj) {
        return;
    }

    const bool touchingLiquid = isTouchingLiquid();
    rotationPitch = 0.0f;

    if (idleTime_ > 0) {
        --idleTime_;
        if ((std::rand() % 20) == 0) {
            randomYawVelocity_ = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 10.0f;
        }
        rotationYaw += randomYawVelocity_;

        if (idleTime_ == 0) {
            chooseWanderDirection();
            walkTime_ = 40 + (std::rand() % 80);
            reselectDirectionTime_ = 8 + (std::rand() % 16);
        }
    } else {
        if (walkTime_ <= 0) {
            beginIdle(35 + (std::rand() % 55));
        } else {
            --walkTime_;
            if (--reselectDirectionTime_ <= 0 || !canWalkToward(wanderYaw_) || collidedHorizontally) {
                chooseWanderDirection();
                reselectDirectionTime_ = 8 + (std::rand() % 16);
            }

            rotationYaw = updateRotation(rotationYaw, wanderYaw_, 20.0f);
            moveForward_ = getBaseMoveSpeed();

            if (shouldJumpToward(wanderYaw_) || collidedHorizontally) {
                isJumping_ = true;
            }

            if ((std::rand() % 120) == 0) {
                beginIdle(20 + (std::rand() % 35));
            }
        }
    }

    if (touchingLiquid && (std::rand() % 5) != 0) {
        isJumping_ = true;
    }
}

inline void EntityAnimals::moveFlying(float strafe, float forward, float acceleration) {
    float magnitude = strafe * strafe + forward * forward;
    if (magnitude < 1.0e-4f) {
        return;
    }

    magnitude = MathHelper::sqrt_float(magnitude);
    if (magnitude < 1.0f) {
        magnitude = 1.0f;
    }

    magnitude = acceleration / magnitude;
    strafe *= magnitude;
    forward *= magnitude;

    const float radians = rotationYaw * static_cast<float>(std::numbers::pi_v<double> / 180.0);
    const float sinYaw = MathHelper::sin(radians);
    const float cosYaw = MathHelper::cos(radians);
    motionX += strafe * cosYaw - forward * sinYaw;
    motionZ += forward * cosYaw + strafe * sinYaw;
}

inline void EntityAnimals::moveEntityWithHeading(float strafe, float forward) {
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

    moveEntity(motionX, motionY, motionZ);
    motionY -= 0.08;
    motionY *= 0.98;
    motionX *= friction;
    motionZ *= friction;
    if (walkTime_ <= 0 && idleTime_ <= 0) {
        beginIdle(35 + (std::rand() % 55));
    }
}

inline void EntityAnimals::beginIdle(int durationTicks) {
    idleTime_ = durationTicks;
    walkTime_ = 0;
    reselectDirectionTime_ = 0;
    moveForward_ = 0.0f;
}

inline bool EntityAnimals::canStandAt(int x, int y, int z) const {
    if (!worldObj || y <= 0 || y >= CHUNK_SIZE_Y - 1) {
        return false;
    }

    Material* below = worldObj->getBlockMaterialNoChunkLoad(x, y - 1, z);
    Material* feet = worldObj->getBlockMaterialNoChunkLoad(x, y, z);
    Material* head = worldObj->getBlockMaterialNoChunkLoad(x, y + 1, z);
    if (!below || !feet || !head) {
        return false;
    }

    if (!worldObj->isBlockSolidNoChunkLoad(x, y - 1, z) || below->getIsLiquid()) {
        return false;
    }
    if (feet->blocksMovement() || feet->getIsLiquid()) {
        return false;
    }
    if (head->blocksMovement() || head->getIsLiquid()) {
        return false;
    }

    return true;
}

inline bool EntityAnimals::findWalkableSurface(int x, int baseY, int z, int& outY) const {
    const int columnTop = worldObj ? worldObj->getHeightValue(x, z) : baseY;
    const int startY = std::min(CHUNK_SIZE_Y - 2, std::max(baseY + 2, columnTop + 1));
    const int minY = std::max(1, std::min(baseY - 6, columnTop - 4));
    for (int y = startY; y >= minY; --y) {
        if (canStandAt(x, y, z)) {
            outY = y;
            return true;
        }
    }
    return false;
}

inline bool EntityAnimals::chooseWanderDirection() {
    if (!worldObj) {
        beginIdle(20);
        return false;
    }

    float bestWeight = -99999.0f;
    float bestYaw = rotationYaw;
    bool foundCandidate = false;
    const int currentY = MathHelper::floor_double(boundingBox.minY);

    for (int i = 0; i < 8; ++i) {
        const float candidateYaw = rotationYaw + static_cast<float>((std::rand() % 241) - 120);
        const double radians = candidateYaw * static_cast<double>(std::numbers::pi_v<float> / 180.0f);
        const double distance = 3.0 + static_cast<double>(std::rand() % 5);
        const int candidateX = MathHelper::floor_double(posX - std::sin(radians) * distance);
        const int candidateZ = MathHelper::floor_double(posZ + std::cos(radians) * distance);
        int candidateY = currentY;
        if (!findWalkableSurface(candidateX, currentY, candidateZ, candidateY)) {
            continue;
        }

        float candidateWeight = getBlockPathWeight(candidateX, candidateY, candidateZ);
        candidateWeight += static_cast<float>(distance) * 0.15f;
        if (candidateWeight > bestWeight) {
            bestWeight = candidateWeight;
            bestYaw = candidateYaw;
            foundCandidate = true;
        }
    }

    if (!foundCandidate) {
        wanderYaw_ = rotationYaw + static_cast<float>((std::rand() % 181) - 90);
        return false;
    }

    wanderYaw_ = bestYaw;
    return true;
}

inline bool EntityAnimals::canWalkToward(float yawDegrees) const {
    if (!worldObj) {
        return false;
    }

    const double radians = yawDegrees * static_cast<double>(std::numbers::pi_v<float> / 180.0f);
    const double probeDistance = std::max(0.9, static_cast<double>(width) + 0.6);
    const int probeX = MathHelper::floor_double(posX - std::sin(radians) * probeDistance);
    const int probeY = MathHelper::floor_double(boundingBox.minY);
    const int probeZ = MathHelper::floor_double(posZ + std::cos(radians) * probeDistance);

    return canStandAt(probeX, probeY, probeZ) || canStandAt(probeX, probeY + 1, probeZ);
}

inline bool EntityAnimals::shouldJumpToward(float yawDegrees) const {
    if (!worldObj) {
        return false;
    }

    const double radians = yawDegrees * static_cast<double>(std::numbers::pi_v<float> / 180.0f);
    const double probeDistance = std::max(0.8, static_cast<double>(width) + 0.4);
    const int probeX = MathHelper::floor_double(posX - std::sin(radians) * probeDistance);
    const int probeY = MathHelper::floor_double(boundingBox.minY);
    const int probeZ = MathHelper::floor_double(posZ + std::cos(radians) * probeDistance);

    return worldObj->isBlockSolidNoChunkLoad(probeX, probeY, probeZ)
        && canStandAt(probeX, probeY + 1, probeZ);
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
