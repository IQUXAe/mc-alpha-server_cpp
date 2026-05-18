#pragma once

#include "EntityCreature.h"
#include "EntityPlayerMP.h"
#include "EntityArrow.h"
#include "EntityItem.h"
#include "../core/Item.h"
#include "../block/Block.h"
#include "../world/World.h"
#include "../world/path/Pathfinder.h"

#include <cmath>
#include <cstdlib>
#include <numbers>
#include <vector>

class EntityMob : public EntityCreature {
public:
    explicit EntityMob(World* world) : EntityCreature(world) {
        worldObj = world;
        stepHeight = 1.0f;
        health = maxHealth = 20;
        maxHurtResistantTime = 12;
    }

    virtual int getMobTypeId() const override = 0;
    virtual std::string getEntityStringId() const override = 0;

    void tick() override {
        EntityLiving::tick();
        if (!isEntityAlive()) return;

        if (attackCooldown_ > 0) --attackCooldown_;
        if (daylightBurnTicks_ <= 0) fire = 0;
        if (daylightBurnTicks_ > 0) {
            fire = std::max(fire, 20);
            if ((daylightBurnTicks_ % 20) == 0) damageEntity(1);
            --daylightBurnTicks_;
        }

        checkDaylightBurn();
        updateAI();
        moveEntityWithHeading(moveStrafing_, moveForward_);

        std::vector<Entity*> nearby;
        worldObj->getEntitiesWithinAABBExcludingEntity(this, boundingBox.expand(0.2, 0.0, 0.2), nearby);
        for (Entity* e : nearby) {
            if (e && e->canBePushed()) e->applyEntityCollision(this);
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
            nbt.getDouble("PosX"), nbt.getDouble("PosY"), nbt.getDouble("PosZ"),
            nbt.getFloat("RotationYaw"), nbt.getFloat("RotationPitch"));
        motionX = nbt.getDouble("MotionX");
        motionY = nbt.getDouble("MotionY");
        motionZ = nbt.getDouble("MotionZ");
        const int16_t savedMaxHealth = nbt.getShort("MaxHealth");
        if (savedMaxHealth > 0) maxHealth = savedMaxHealth;
        health = nbt.getShort("Health");
        if (health > maxHealth) health = maxHealth;
        if (health <= 0) health = maxHealth;
    }

    virtual bool getCanSpawnHere() const;

protected:
    virtual float getBaseMoveSpeed() const { return moveSpeed; }
    virtual float getAttackReach() const { return 2.5f; }
    virtual int getAttackStrength() const { return 2; }
    virtual bool burnsInDaylight() const { return false; }
    virtual bool shouldAggroPlayer(const EntityPlayerMP& player) const;
    // Mob-specific attack (called from attackEntityAt bridge)
    virtual void attackTarget(EntityPlayerMP& player, float distance);
    virtual void tickMobExtra() {}
    virtual int getDropItemId() const { return 0; }
    virtual int getDropCount() const { return 0; }
    void damageEntity(int amount) override { EntityLiving::damageEntity(amount); }
    void onDeath() override;

    // Bridge EntityCreature::attackEntityAt -> mob's attackTarget
    void attackEntityAt(Entity* target, float distance) override {
        if (auto* player = dynamic_cast<EntityPlayerMP*>(target)) {
            attackTarget(*player, distance);
        }
    }

    // Mob-specific AI override — manages target + uses base EntityCreature pathfinding
    void updateAI() override {
        tickMobExtra();
        moveStrafing_ = 0.0f;
        moveForward_ = 0.0f;
        isJumping_ = false;

        if (!worldObj) return;

        const bool inLiquid = isTouchingLiquid();

        // Target refresh
        if (--targetRefreshTime_ <= 0) {
            targetRefreshTime_ = 5;
            EntityPlayerMP* fresh = acquireTarget();
            if (fresh) {
                targetPlayer_ = fresh;
            } else if (hasValidTarget()) {
                const double maxDist = static_cast<double>(getTargetRange()) * 1.5;
                if (targetPlayer_->getDistanceSq(posX, posY, posZ) > maxDist * maxDist) {
                    targetPlayer_ = nullptr;
                }
            }
        }
        if (hasValidTarget() && !shouldAggroPlayer(*targetPlayer_)) {
            targetPlayer_ = nullptr;
        }

        // Set creature-level target and use base pathfinding/wander AI
        targetEntity_ = hasValidTarget() ? static_cast<Entity*>(targetPlayer_) : nullptr;
        EntityCreature::updateAI();

        // Chase speed bonus when actively pursuing
        if (hasValidTarget()) {
            const double dx = targetPlayer_->posX - posX;
            const double dz = targetPlayer_->posZ - posZ;
            const double dy = targetPlayer_->posY - posY;
            const float dist = MathHelper::sqrt_float(
                static_cast<float>(dx * dx + dy * dy + dz * dz));
            moveForward_ = getBaseMoveSpeed()
                * (dist > getAttackReach() + 1.0f ? 1.2f : 0.85f);
        }

        if (inLiquid && (std::rand() % 5) != 0) {
            isJumping_ = true;
        }
    }

    void checkDaylightBurn() {
        if (!burnsInDaylight() || !worldObj || !worldObj->isDaytime()) return;
        const int x = MathHelper::floor_double(posX);
        const int y = MathHelper::floor_double(posY);
        const int z = MathHelper::floor_double(posZ);
        const float brightness = getBrightness();
        if (brightness > 0.5f && worldObj->canBlockSeeSky(x, y, z)
            && static_cast<float>(std::rand()) / RAND_MAX * 30.0f < (brightness - 0.4f) * 2.0f) {
            daylightBurnTicks_ = 300;
            fire = std::max(fire, 300);
        }
    }

    float getBrightness() const {
        if (!worldObj) return 0.0f;
        const int x = MathHelper::floor_double(posX);
        const int y = MathHelper::floor_double(boundingBox.minY);
        const int z = MathHelper::floor_double(posZ);
        return static_cast<float>(worldObj->getBlockLightValue(x, y, z)) / 15.0f;
    }

    bool hasValidTarget() const {
        return targetPlayer_ && !targetPlayer_->isDead && targetPlayer_->health > 0;
    }

    EntityPlayerMP* acquireTarget() const {
        if (!worldObj) return nullptr;
        EntityPlayerMP* player = worldObj->getClosestPlayer(posX, posY, posZ, getTargetRange());
        if (!player || player->isDead || player->health <= 0) return nullptr;
        return shouldAggroPlayer(*player) ? player : nullptr;
    }

    int attackCooldown_ = 0;
    int daylightBurnTicks_ = 0;
    int targetRefreshTime_ = 0;
    EntityPlayerMP* targetPlayer_ = nullptr;
    std::unique_ptr<PathEntity> pathEntity_;
    bool hasAttacked_ = false;
};

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

inline bool EntityMob::getCanSpawnHere() const {
    if (!worldObj) return false;
    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(boundingBox.minY);
    const int z = MathHelper::floor_double(posZ);
    if (worldObj->getSavedLightValue(0, x, y, z) > (std::rand() % 32)) return false;
    if (worldObj->getBlockLightValue(x, y, z) > (std::rand() % 8)) return false;
    std::vector<AxisAlignedBB> collisions;
    worldObj->getCollidingBoundingBoxes(const_cast<EntityMob*>(this), boundingBox, collisions);
    return collisions.empty() && !isTouchingLiquid();
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


// ==================== EntityZombie ====================

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
    int getAttackStrength() const override { return 5; }
    int getDropItemId() const override { return Item::feather ? Item::feather->itemID : 0; }
    int getDropCount() const override { return std::rand() % 3; }
};


// ==================== EntitySkeleton ====================

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
        if (distance < 10.0f && attackCooldown_ == 0) {
            auto arrow = std::make_unique<EntityArrow>(worldObj, this);
            arrow->posY += 1.4;
            const double dx = player.posX - posX;
            const double dz = player.posZ - posZ;
            const double dy = player.posY + player.getEyeHeight() - 0.2 - arrow->posY;
            const float lift = MathHelper::sqrt_float(
                static_cast<float>(dx * dx + dz * dz)) * 0.2f;
            arrow->shoot(dx, dy + lift, dz, 0.6f, 12.0f);
            worldObj->spawnEntityInWorld(std::move(arrow));
            attackCooldown_ = 30;
        }
        moveForward_ = distance < 6.0f ? 0.0f : getBaseMoveSpeed() * 0.6f;
    }

    int getDropItemId() const override { return Item::arrow ? Item::arrow->itemID : 0; }
    int getDropCount() const override { return 1 + (std::rand() % 3); }
};


// ==================== EntitySpider ====================

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

    bool shouldAggroPlayer(const EntityPlayerMP&) const override {
        return getBrightness() < 0.5f;
    }

    // Spider wall-climbing (Java func_144_E) — sticks to any solid wall
    bool isOnLadder() const override {
        if (!worldObj) return false;
        const int x = MathHelper::floor_double(posX);
        const int y = MathHelper::floor_double(boundingBox.minY);
        const int z = MathHelper::floor_double(posZ);
        return worldObj->isBlockSolidNoChunkLoad(x - 1, y, z)
            || worldObj->isBlockSolidNoChunkLoad(x + 1, y, z)
            || worldObj->isBlockSolidNoChunkLoad(x, y, z - 1)
            || worldObj->isBlockSolidNoChunkLoad(x, y, z + 1);
    }

protected:
    void attackTarget(EntityPlayerMP& player, float distance) override {
        const float brightness = getBrightness();
        if (brightness > 0.5f && (std::rand() % 100) == 0) {
            targetPlayer_ = nullptr;
            targetEntity_ = nullptr;
            currentPath_ = nullptr;
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


// ==================== EntityCreeper ====================

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
        if ((swellDirection_ <= 0 && distance < 3.0f)
            || (swellDirection_ > 0 && distance < 7.0f)) {
            swellDirection_ = 1;
            if (++swellTime_ >= 30) {
                explode();
                return;
            }
            moveForward_ = distance < 3.5f ? 0.0f : getBaseMoveSpeed() * 0.75f;
        } else {
            if (swellTime_ > 0) --swellTime_;
            swellDirection_ = -1;
        }
    }

    int getDropItemId() const override { return Item::gunpowder ? Item::gunpowder->itemID : 0; }
    int getDropCount() const override { return std::rand() % 3; }

private:
    int swellTime_ = 0;
    int swellDirection_ = -1;

    // Alpha 1.2.6 creeper explosion: radius 3.0, damages entities + destroys blocks
    // Runs on the server tick thread — no concurrency concerns
    void explode() {
        if (!worldObj) { isDead = true; return; }

        constexpr float radius = 3.0f;

        // Phase 1: damage nearby entities
        std::vector<Entity*> nearby;
        worldObj->getEntitiesWithinAABBExcludingEntity(
            this, boundingBox.expand(radius, radius, radius), nearby);
        for (Entity* e : nearby) {
            auto* player = dynamic_cast<EntityPlayerMP*>(e);
            if (!player || player->isDead) continue;
            const double dx = player->posX - posX;
            const double dy = player->posY - posY;
            const double dz = player->posZ - posZ;
            const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));
            if (dist <= radius) {
                int damage = static_cast<int>(12.0f * (1.0f - dist / radius));
                if (damage < 1) damage = 1;
                player->attackEntityFrom(this, damage);
            }
        }

        // Phase 2: destroy blocks in a rough sphere (Alpha algorithm)
        const int cx = MathHelper::floor_double(posX);
        const int cy = MathHelper::floor_double(posY);
        const int cz = MathHelper::floor_double(posZ);
        const int r = static_cast<int>(radius);

        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dz = -r; dz <= r; ++dz) {
                    const int bx = cx + dx;
                    const int by = cy + dy;
                    const int bz = cz + dz;
                    const float dist = std::sqrt(
                        static_cast<float>(dx * dx + dy * dy + dz * dz));
                    if (dist > radius) continue;

                    const uint8_t blockId = worldObj->getBlockIdLoadedOnly(bx, by, bz);
                    if (blockId == 0) continue;
                    Block* block = Block::blocksList[blockId];
                    if (!block || block->blockHardness < 0.0f) continue;

                    // Alpha-style destruction probability
                    const float chance = 1.0f - (dist / radius);
                    if (static_cast<float>(std::rand()) / RAND_MAX <= chance) {
                        // Drop item (Alpha: blocks drop on explosion)
                        auto item = std::make_unique<EntityItem>(blockId, 1, 0);
                        item->setPosition(
                            static_cast<double>(bx) + 0.5,
                            static_cast<double>(by) + 0.5,
                            static_cast<double>(bz) + 0.5);
                        item->pickupDelay = 10;
                        worldObj->spawnEntityInWorld(std::move(item));

                        worldObj->setBlockWithNotifyNoClientUpdate(bx, by, bz, 0);
                    }
                }
            }
        }

        isDead = true;
    }
};
