#pragma once

#include "EntityLiving.h"
#include "../block/Block.h"
#include "../core/Item.h"
#include "../core/MathHelper.h"
#include "../world/World.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

class EntityArrow : public Entity {
public:
    explicit EntityArrow(World* world) {
        worldObj = world;
        width = 0.5f;
        height = 0.5f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    EntityArrow(World* world, EntityLiving* shooterEntity) : EntityArrow(world) {
        shooter = shooterEntity;
        if (!shooterEntity) {
            return;
        }

        setPositionAndRotation(
            shooterEntity->posX,
            shooterEntity->posY + shooterEntity->getEyeHeight(),
            shooterEntity->posZ,
            shooterEntity->rotationYaw,
            shooterEntity->rotationPitch);

        posX -= MathHelper::cos(rotationYaw / 180.0f * static_cast<float>(std::numbers::pi_v<double>)) * 0.16f;
        posY -= 0.1;
        posZ -= MathHelper::sin(rotationYaw / 180.0f * static_cast<float>(std::numbers::pi_v<double>)) * 0.16f;
        setPosition(posX, posY, posZ);

        motionX = -MathHelper::sin(rotationYaw / 180.0f * static_cast<float>(std::numbers::pi_v<double>))
            * MathHelper::cos(rotationPitch / 180.0f * static_cast<float>(std::numbers::pi_v<double>));
        motionZ = MathHelper::cos(rotationYaw / 180.0f * static_cast<float>(std::numbers::pi_v<double>))
            * MathHelper::cos(rotationPitch / 180.0f * static_cast<float>(std::numbers::pi_v<double>));
        motionY = -MathHelper::sin(rotationPitch / 180.0f * static_cast<float>(std::numbers::pi_v<double>));
        shoot(motionX, motionY, motionZ, 1.5f, 1.0f);
    }

    int getTrackingRange() const { return 64; }
    int getTrackingRate() const { return 5; }
    bool shouldSendVelocity() const { return true; }
    bool canBePushed() const override { return false; }
    bool canBeCollidedWith() const override { return false; }

    void shoot(double vx, double vy, double vz, float velocity, float inaccuracy) {
        const double length = MathHelper::sqrt_double(vx * vx + vy * vy + vz * vz);
        if (length < 1.0E-7) {
            return;
        }

        vx /= length;
        vy /= length;
        vz /= length;

        auto randomNoise = []() -> double {
            return (static_cast<double>(std::rand()) / RAND_MAX
                - static_cast<double>(std::rand()) / RAND_MAX) * 0.0075;
        };

        vx += randomNoise() * inaccuracy;
        vy += randomNoise() * inaccuracy;
        vz += randomNoise() * inaccuracy;
        vx *= velocity;
        vy *= velocity;
        vz *= velocity;

        motionX = vx;
        motionY = vy;
        motionZ = vz;

        const float horizontal = MathHelper::sqrt_float(static_cast<float>(vx * vx + vz * vz));
        prevRotationYaw = rotationYaw = static_cast<float>(std::atan2(vx, vz) * 180.0 / std::numbers::pi_v<double>);
        prevRotationPitch = rotationPitch = static_cast<float>(std::atan2(vy, horizontal) * 180.0 / std::numbers::pi_v<double>);
        ticksInGround = 0;
    }

    void tick() override {
        Entity::tick();

        if (prevRotationPitch == 0.0f && prevRotationYaw == 0.0f) {
            const float horizontal = MathHelper::sqrt_float(static_cast<float>(motionX * motionX + motionZ * motionZ));
            prevRotationYaw = rotationYaw = static_cast<float>(std::atan2(motionX, motionZ) * 180.0 / std::numbers::pi_v<double>);
            prevRotationPitch = rotationPitch = static_cast<float>(std::atan2(motionY, horizontal) * 180.0 / std::numbers::pi_v<double>);
        }

        if (arrowShake > 0) {
            --arrowShake;
        }

        if (inGround) {
            if (worldObj && worldObj->getBlockIdNoChunkLoad(tileX, tileY, tileZ) == inTile) {
                if (++ticksInGround >= 1200) {
                    isDead = true;
                }
                return;
            }

            inGround = false;
            motionX *= (static_cast<double>(std::rand()) / RAND_MAX) * 0.2;
            motionY *= (static_cast<double>(std::rand()) / RAND_MAX) * 0.2;
            motionZ *= (static_cast<double>(std::rand()) / RAND_MAX) * 0.2;
            ticksInGround = 0;
            ticksInAir = 0;
        } else {
            ++ticksInAir;
        }

        const Vec3D start(posX, posY, posZ);
        Vec3D end(posX + motionX, posY + motionY, posZ + motionZ);

        struct BlockHit {
            int x = 0;
            int y = 0;
            int z = 0;
            int blockId = 0;
            int8_t side = 0;
            Vec3D hitVec;
            double distSq = std::numeric_limits<double>::max();
        };

        std::optional<BlockHit> blockHit;
        if (worldObj) {
            const AxisAlignedBB sweep = boundingBox.addCoord(motionX, motionY, motionZ).expand(1.0, 1.0, 1.0);
            const int minX = MathHelper::floor_double(sweep.minX);
            const int minY = MathHelper::floor_double(sweep.minY);
            const int minZ = MathHelper::floor_double(sweep.minZ);
            const int maxX = MathHelper::floor_double(sweep.maxX);
            const int maxY = MathHelper::floor_double(sweep.maxY);
            const int maxZ = MathHelper::floor_double(sweep.maxZ);

            for (int x = minX; x <= maxX; ++x) {
                for (int y = minY; y <= maxY; ++y) {
                    for (int z = minZ; z <= maxZ; ++z) {
                        const int blockId = worldObj->getBlockIdNoChunkLoad(x, y, z);
                        if (blockId <= 0 || !Block::blocksList[blockId]) {
                            continue;
                        }

                        auto bb = Block::blocksList[blockId]->getCollisionBoundingBoxFromPool(worldObj, x, y, z);
                        if (!bb) {
                            continue;
                        }

                        auto clipped = bb->clip(start, end);
                        if (!clipped) {
                            continue;
                        }

                        const double distSq = start.squareDistanceTo(clipped->hitVec);
                        if (!blockHit || distSq < blockHit->distSq) {
                            blockHit = BlockHit{x, y, z, blockId, clipped->sideHit, clipped->hitVec, distSq};
                        }
                    }
                }
            }
        }

        Entity* entityHit = nullptr;
        double closestEntityDistSq = blockHit ? blockHit->distSq : std::numeric_limits<double>::max();
        if (worldObj) {
            std::vector<Entity*> candidates;
            worldObj->getEntitiesWithinAABBExcludingEntity(this, boundingBox.addCoord(motionX, motionY, motionZ).expand(1.0, 1.0, 1.0), candidates);

            for (Entity* candidate : candidates) {
                if (!candidate || !candidate->canBeCollidedWith() || (candidate == shooter && ticksInAir < 5)) {
                    continue;
                }

                AxisAlignedBB expanded = candidate->boundingBox.expand(0.3, 0.3, 0.3);
                auto clipped = expanded.clip(start, end);
                if (!clipped) {
                    continue;
                }

                const double distSq = start.squareDistanceTo(clipped->hitVec);
                if (distSq < closestEntityDistSq) {
                    closestEntityDistSq = distSq;
                    entityHit = candidate;
                }
            }
        }

        if (entityHit) {
            entityHit->attackEntityFrom(shooter, 4);
            isDead = true;
            return;
        }

        if (blockHit) {
            tileX = blockHit->x;
            tileY = blockHit->y;
            tileZ = blockHit->z;
            inTile = blockHit->blockId;
            posX = blockHit->hitVec.xCoord;
            posY = blockHit->hitVec.yCoord;
            posZ = blockHit->hitVec.zCoord;
            setPosition(posX, posY, posZ);
            inGround = true;
            arrowShake = 7;
            return;
        }

        posX += motionX;
        posY += motionY;
        posZ += motionZ;

        const float horizontal = MathHelper::sqrt_float(static_cast<float>(motionX * motionX + motionZ * motionZ));
        rotationYaw = static_cast<float>(std::atan2(motionX, motionZ) * 180.0 / std::numbers::pi_v<double>);
        rotationPitch = static_cast<float>(std::atan2(motionY, horizontal) * 180.0 / std::numbers::pi_v<double>);

        while (rotationPitch - prevRotationPitch < -180.0f) prevRotationPitch -= 360.0f;
        while (rotationPitch - prevRotationPitch >= 180.0f) prevRotationPitch += 360.0f;
        while (rotationYaw - prevRotationYaw < -180.0f) prevRotationYaw -= 360.0f;
        while (rotationYaw - prevRotationYaw >= 180.0f) prevRotationYaw += 360.0f;

        rotationPitch = prevRotationPitch + (rotationPitch - prevRotationPitch) * 0.2f;
        rotationYaw = prevRotationYaw + (rotationYaw - prevRotationYaw) * 0.2f;

        float drag = 0.99f;
        if (isInWater) {
            drag = 0.8f;
        }

        motionX *= drag;
        motionY *= drag;
        motionZ *= drag;
        motionY -= 0.03;
        setPosition(posX, posY, posZ);
    }

    EntityLiving* shooter = nullptr;
    int tileX = -1;
    int tileY = -1;
    int tileZ = -1;
    int inTile = 0;
    bool inGround = false;
    int arrowShake = 0;
    int ticksInGround = 0;
    int ticksInAir = 0;
};
