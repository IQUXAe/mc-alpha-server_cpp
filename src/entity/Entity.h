#pragma once

#include "../core/AxisAlignedBB.h"
#include "../core/Vec3D.h"
#include <string>
#include <cstdint>

class World;

class Entity {
public:
    int32_t entityId;
    double posX = 0.0, posY = 0.0, posZ = 0.0;
    double prevPosX = 0.0, prevPosY = 0.0, prevPosZ = 0.0;
    double motionX = 0.0, motionY = 0.0, motionZ = 0.0;
    float rotationYaw = 0.0f;
    float rotationPitch = 0.0f;
    float prevRotationYaw = 0.0f;
    float prevRotationPitch = 0.0f;
    AxisAlignedBB boundingBox;
    bool onGround = false;
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

    World* worldObj = nullptr;

    Entity() : entityId(nextEntityId++) {}
    virtual ~Entity() = default;

    virtual void tick() {
        prevPosX = posX;
        prevPosY = posY;
        prevPosZ = posZ;
        prevRotationYaw = rotationYaw;
        prevRotationPitch = rotationPitch;
    }

    virtual void setPosition(double x, double y, double z) {
        posX = x;
        posY = y;
        posZ = z;
        double hw = width / 2.0;
        boundingBox = AxisAlignedBB(x - hw, y - yOffset, z - hw,
                                     x + hw, y - yOffset + height, z + hw);
    }

    virtual void moveEntity(double dx, double dy, double dz) {
        posX += dx;
        posY += dy;
        posZ += dz;
        setPosition(posX, posY, posZ);
    }

    double getDistanceSq(double x, double y, double z) const {
        double dx = posX - x;
        double dy = posY - y;
        double dz = posZ - z;
        return dx * dx + dy * dy + dz * dz;
    }

    double getDistanceSqToEntity(const Entity& other) const {
        return getDistanceSq(other.posX, other.posY, other.posZ);
    }

    virtual bool isEntityAlive() const { return !isDead; }

    static int32_t nextEntityId;
};
