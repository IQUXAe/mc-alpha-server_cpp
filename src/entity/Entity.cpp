#include "Entity.h"
#include "../world/World.h"
#include <vector>
#include <algorithm>

int32_t Entity::nextEntityId = 1;

void Entity::moveEntity(double dx, double dy, double dz) {
    if (noClip) {
        setPosition(posX + dx, posY + dy, posZ + dz);
        return;
    }

    double oldX = dx;
    double oldY = dy;
    double oldZ = dz;

    if (worldObj) {
        std::vector<AxisAlignedBB> boxes;
        AxisAlignedBB expanded = boundingBox.addCoord(dx, dy, dz);
        worldObj->getCollidingBoundingBoxes(this, expanded, boxes);

        for (const auto& box : boxes) {
            dy = box.calculateYOffset(boundingBox, dy);
        }
        boundingBox.offset(0, dy, 0);

        for (const auto& box : boxes) {
            dx = box.calculateXOffset(boundingBox, dx);
        }
        boundingBox.offset(dx, 0, 0);

        for (const auto& box : boxes) {
            dz = box.calculateZOffset(boundingBox, dz);
        }
        boundingBox.offset(0, 0, dz);
    }

    posX = (boundingBox.minX + boundingBox.maxX) / 2.0;
    posY = boundingBox.minY + yOffset;
    posZ = (boundingBox.minZ + boundingBox.maxZ) / 2.0;

    onGround = (oldY != dy && oldY < 0.0);

    if (oldX != dx) motionX = 0.0;
    if (oldY != dy) motionY = 0.0;
    if (oldZ != dz) motionZ = 0.0;
}
