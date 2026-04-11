#pragma once

#include "../core/AxisAlignedBB.h"
#include "../core/Vec3D.h"
#include <string>
#include <cstdint>
#include <cmath>

class World;
class Material;

class Entity {
public:
    int32_t entityId;
    double posX = 0.0, posY = 0.0, posZ = 0.0;
    double prevPosX = 0.0, prevPosY = 0.0, prevPosZ = 0.0;
    double lastX = 0.0, lastY = 0.0, lastZ = 0.0;  // For entity tracking
    double motionX = 0.0, motionY = 0.0, motionZ = 0.0;
    float rotationYaw = 0.0f;
    float rotationPitch = 0.0f;
    float prevRotationYaw = 0.0f;
    float prevRotationPitch = 0.0f;
    float lastYaw = 0.0f;    // For entity tracking
    float lastPitch = 0.0f;  // For entity tracking
    AxisAlignedBB boundingBox;
    bool onGround = false;
    bool collidedHorizontally = false;
    bool collidedVertically = false;
    bool isCollided = false;
    bool isDead = false;
    float height = 1.8f;
    float width = 0.6f;
    float yOffset = 0.0f;
    float stepHeight = 0.0f;
    bool noClip = false;
    float fallDistance = 0.0f;
    int fire = 0;
    int air = 300;
    bool isInWater = false;
    int dimension = 0;
    int fireResistance = 1;
    bool suppressMoveFallState = false;
    Entity* ridingEntity = nullptr;
    Entity* riddenByEntity = nullptr;

    World* worldObj = nullptr;

    Entity() : entityId(nextEntityId++) {}
    virtual ~Entity() = default;

    virtual void tick() {
        if (ridingEntity && ridingEntity->isDead) {
            ridingEntity = nullptr;
        }
        if (riddenByEntity && riddenByEntity->isDead) {
            riddenByEntity = nullptr;
        }
        prevPosX = posX;
        prevPosY = posY;
        prevPosZ = posZ;
        prevRotationYaw = rotationYaw;
        prevRotationPitch = rotationPitch;
        lastX = posX;
        lastY = posY;
        lastZ = posZ;
        lastYaw = rotationYaw;
        lastPitch = rotationPitch;
        updateEnvironmentalState();
    }

    virtual void setPosition(double x, double y, double z) {
        posX = x;
        posY = y;
        posZ = z;
        double hw = width / 2.0;
        boundingBox = AxisAlignedBB(x - hw, y - yOffset, z - hw,
                                     x + hw, y - yOffset + height, z + hw);
    }

    virtual void setPositionAndRotation(double x, double y, double z, float yaw, float pitch) {
        setPosition(x, y, z);
        rotationYaw = yaw;
        rotationPitch = pitch;
    }

    virtual void moveEntity(double dx, double dy, double dz);
    virtual void addVelocity(double dx, double dy, double dz) {
        motionX += dx;
        motionY += dy;
        motionZ += dz;
    }
    virtual bool canBePushed() const { return !isDead; }
    virtual bool canBeCollidedWith() const { return !isDead; }
    virtual bool preventsEntitySpawning() const { return false; }
    virtual void applyEntityCollision(Entity* other);
    virtual void attackEntityFrom(Entity* attacker, int amount) {}
    virtual float getEyeHeight() const { return 0.0f; }
    virtual double getMountedYOffset() const { return static_cast<double>(height) * 0.75; }
    virtual void updateRiderPosition();
    virtual void updateRidden();
    virtual void mountEntity(Entity* vehicle);
    virtual void onFall(float distance) {}
    virtual void setOnFire(int ticks);
    virtual void onStruckByFire() { attackEntityFrom(nullptr, 1); }
    virtual bool isImmuneToFire() const { return false; }

    double getDistanceSq(double x, double y, double z) const {
        double dx = posX - x;
        double dy = posY - y;
        double dz = posZ - z;
        return dx * dx + dy * dy + dz * dz;
    }

    double getDistanceSqToEntity(const Entity& other) const {
        return getDistanceSq(other.posX, other.posY, other.posZ);
    }

    float getDistanceToEntity(const Entity* other) const {
        if (!other) return 0.0f;
        double dx = posX - other->posX;
        double dy = posY - other->posY;
        double dz = posZ - other->posZ;
        return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    virtual bool isEntityAlive() const { return !isDead; }

    static int32_t nextEntityId;

protected:
    void updateEnvironmentalState();
    bool isInsideMaterial(Material* material) const;
    bool isInsideOpaqueBlock() const;
    bool isInLava() const;
    void updateFallState(double dy);
};
