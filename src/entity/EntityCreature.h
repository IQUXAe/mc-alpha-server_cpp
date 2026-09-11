#pragma once

#include "EntityLiving.h"
#include "../world/path/Pathfinder.h"

#include <memory>
#include <optional>

class EntityCreature : public EntityLiving {
public:
    explicit EntityCreature(World* world) {
        worldObj = world;
        stepHeight = 1.0f;
    }

    int getTrackingRange() const override { return 160; }
    int getTrackingRate() const override { return 2; }
    bool shouldSendVelocity() const override { return false; }
    virtual int getMaxSpawnedInChunk() const { return 4; }

    // Called to find a target entity (func_158_i)
    virtual Entity* findTarget() { return nullptr; }

    // Called when creature should attack its target (func_157_a)
    virtual void attackEntityAt(Entity* target, float distance) {}

    // Block path weight used for wander destination selection (func_159_a)
    virtual float getBlockPathWeight(int x, int y, int z) const { return 0.0f; }

    virtual float getTargetRange() const { return 16.0f; }

    void tick() override {
        EntityLiving::tick();
        if (!isEntityAlive()) return;
        updateAI();
        moveEntityWithHeading(moveStrafing_, moveForward_);
    }

protected:
    Entity* targetEntity_ = nullptr;
    std::unique_ptr<PathEntity> currentPath_;
    bool isAttacking_ = false;
    float moveStrafing_ = 0.0f;
    float moveForward_ = 0.0f;

    // Java's func_145_g — line-of-sight check
    bool canSeeEntity(const Entity& target) const {
        if (!worldObj) return false;
        Vec3D from(posX, posY + static_cast<double>(getEyeHeight()), posZ);
        Vec3D to(target.posX, target.posY + static_cast<double>(target.getEyeHeight()), target.posZ);
        return worldObj->canEntitySee(const_cast<EntityCreature*>(this), from, to);
    }

    // Main AI update — mirrors Java EntityCreature.func_152_d_()
    virtual void updateAI() {
        isAttacking_ = false;

        // Phase 1: target management
        if (targetEntity_ == nullptr) {
            targetEntity_ = findTarget();
            if (targetEntity_ != nullptr) {
                currentPath_ = worldObj->getPathToEntity(this, targetEntity_, getTargetRange());
            }
        } else if (!targetEntity_->isEntityAlive()) {
            targetEntity_ = nullptr;
        } else {
            const float dist = MathHelper::sqrt_float(
                static_cast<float>(targetEntity_->getDistanceSqToEntity(*this)));
            if (canSeeEntity(*targetEntity_)) {
                attackEntityAt(targetEntity_, dist);
            }
        }

        // Phase 2: path management — wander or re-path to target
        if (isAttacking_ || targetEntity_ == nullptr
            || (currentPath_ != nullptr && (RustBridge::rngNextInt(20)) != 0)) {
            if ((currentPath_ == nullptr && (RustBridge::rngNextInt(80)) == 0)
                || (RustBridge::rngNextInt(80)) == 0) {
                pickWanderDestination();
            }
        } else if (targetEntity_ != nullptr) {
            currentPath_ = worldObj->getPathToEntity(this, targetEntity_, getTargetRange());
        }

        // Phase 3: path following
        followPath();
    }

    // Wander destination — pick best of 10 random points in 13×7×13 area
    void pickWanderDestination() {
        bool found = false;
        int bestX = 0, bestY = 0, bestZ = 0;
        float bestWeight = -99999.0f;
        const int baseY = MathHelper::floor_double(boundingBox.minY);

        for (int i = 0; i < 10; ++i) {
            const int cx = MathHelper::floor_double(posX) + RustBridge::rngNextInt(13) - 6;
            const int cy = baseY + RustBridge::rngNextInt(7) - 3;
            const int cz = MathHelper::floor_double(posZ) + RustBridge::rngNextInt(13) - 6;
            const float w = getBlockPathWeight(cx, cy, cz);
            if (w > bestWeight) {
                bestWeight = w;
                bestX = cx;
                bestY = cy;
                bestZ = cz;
                found = true;
            }
        }

        if (found) {
            currentPath_ = worldObj->getPathToBlock(this, bestX, bestY, bestZ, 10.0f);
        }
    }

    // Follow the current path — mirrors Java logic in func_152_d_()
    void followPath() {
        const int floorY = MathHelper::floor_double(boundingBox.minY);
        const bool inLiquid = isTouchingLiquid();

        moveForward_ = 0.0f;
        moveStrafing_ = 0.0f;
        isJumping_ = false;

        if (currentPath_ != nullptr && RustBridge::rngNextInt(100) != 0) {
            std::optional<Vec3D> nextPos = currentPath_->getPosition(*this);
            const double widthSq = static_cast<double>(width * 2.0f);
            const double threshold = widthSq * widthSq;

            // Advance path while current point is inside entity bounding box
            while (nextPos.has_value()
                && nextPos->squareDistanceTo(posX, nextPos->yCoord, posZ) < threshold) {
                currentPath_->incrementPathIndex();
                if (currentPath_->isFinished()) {
                    nextPos = std::nullopt;
                    currentPath_ = nullptr;
                } else {
                    nextPos = currentPath_->getPosition(*this);
                }
            }

            if (nextPos.has_value()) {
                const double dx = nextPos->xCoord - posX;
                const double dz = nextPos->zCoord - posZ;
                const double dy = nextPos->yCoord - static_cast<double>(floorY);

                RustBridge::SteerOut steer{};
                const double tdx = (isAttacking_ && targetEntity_) ? targetEntity_->posX - posX : 0.0;
                const double tdz = (isAttacking_ && targetEntity_) ? targetEntity_->posZ - posZ : 0.0;
                if (RustBridge::aiSteerToPoint(dx, dz, dy, rotationYaw, isAttacking_,
                                               targetEntity_ != nullptr, tdx, tdz,
                                               moveForward_, &steer)) {
                    rotationYaw = steer.new_yaw;
                    moveStrafing_ = steer.strafe;
                    moveForward_ = steer.forward;
                    if (steer.jump) {
                        isJumping_ = true;
                    }
                }
            }

            // Face target while chasing
            if (targetEntity_ != nullptr) {
                faceEntity(*targetEntity_, 30.0f);
            }

        }

        // Jump over obstacles (only horizontal collision, not ground contact)
        if (collidedHorizontally) {
            isJumping_ = true;
        }

        // Jump in liquid (80% chance each tick to stay afloat)
        if (RustBridge::rngNextFloat() < 0.8f && inLiquid) {
            isJumping_ = true;
        }

        // Set forward movement using per-mob moveSpeed
        if (currentPath_ != nullptr) {
            moveForward_ = moveSpeed;
        }
    }

    // Face another entity — Java EntityLiving.func_147_b()
    void faceEntity(const Entity& target, float maxTurn) {
        const double dx = target.posX - posX;
        const double dz = target.posZ - posZ;
        double dy;
        if (auto* living = dynamic_cast<const EntityLiving*>(&target)) {
            dy = living->posY + static_cast<double>(living->getEyeHeight())
                - (posY + static_cast<double>(getEyeHeight()));
        } else {
            dy = (target.boundingBox.minY + target.boundingBox.maxY) / 2.0
                - (posY + static_cast<double>(getEyeHeight()));
        }

        float outYaw = rotationYaw;
        float outPitch = rotationPitch;
        if (RustBridge::aiFaceAngles(dx, dz, dy, rotationYaw, rotationPitch, maxTurn, &outYaw, &outPitch)) {
            rotationYaw = outYaw;
            rotationPitch = outPitch;
        }
    }
};
