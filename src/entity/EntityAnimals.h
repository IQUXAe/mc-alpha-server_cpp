#pragma once

#include "EntityLiving.h"
#include "EntityItem.h"
#include "../core/Item.h"
#include "../core/MathHelper.h"
#include "../core/NBT.h"
#include "../core/Material.h"
#include "../block/Block.h"
#include "../world/World.h"
#include "../world/path/Pathfinder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <vector>

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
    bool isMovementBlocked() const;

    std::unique_ptr<PathEntity> pathToEntity_{};
    float moveStrafing_ = 0.0f;
    float moveForward_ = 0.0f;
    float randomYawVelocity_ = 0.0f;
    bool isJumping_ = false;
    int pathStuckTicks_ = 0;
    double lastNodeDistanceSq_ = -1.0;
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

inline bool EntityAnimals::isMovementBlocked() const {
    if (!worldObj) {
        return false;
    }

    const double radians = rotationYaw * static_cast<double>(std::numbers::pi_v<double> / 180.0);
    const double forwardX = -std::sin(radians);
    const double forwardZ = std::cos(radians);
    const double probeDistance = std::max(0.8, static_cast<double>(width) + 0.35);

    const int x = MathHelper::floor_double(posX + forwardX * probeDistance);
    const int y = MathHelper::floor_double(boundingBox.minY + 0.05);
    const int z = MathHelper::floor_double(posZ + forwardZ * probeDistance);

    return worldObj->isBlockSolidNoChunkLoad(x, y, z)
        && !worldObj->isBlockSolidNoChunkLoad(x, y + 1, z)
        && !worldObj->isBlockSolidNoChunkLoad(x, y + 2, z);
}

inline void EntityAnimals::updateEntityActionState() {
    tickExtra();
    moveStrafing_ = 0.0f;
    moveForward_ = 0.0f;
    isJumping_ = false;

    if (!worldObj) {
        return;
    }

    if (!pathToEntity_ || pathToEntity_->isFinished()) {
        const int currentX = MathHelper::floor_double(posX);
        const int currentY = MathHelper::floor_double(boundingBox.minY);
        const int currentZ = MathHelper::floor_double(posZ);
        struct Candidate {
            int x;
            int y;
            int z;
            float weight;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(20);

        for (int i = 0; i < 20; ++i) {
            const int x = MathHelper::floor_double(posX + static_cast<double>(std::rand() % 17) - 8.0);
            const int y = MathHelper::floor_double(posY + static_cast<double>(std::rand() % 9) - 4.0);
            const int z = MathHelper::floor_double(posZ + static_cast<double>(std::rand() % 17) - 8.0);
            const int dx = x - currentX;
            const int dy = y - currentY;
            const int dz = z - currentZ;
            if (dx * dx + dy * dy + dz * dz < 4) {
                continue;
            }
            candidates.push_back(Candidate{x, y, z, getBlockPathWeight(x, y, z)});
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.weight > rhs.weight;
        });

        Pathfinder pathfinder(*worldObj);
        pathToEntity_.reset();
        for (const auto& candidate : candidates) {
            auto path = pathfinder.createEntityPathTo(*this, candidate.x, candidate.y, candidate.z, 12.0f);
            if (path && !path->isFinished()) {
                pathToEntity_ = std::move(path);
                pathStuckTicks_ = 0;
                lastNodeDistanceSq_ = -1.0;
                break;
            }
        }
    }

    const bool touchingLiquid = isTouchingLiquid();
    rotationPitch = 0.0f;

    if (pathToEntity_ && (std::rand() % 100) != 0) {
        auto pathPosition = pathToEntity_->getPosition(*this);
        const double radius = std::max(1.25, static_cast<double>(width) * 3.0);

        while (pathPosition && pathPosition->squareDistanceTo(posX, pathPosition->yCoord, posZ) < radius * radius) {
            pathToEntity_->incrementPathIndex();
            if (pathToEntity_->isFinished()) {
                pathPosition.reset();
                pathToEntity_.reset();
            } else {
                pathPosition = pathToEntity_->getPosition(*this);
            }
            lastNodeDistanceSq_ = -1.0;
        }

        if (pathPosition) {
            const double dx = pathPosition->xCoord - posX;
            const double dz = pathPosition->zCoord - posZ;
            const double dy = pathPosition->yCoord - static_cast<double>(MathHelper::floor_double(boundingBox.minY));
            const double nodeDistanceSq = dx * dx + dz * dz;
            if (lastNodeDistanceSq_ >= 0.0 && nodeDistanceSq > lastNodeDistanceSq_ + 1.5) {
                pathToEntity_.reset();
                pathStuckTicks_ = 0;
                lastNodeDistanceSq_ = -1.0;
            } else {
                lastNodeDistanceSq_ = nodeDistanceSq;
            }

            if (!pathToEntity_) {
                moveForward_ = 0.0f;
            } else {
            const float targetYaw = static_cast<float>(
                std::atan2(dz, dx) * 180.0 / std::numbers::pi_v<double>) - 90.0f;
            rotationYaw = updateRotation(rotationYaw, targetYaw, 30.0f);
            moveForward_ = 0.35f;
            if (dy > 0.0) {
                isJumping_ = true;
            }
            if (isMovementBlocked()) {
                isJumping_ = true;
            }
            }
        } else {
            pathToEntity_.reset();
            lastNodeDistanceSq_ = -1.0;
        }
    } else if (pathToEntity_ && pathToEntity_->isFinished()) {
        pathToEntity_.reset();
        lastNodeDistanceSq_ = -1.0;
    }

    if (!pathToEntity_) {
        if ((std::rand() % 20) == 0) {
            randomYawVelocity_ = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 10.0f;
        }
        rotationYaw += randomYawVelocity_;
        moveForward_ = ((std::rand() % 12) == 0) ? 0.15f : 0.0f;
        if (isMovementBlocked()) {
            isJumping_ = true;
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
    const double previousX = posX;
    const double previousZ = posZ;

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

    if (pathToEntity_ && forward > 0.0f) {
        const double movedX = posX - previousX;
        const double movedZ = posZ - previousZ;
        if (movedX * movedX + movedZ * movedZ < 1.0e-4) {
            ++pathStuckTicks_;
            if (pathStuckTicks_ > 10) {
                pathToEntity_.reset();
                isJumping_ = true;
                pathStuckTicks_ = 0;
                lastNodeDistanceSq_ = -1.0;
                motionY = std::max(motionY, 0.42);
            }
        } else {
            pathStuckTicks_ = 0;
        }
    } else {
        pathStuckTicks_ = 0;
    }
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
