#pragma once

#include "Entity.h"
#include "EntityItem.h"
#include "EntityPlayerMP.h"
#include "../block/Block.h"
#include "../core/Item.h"
#include "../core/Material.h"
#include "../core/MathHelper.h"
#include "../core/NBT.h"
#include "../world/World.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <vector>

class EntityBoat : public Entity {
public:
    int timeSinceHit = 0;
    int damageTaken = 0;
    int forwardDirection = 1;

    explicit EntityBoat(World* world) {
        worldObj = world;
        width = 1.5f;
        height = 0.6f;
        yOffset = height / 2.0f;
        setPosition(posX, posY, posZ);
    }

    EntityBoat(World* world, double x, double y, double z) : EntityBoat(world) {
        setPosition(x, y + static_cast<double>(yOffset), z);
        prevPosX = x;
        prevPosY = y;
        prevPosZ = z;
    }

    bool canBeCollidedWith() const override { return !isDead; }
    bool canBePushed() const override { return !isDead; }
    bool preventsEntitySpawning() const override { return true; }
    double getMountedYOffset() const override { return -0.3; }

    void writeToNBT(NBTCompound& nbt) const {
        nbt.setString("id", "Boat");
        nbt.setDouble("PosX", posX);
        nbt.setDouble("PosY", posY);
        nbt.setDouble("PosZ", posZ);
        nbt.setDouble("MotionX", motionX);
        nbt.setDouble("MotionY", motionY);
        nbt.setDouble("MotionZ", motionZ);
        nbt.setFloat("RotationYaw", rotationYaw);
        nbt.setFloat("RotationPitch", rotationPitch);
        nbt.setInt("TimeSinceHit", timeSinceHit);
        nbt.setInt("DamageTaken", damageTaken);
        nbt.setInt("ForwardDirection", forwardDirection);
    }

    void readFromNBT(const NBTCompound& nbt) {
        setPositionAndRotation(
            nbt.getDouble("PosX"),
            nbt.getDouble("PosY"),
            nbt.getDouble("PosZ"),
            nbt.getFloat("RotationYaw"),
            nbt.getFloat("RotationPitch"));
        motionX = nbt.getDouble("MotionX");
        motionY = nbt.getDouble("MotionY");
        motionZ = nbt.getDouble("MotionZ");
        timeSinceHit = nbt.getInt("TimeSinceHit");
        damageTaken = nbt.getInt("DamageTaken");
        const int savedForwardDirection = nbt.getInt("ForwardDirection");
        if (savedForwardDirection != 0) {
            forwardDirection = savedForwardDirection;
        }
    }

    void attackEntityFrom(Entity* attacker, int amount) override {
        if (isDead || amount <= 0) {
            return;
        }

        forwardDirection = -forwardDirection;
        timeSinceHit = 10;
        damageTaken += amount * 10;

        if (damageTaken > 40) {
            if (riddenByEntity) {
                riddenByEntity->mountEntity(nullptr);
            }
            dropMaterials();
            isDead = true;
        }
    }

    void tick() override {
        Entity::tick();
        if (!worldObj || isDead) {
            return;
        }

        if (timeSinceHit > 0) {
            --timeSinceHit;
        }
        if (damageTaken > 0) {
            --damageTaken;
        }

        // Alpha boats are player-driven; if a mob somehow became a rider
        // (edge case from custom server logic), eject it to avoid stuck boats.
        if (riddenByEntity && !dynamic_cast<EntityPlayerMP*>(riddenByEntity)) {
            riddenByEntity->mountEntity(nullptr);
        }

        const double waterFraction = computeWaterFraction();
        motionY += 0.04 * (waterFraction * 2.0 - 1.0);

        if (riddenByEntity) {
            motionX += riddenByEntity->motionX * 0.2;
            motionZ += riddenByEntity->motionZ * 0.2;
        }

        motionX = std::clamp(motionX, -0.4, 0.4);
        motionZ = std::clamp(motionZ, -0.4, 0.4);

        if (onGround) {
            motionX *= 0.5;
            motionY *= 0.5;
            motionZ *= 0.5;
        }

        moveEntity(motionX, motionY, motionZ);

        const double horizontalSpeed = std::sqrt(motionX * motionX + motionZ * motionZ);
        if (collidedHorizontally && horizontalSpeed > 0.15) {
            if (riddenByEntity) {
                riddenByEntity->mountEntity(nullptr);
            }
            dropMaterials();
            isDead = true;
            return;
        }

        motionX *= 0.99;
        motionY *= 0.95;
        motionZ *= 0.99;

        rotationPitch = 0.0f;
        const double deltaX = prevPosX - posX;
        const double deltaZ = prevPosZ - posZ;
        if (deltaX * deltaX + deltaZ * deltaZ > 0.001) {
            double targetYaw = std::atan2(deltaZ, deltaX) * 180.0 / std::numbers::pi;
            double yawDelta = targetYaw - static_cast<double>(rotationYaw);
            while (yawDelta >= 180.0) yawDelta -= 360.0;
            while (yawDelta < -180.0) yawDelta += 360.0;
            yawDelta = std::clamp(yawDelta, -20.0, 20.0);
            rotationYaw = static_cast<float>(static_cast<double>(rotationYaw) + yawDelta);
        }

        std::vector<Entity*> nearbyEntities;
        worldObj->getEntitiesWithinAABBExcludingEntity(this, boundingBox.expand(0.2, 0.0, 0.2), nearbyEntities);
        for (Entity* entity : nearbyEntities) {
            if (entity && entity != riddenByEntity && dynamic_cast<EntityBoat*>(entity)) {
                entity->applyEntityCollision(this);
            }
        }

        if (riddenByEntity && riddenByEntity->isDead) {
            riddenByEntity = nullptr;
        }

        updateRiderPosition();
    }

    void updateRiderPosition() override {
        if (!riddenByEntity) {
            return;
        }

        const double yawRadians = static_cast<double>(rotationYaw) * std::numbers::pi / 180.0;
        const double offsetX = std::cos(yawRadians) * 0.4;
        const double offsetZ = std::sin(yawRadians) * 0.4;
        riddenByEntity->setPosition(posX + offsetX, posY + 0.35, posZ + offsetZ);
    }

private:
    double computeWaterFraction() const {
        if (!worldObj) {
            return 0.0;
        }

        constexpr int sliceCount = 5;
        double fraction = 0.0;
        for (int slice = 0; slice < sliceCount; ++slice) {
            const double minY = boundingBox.minY + (boundingBox.maxY - boundingBox.minY) * static_cast<double>(slice) / static_cast<double>(sliceCount) - 0.125;
            const double maxY = boundingBox.minY + (boundingBox.maxY - boundingBox.minY) * static_cast<double>(slice + 1) / static_cast<double>(sliceCount) - 0.125;
            const int minX = MathHelper::floor_double(boundingBox.minX);
            const int maxX = MathHelper::floor_double(boundingBox.maxX);
            const int minBlockY = MathHelper::floor_double(minY);
            const int maxBlockY = MathHelper::floor_double(maxY);
            const int minZ = MathHelper::floor_double(boundingBox.minZ);
            const int maxZ = MathHelper::floor_double(boundingBox.maxZ);

            bool sliceInWater = false;
            for (int x = minX; x <= maxX && !sliceInWater; ++x) {
                for (int y = minBlockY; y <= maxBlockY && !sliceInWater; ++y) {
                    for (int z = minZ; z <= maxZ; ++z) {
                        if (worldObj->getBlockMaterialNoChunkLoad(x, y, z) == &Material::water) {
                            sliceInWater = true;
                            break;
                        }
                    }
                }
            }

            if (sliceInWater) {
                fraction += 1.0 / static_cast<double>(sliceCount);
            }
        }

        return fraction;
    }

    void dropMaterials() {
        if (!worldObj) {
            return;
        }

        auto spawnDrop = [this](int itemId) {
            auto item = std::make_unique<EntityItem>(itemId, 1, 0);
            item->setPosition(posX, posY, posZ);
            item->pickupDelay = 10;
            worldObj->spawnEntityInWorld(std::move(item));
        };

        for (int i = 0; i < 3; ++i) {
            spawnDrop(Block::planks ? Block::planks->blockID : 5);
        }
        for (int i = 0; i < 2; ++i) {
            spawnDrop(Item::stick ? Item::stick->itemID : 280);
        }
    }
};
