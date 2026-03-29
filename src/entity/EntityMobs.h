#pragma once

#include "EntityLiving.h"
#include "EntityPlayerMP.h"
#include "EntityArrow.h"
#include "../core/Item.h"
#include "../core/MathHelper.h"
#include "../core/NBT.h"
#include "../core/Material.h"
#include "../block/Block.h"
#include "../world/World.h"

#include <cmath>
#include <cstdlib>
#include <numbers>
#include <vector>

class EntityMob : public EntityLiving {
public:
    explicit EntityMob(World* world) {
        worldObj = world;
        stepHeight = 1.0f;
        health = maxHealth = 20;
        // Slightly lower i-frames than vanilla default (20) so co-op hits
        // combine more reliably while still preventing multi-hit spam in one tick.
        maxHurtResistantTime = 12;
    }

    int getTrackingRange() const override { return 160; }
    int getTrackingRate() const override { return 2; }
    bool shouldSendVelocity() const override { return false; }
    virtual int getMaxSpawnedInChunk() const { return 4; }

    void tick() override {
        EntityLiving::tick();
        if (attackCooldown_ > 0) {
            --attackCooldown_;
        }
        if (daylightBurnTicks_ <= 0) {
            fire = 0;
        }
        if (daylightBurnTicks_ > 0) {
            fire = std::max(fire, 20);
            if ((daylightBurnTicks_ % 20) == 0) {
                damageEntity(1);
            }
            --daylightBurnTicks_;
        }

        updateMobActionState();
        moveEntityWithHeading(moveStrafing_, moveForward_);

        std::vector<Entity*> nearbyEntities;
        worldObj->getEntitiesWithinAABBExcludingEntity(this, boundingBox.expand(0.2, 0.0, 0.2), nearbyEntities);
        for (Entity* nearby : nearbyEntities) {
            if (nearby && nearby->canBePushed()) {
                nearby->applyEntityCollision(this);
            }
        }
    }

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
        nbt.setShort("MaxHealth", maxHealth);
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
        const int16_t savedMaxHealth = nbt.getShort("MaxHealth");
        if (savedMaxHealth > 0) {
            maxHealth = savedMaxHealth;
        }
        health = nbt.getShort("Health");
        if (health > maxHealth) health = maxHealth;
        if (health <= 0) health = maxHealth;
    }

    virtual bool getCanSpawnHere() const;

protected:
    virtual float getBlockPathWeight(int x, int y, int z) const;
    virtual float getBaseMoveSpeed() const { return moveSpeed; }
    virtual float getTargetRange() const { return 16.0f; }
    virtual float getAttackReach() const { return 2.5f; }
    virtual int getAttackStrength() const { return 2; }
    virtual bool burnsInDaylight() const { return false; }
    virtual bool shouldAggroPlayer(const EntityPlayerMP& player) const;
    virtual void attackTarget(EntityPlayerMP& player, float distance);
    virtual void tickMobExtra() {}
    virtual int getDropItemId() const { return 0; }
    virtual int getDropCount() const { return 0; }
    void damageEntity(int amount) override;

    void onDeath() override;
    void updateMobActionState();
    void moveEntityWithHeading(float strafe, float forward);
    bool isTouchingLiquid() const;
    void moveFlying(float strafe, float forward, float acceleration);
    float updateRotation(float current, float target, float maxTurn) const;
    void beginIdle(int durationTicks);
    bool chooseWanderDirection();
    bool findWalkableSurface(int x, int baseY, int z, int& outY) const;
    bool canStandAt(int x, int y, int z) const;
    bool canWalkToward(float yawDegrees) const;
    bool shouldJumpToward(float yawDegrees) const;
    float getBrightness() const;
    bool hasValidTarget() const;
    EntityPlayerMP* acquireTarget() const;

protected:
    float moveStrafing_ = 0.0f;
    float moveForward_ = 0.0f;
    float randomYawVelocity_ = 0.0f;
    float wanderYaw_ = 0.0f;
    bool isJumping_ = false;
    int idleTime_ = 20;
    int walkTime_ = 0;
    int reselectDirectionTime_ = 0;
    int attackCooldown_ = 0;
    int daylightBurnTicks_ = 0;
    int targetRefreshTime_ = 0;
    EntityPlayerMP* targetPlayer_ = nullptr;
};

inline float EntityMob::getBlockPathWeight(int x, int y, int z) const {
    if (!worldObj) return 0.0f;
    return 0.5f - static_cast<float>(worldObj->getBlockLightValue(x, y, z)) / 15.0f;
}

inline float EntityMob::getBrightness() const {
    if (!worldObj) {
        return 0.0f;
    }

    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(boundingBox.minY);
    const int z = MathHelper::floor_double(posZ);
    return static_cast<float>(worldObj->getBlockLightValue(x, y, z)) / 15.0f;
}

inline bool EntityMob::getCanSpawnHere() const {
    if (!worldObj) return false;

    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(boundingBox.minY);
    const int z = MathHelper::floor_double(posZ);
    if (worldObj->getSavedLightValue(0, x, y, z) > (std::rand() % 32)) {
        return false;
    }
    if (worldObj->getBlockLightValue(x, y, z) > (std::rand() % 8)) {
        return false;
    }

    std::vector<AxisAlignedBB> collisions;
    worldObj->getCollidingBoundingBoxes(const_cast<EntityMob*>(this), boundingBox, collisions);
    return collisions.empty() && !isTouchingLiquid();
}

inline bool EntityMob::isTouchingLiquid() const {
    if (!worldObj) {
        return false;
    }

    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(boundingBox.minY);
    const int z = MathHelper::floor_double(posZ);
    return worldObj->getBlockMaterialNoChunkLoad(x, y, z)->getIsLiquid()
        || worldObj->getBlockMaterialNoChunkLoad(x, y + 1, z)->getIsLiquid();
}

inline float EntityMob::updateRotation(float current, float target, float maxTurn) const {
    float delta = target - current;
    while (delta < -180.0f) delta += 360.0f;
    while (delta >= 180.0f) delta -= 360.0f;
    if (delta > maxTurn) delta = maxTurn;
    if (delta < -maxTurn) delta = -maxTurn;
    return current + delta;
}

inline bool EntityMob::hasValidTarget() const {
    return targetPlayer_ && !targetPlayer_->isDead && targetPlayer_->health > 0;
}

inline EntityPlayerMP* EntityMob::acquireTarget() const {
    if (!worldObj) {
        return nullptr;
    }

    EntityPlayerMP* player = worldObj->getClosestPlayer(posX, posY, posZ, getTargetRange());
    if (!player || player->isDead || player->health <= 0) {
        return nullptr;
    }
    return shouldAggroPlayer(*player) ? player : nullptr;
}

inline bool EntityMob::shouldAggroPlayer(const EntityPlayerMP&) const {
    return true;
}

inline void EntityMob::attackTarget(EntityPlayerMP& player, float distance) {
    if (distance < getAttackReach()
        && player.boundingBox.maxY > boundingBox.minY
        && player.boundingBox.minY < boundingBox.maxY
        && attackCooldown_ == 0) {
        attackCooldown_ = 20;
        player.attackEntityFrom(this, getAttackStrength());
    }
}

inline void EntityMob::damageEntity(int amount) {
    EntityLiving::damageEntity(amount);
}

inline void EntityMob::updateMobActionState() {
    tickMobExtra();
    moveStrafing_ = 0.0f;
    moveForward_ = 0.0f;
    isJumping_ = false;

    if (!worldObj) {
        return;
    }

    if (burnsInDaylight() && worldObj->isDaytime()) {
        const int x = MathHelper::floor_double(posX);
        const int y = MathHelper::floor_double(posY);
        const int z = MathHelper::floor_double(posZ);
        const float brightness = getBrightness();
        if (brightness > 0.5f && worldObj->canBlockSeeSky(x, y, z)
            && static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 30.0f < (brightness - 0.4f) * 2.0f) {
            daylightBurnTicks_ = 300;
            fire = std::max(fire, 300);
        }
    }

    const bool touchingLiquid = isTouchingLiquid();
    rotationPitch = 0.0f;

    if (--targetRefreshTime_ <= 0) {
        targetRefreshTime_ = 5;
        EntityPlayerMP* refreshedTarget = acquireTarget();
        if (refreshedTarget) {
            targetPlayer_ = refreshedTarget;
        } else if (hasValidTarget()) {
            const double maxDistance = static_cast<double>(getTargetRange()) * 1.5;
            if (targetPlayer_->getDistanceSq(posX, posY, posZ) > maxDistance * maxDistance) {
                targetPlayer_ = nullptr;
            }
        }
    }

    if (hasValidTarget() && !shouldAggroPlayer(*targetPlayer_)) {
        targetPlayer_ = nullptr;
    }

    if (hasValidTarget()) {
        const double dx = targetPlayer_->posX - posX;
        const double dy = targetPlayer_->posY - posY;
        const double dz = targetPlayer_->posZ - posZ;
        const float distance = MathHelper::sqrt_float(static_cast<float>(dx * dx + dy * dy + dz * dz));
        const float targetYaw = static_cast<float>(
            std::atan2(dz, dx) * 180.0 / std::numbers::pi_v<double>) - 90.0f;

        rotationYaw = updateRotation(rotationYaw, targetYaw, 20.0f);
        moveForward_ = getBaseMoveSpeed() * (distance > getAttackReach() + 1.0f ? 1.2f : 0.85f);
        attackTarget(*targetPlayer_, distance);
        if (shouldJumpToward(targetYaw) || collidedHorizontally) {
            isJumping_ = true;
        }

        idleTime_ = 0;
        walkTime_ = 0;
        reselectDirectionTime_ = 0;
    } else if (idleTime_ > 0) {
        --idleTime_;
        if ((std::rand() % 20) == 0) {
            randomYawVelocity_ = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 10.0f;
        }
        rotationYaw += randomYawVelocity_;

        if (idleTime_ == 0) {
            chooseWanderDirection();
            walkTime_ = 20 + (std::rand() % 60);
            reselectDirectionTime_ = 8 + (std::rand() % 16);
        }
    } else {
        if (walkTime_ <= 0) {
            beginIdle(20 + (std::rand() % 40));
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
        }
    }

    if (touchingLiquid && (std::rand() % 5) != 0) {
        isJumping_ = true;
    }
}

inline void EntityMob::moveFlying(float strafe, float forward, float acceleration) {
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

inline void EntityMob::moveEntityWithHeading(float strafe, float forward) {
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
}

inline void EntityMob::beginIdle(int durationTicks) {
    idleTime_ = durationTicks;
    walkTime_ = 0;
    reselectDirectionTime_ = 0;
    moveForward_ = 0.0f;
}

inline bool EntityMob::canStandAt(int x, int y, int z) const {
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

inline bool EntityMob::findWalkableSurface(int x, int baseY, int z, int& outY) const {
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

inline bool EntityMob::chooseWanderDirection() {
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
        candidateWeight += static_cast<float>(distance) * 0.1f;
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

inline bool EntityMob::canWalkToward(float yawDegrees) const {
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

inline bool EntityMob::shouldJumpToward(float yawDegrees) const {
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

inline void EntityMob::onDeath() {
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

class EntityZombie : public EntityMob {
public:
    explicit EntityZombie(World* world) : EntityMob(world) {
        width = 0.6f;
        height = 1.8f;
        yOffset = 0.0f;
        moveSpeed = 0.5f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 54; }
    std::string getEntityStringId() const override { return "Zombie"; }

protected:
    bool burnsInDaylight() const override { return true; }
    int getAttackStrength() const override { return 2; }
    int getDropItemId() const override { return Item::feather ? Item::feather->itemID : 0; }
    int getDropCount() const override { return std::rand() % 3; }
};

class EntitySkeleton : public EntityMob {
public:
    explicit EntitySkeleton(World* world) : EntityMob(world) {
        width = 0.6f;
        height = 1.8f;
        yOffset = 0.0f;
        moveSpeed = 0.65f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 51; }
    std::string getEntityStringId() const override { return "Skeleton"; }

protected:
    bool burnsInDaylight() const override { return true; }
    float getAttackReach() const override { return 10.0f; }
    void attackTarget(EntityPlayerMP& player, float distance) override {
        if (distance < 10.0f) {
            if (attackCooldown_ == 0) {
                auto arrow = std::make_unique<EntityArrow>(worldObj, this);
                arrow->posY += 1.4;
                const double dx = player.posX - posX;
                const double dz = player.posZ - posZ;
                const double dy = player.posY + player.getEyeHeight() - 0.2 - arrow->posY;
                const float lift = MathHelper::sqrt_float(static_cast<float>(dx * dx + dz * dz)) * 0.2f;
                arrow->shoot(dx, dy + lift, dz, 0.6f, 12.0f);
                worldObj->spawnEntityInWorld(std::move(arrow));
                attackCooldown_ = 30;
            }
            moveForward_ = distance < 6.0f ? 0.0f : getBaseMoveSpeed() * 0.6f;
        }
    }
    int getDropItemId() const override { return Item::arrow ? Item::arrow->itemID : 0; }
    int getDropCount() const override { return 1 + (std::rand() % 3); }
};

class EntitySpider : public EntityMob {
public:
    explicit EntitySpider(World* world) : EntityMob(world) {
        width = 1.4f;
        height = 0.9f;
        yOffset = 0.0f;
        moveSpeed = 0.8f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 52; }
    std::string getEntityStringId() const override { return "Spider"; }

protected:
    bool shouldAggroPlayer(const EntityPlayerMP&) const override {
        return getBrightness() < 0.5f;
    }

    void attackTarget(EntityPlayerMP& player, float distance) override {
        const float brightness = getBrightness();
        if (brightness > 0.5f && (std::rand() % 100) == 0) {
            targetPlayer_ = nullptr;
            beginIdle(20 + (std::rand() % 20));
            return;
        }

        if (distance > 2.0f && distance < 6.0f && (std::rand() % 10) == 0 && onGround) {
            const double dx = player.posX - posX;
            const double dz = player.posZ - posZ;
            const double len = std::max(0.001, std::sqrt(dx * dx + dz * dz));
            motionX = dx / len * 0.4 + motionX * 0.2;
            motionZ = dz / len * 0.4 + motionZ * 0.2;
            motionY = 0.4;
            return;
        }

        if (distance < getAttackReach() && attackCooldown_ == 0) {
            attackCooldown_ = 20;
            player.attackEntityFrom(this, 1);
        }
    }

    int getDropItemId() const override { return Item::silk ? Item::silk->itemID : 0; }
    int getDropCount() const override { return std::rand() % 3; }
};

class EntityCreeper : public EntityMob {
public:
    explicit EntityCreeper(World* world) : EntityMob(world) {
        width = 0.6f;
        height = 1.7f;
        yOffset = 0.0f;
        moveSpeed = 0.7f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 50; }
    std::string getEntityStringId() const override { return "Creeper"; }

protected:
    void attackTarget(EntityPlayerMP& player, float distance) override {
        if ((swellDirection_ <= 0 && distance < 3.0f) || (swellDirection_ > 0 && distance < 7.0f)) {
            swellDirection_ = 1;
            if (++swellTime_ >= 30) {
                const double radius = 3.0;
                if (worldObj) {
                    std::vector<Entity*> nearbyEntities;
                    worldObj->getEntitiesWithinAABBExcludingEntity(
                        this,
                        boundingBox.expand(radius, radius, radius),
                        nearbyEntities);

                    for (Entity* nearby : nearbyEntities) {
                        auto* other = dynamic_cast<EntityPlayerMP*>(nearby);
                        if (!other || other->isDead) continue;

                        const double dx = other->posX - posX;
                        const double dy = other->posY - posY;
                        const double dz = other->posZ - posZ;
                        const double distSq = dx * dx + dy * dy + dz * dz;
                        if (distSq <= radius * radius) {
                            other->attackEntityFrom(this, 12);
                        }
                    }
                }
                isDead = true;
                return;
            }
            moveForward_ = distance < 3.5f ? 0.0f : getBaseMoveSpeed() * 0.75f;
        } else {
            if (swellTime_ > 0) {
                --swellTime_;
            }
            swellDirection_ = -1;
        }
    }

    int getDropItemId() const override { return Item::gunpowder ? Item::gunpowder->itemID : 0; }
    int getDropCount() const override { return std::rand() % 3; }

private:
    int swellTime_ = 0;
    int swellDirection_ = -1;
};
