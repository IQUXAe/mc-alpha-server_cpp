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

#include "../core/RustBridge.h"

inline thread_local World* gBoatWorld = nullptr;

extern "C" inline bool boatIsWaterCell(int32_t x, int32_t y, int32_t z) {
    return gBoatWorld->getBlockMaterialNoChunkLoad(x, y, z) == &Material::water;
}

struct BoatWorldGuard {
    explicit BoatWorldGuard(World* world) : prev_(gBoatWorld) { gBoatWorld = world; }
    ~BoatWorldGuard() { gBoatWorld = prev_; }

private:
    World* prev_;
};

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
            if (Entity* rider = getRiddenByEntity()) {
                rider->mountEntity(nullptr);
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
        if (Entity* rider = getRiddenByEntity()) {
            if (!dynamic_cast<EntityPlayerMP*>(rider)) {
                rider->mountEntity(nullptr);
            }
        }

        const double waterFraction = computeWaterFraction();
        motionY += 0.04 * (waterFraction * 2.0 - 1.0);

        if (Entity* rider = getRiddenByEntity()) {
            motionX += rider->motionX * 0.2;
            motionZ += rider->motionZ * 0.2;
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
            if (Entity* rider = getRiddenByEntity()) {
                rider->mountEntity(nullptr);
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
        float steeredYaw = rotationYaw;
        if (RustBridge::boatSteer(deltaX, deltaZ, rotationYaw, &steeredYaw)) {
            rotationYaw = steeredYaw;
        }

        std::vector<Entity*> nearbyEntities;
        worldObj->getEntitiesWithinAABBExcludingEntity(this, boundingBox.expand(0.2, 0.0, 0.2), nearbyEntities);
        for (Entity* entity : nearbyEntities) {
            if (entity && entity != getRiddenByEntity() && dynamic_cast<EntityBoat*>(entity)) {
                entity->applyEntityCollision(this);
            }
        }

        if (Entity* rider = getRiddenByEntity(); rider && rider->isDead) {
            riddenByEntityId = -1;
        }

        updateRiderPosition();
    }

    void updateRiderPosition() override {
        Entity* rider = getRiddenByEntity();
        if (!rider) {
            return;
        }

        double offsetX = 0.4;
        double offsetZ = 0.0;
        RustBridge::boatRiderOffset(rotationYaw, &offsetX, &offsetZ);
        rider->setPosition(posX + offsetX, posY + 0.35, posZ + offsetZ);
    }

private:
    double computeWaterFraction() const {
        if (!worldObj) {
            return 0.0;
        }

        BoatWorldGuard guard(worldObj);
        return RustBridge::boatWaterFraction(boundingBox.minX, boundingBox.minY, boundingBox.minZ,
                                             boundingBox.maxX, boundingBox.maxY, boundingBox.maxZ,
                                             &boatIsWaterCell);
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
