#include "Entity.h"
#include "../block/Block.h"
#include "../core/Material.h"
#include "../core/MathHelper.h"
#include "../world/World.h"
#include <vector>
#include <algorithm>

int32_t Entity::nextEntityId = 1;

void Entity::moveEntity(double dx, double dy, double dz) {
    if (noClip) {
        setPosition(posX + dx, posY + dy, posZ + dz);
        return;
    }

    const double oldX = dx;
    const double oldY = dy;
    const double oldZ = dz;
    const AxisAlignedBB originalBoundingBox = boundingBox;

    if (worldObj) {
        auto resolveMovement = [&](AxisAlignedBB box, double moveX, double moveY, double moveZ) {
            std::vector<AxisAlignedBB> boxes;
            worldObj->getCollidingBoundingBoxes(this, box.addCoord(moveX, moveY, moveZ), boxes);

            for (const auto& collisionBox : boxes) {
                moveY = collisionBox.calculateYOffset(box, moveY);
            }
            box.offset(0.0, moveY, 0.0);

            for (const auto& collisionBox : boxes) {
                moveX = collisionBox.calculateXOffset(box, moveX);
            }
            box.offset(moveX, 0.0, 0.0);

            for (const auto& collisionBox : boxes) {
                moveZ = collisionBox.calculateZOffset(box, moveZ);
            }
            box.offset(0.0, 0.0, moveZ);

            return std::tuple{box, moveX, moveY, moveZ};
        };

        auto [resolvedBox, resolvedX, resolvedY, resolvedZ] = resolveMovement(boundingBox, dx, dy, dz);
        boundingBox = resolvedBox;
        dx = resolvedX;
        dy = resolvedY;
        dz = resolvedZ;

        const bool canStepUp = stepHeight > 0.0f
            && (onGround || (oldY != dy && oldY < 0.0))
            && (oldX != dx || oldZ != dz);
        if (canStepUp) {
            auto [stepBox, stepX, stepY, stepZ] = resolveMovement(
                originalBoundingBox,
                oldX,
                static_cast<double>(stepHeight),
                oldZ);

            const double flatDistance = dx * dx + dz * dz;
            const double steppedDistance = stepX * stepX + stepZ * stepZ;
            if (steppedDistance > flatDistance) {
                boundingBox = stepBox;
                dx = stepX;
                dy = stepY;
                dz = stepZ;
            }
        }
    }

    posX = (boundingBox.minX + boundingBox.maxX) / 2.0;
    posY = boundingBox.minY + yOffset;
    posZ = (boundingBox.minZ + boundingBox.maxZ) / 2.0;

    collidedHorizontally = (oldX != dx || oldZ != dz);
    collidedVertically = (oldY != dy);
    onGround = (oldY != dy && oldY < 0.0);
    isCollided = collidedHorizontally || collidedVertically;

    if (oldX != dx) motionX = 0.0;
    if (oldY != dy) motionY = 0.0;
    if (oldZ != dz) motionZ = 0.0;

    if (!suppressMoveFallState) {
        updateFallState(dy);
    }

    if (worldObj && onGround && (oldX != dx || oldZ != dz)) {
        const int blockX = MathHelper::floor_double(posX);
        const int blockY = MathHelper::floor_double(boundingBox.minY - 0.2);
        const int blockZ = MathHelper::floor_double(posZ);
        const int blockId = worldObj->getBlockIdNoChunkLoad(blockX, blockY, blockZ);
        if (blockId > 0) {
            Block* block = Block::blocksList[blockId];
            if (block) {
                block->onEntityWalking(worldObj, blockX, blockY, blockZ, this);
            }
        }
    }

    updateEnvironmentalState();
}

void Entity::applyEntityCollision(Entity* other) {
    if (!other || other == this || !canBePushed() || !other->canBePushed()) {
        return;
    }

    double dx = other->posX - posX;
    double dz = other->posZ - posZ;
    double maxAbs = MathHelper::abs_max(dx, dz);
    if (maxAbs < 0.01) {
        return;
    }

    maxAbs = MathHelper::sqrt_double(maxAbs);
    dx /= maxAbs;
    dz /= maxAbs;

    double impulseScale = 1.0 / maxAbs;
    if (impulseScale > 1.0) {
        impulseScale = 1.0;
    }

    dx *= impulseScale * 0.05;
    dz *= impulseScale * 0.05;

    addVelocity(-dx, 0.0, -dz);
    other->addVelocity(dx, 0.0, dz);
}

void Entity::updateRiderPosition() {
    if (!riddenByEntity) {
        return;
    }
    riddenByEntity->setPosition(posX, posY + getMountedYOffset(), posZ);
}

void Entity::updateRidden() {
    if (!ridingEntity) {
        return;
    }
    if (ridingEntity->isDead) {
        mountEntity(nullptr);
        return;
    }
    motionX = 0.0;
    motionY = 0.0;
    motionZ = 0.0;
    ridingEntity->updateRiderPosition();
}

void Entity::mountEntity(Entity* vehicle) {
    if (ridingEntity == vehicle) {
        if (vehicle && vehicle->riddenByEntity == this) {
            vehicle->riddenByEntity = nullptr;
        }
        ridingEntity = nullptr;
        return;
    }

    if (ridingEntity && ridingEntity->riddenByEntity == this) {
        ridingEntity->riddenByEntity = nullptr;
    }

    if (vehicle && vehicle->riddenByEntity && vehicle->riddenByEntity != this) {
        vehicle->riddenByEntity->ridingEntity = nullptr;
    }

    ridingEntity = vehicle;
    if (vehicle) {
        vehicle->riddenByEntity = this;
    }
}

void Entity::setOnFire(int ticks) {
    if (isImmuneToFire()) {
        return;
    }
    if (ticks > fire) {
        fire = ticks;
    }
}

bool Entity::isInsideMaterial(Material* material) const {
    if (!worldObj || !material) {
        return false;
    }

    const double sampleY = posY + static_cast<double>(getEyeHeight());
    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(sampleY);
    const int z = MathHelper::floor_double(posZ);

    Material* blockMaterial = worldObj->getBlockMaterialNoChunkLoad(x, y, z);
    if (!blockMaterial || blockMaterial != material) {
        return false;
    }

    return true;
}

bool Entity::isInsideOpaqueBlock() const {
    if (!worldObj) {
        return false;
    }

    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(posY + static_cast<double>(getEyeHeight()));
    const int z = MathHelper::floor_double(posZ);
    return worldObj->isBlockSolidNoChunkLoad(x, y, z);
}

bool Entity::isInLava() const {
    if (!worldObj) {
        return false;
    }

    const AxisAlignedBB probe = boundingBox.expand(0.0, -0.4, 0.0);
    const int minX = MathHelper::floor_double(probe.minX);
    const int minY = MathHelper::floor_double(probe.minY);
    const int minZ = MathHelper::floor_double(probe.minZ);
    const int maxX = MathHelper::floor_double(probe.maxX);
    const int maxY = MathHelper::floor_double(probe.maxY);
    const int maxZ = MathHelper::floor_double(probe.maxZ);

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                Material* material = worldObj->getBlockMaterialNoChunkLoad(x, y, z);
                if (material == &Material::lava) {
                    return true;
                }
            }
        }
    }

    return false;
}

void Entity::updateFallState(double dy) {
    if (onGround) {
        if (fallDistance > 0.0f) {
            onFall(fallDistance);
            fallDistance = 0.0f;
        }
    } else if (dy < 0.0) {
        fallDistance = static_cast<float>(fallDistance - dy);
    }
}

void Entity::updateEnvironmentalState() {
    if (!worldObj || isDead) {
        return;
    }

    isInWater = false;
    const AxisAlignedBB waterProbe = boundingBox.expand(0.0, -0.4, 0.0);
    const int minX = MathHelper::floor_double(waterProbe.minX);
    const int minY = MathHelper::floor_double(waterProbe.minY);
    const int minZ = MathHelper::floor_double(waterProbe.minZ);
    const int maxX = MathHelper::floor_double(waterProbe.maxX);
    const int maxY = MathHelper::floor_double(waterProbe.maxY);
    const int maxZ = MathHelper::floor_double(waterProbe.maxZ);

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                Material* material = worldObj->getBlockMaterialNoChunkLoad(x, y, z);
                if (material == &Material::water) {
                    isInWater = true;
                    break;
                }
            }
            if (isInWater) break;
        }
        if (isInWater) break;
    }

    if (isInWater) {
        fallDistance = 0.0f;
    }

    const int bbMinX = MathHelper::floor_double(boundingBox.minX);
    const int bbMinY = MathHelper::floor_double(boundingBox.minY - 0.001);
    const int bbMinZ = MathHelper::floor_double(boundingBox.minZ);
    const int bbMaxX = MathHelper::floor_double(boundingBox.maxX);
    const int bbMaxY = MathHelper::floor_double(boundingBox.maxY);
    const int bbMaxZ = MathHelper::floor_double(boundingBox.maxZ);

    bool touchedFire = false;
    bool touchedCactus = false;
    for (int x = bbMinX; x <= bbMaxX; ++x) {
        for (int y = bbMinY; y <= bbMaxY; ++y) {
            for (int z = bbMinZ; z <= bbMaxZ; ++z) {
                const int blockId = worldObj->getBlockIdNoChunkLoad(x, y, z);
                if (blockId == 51) {
                    touchedFire = true;
                } else if (blockId == 81) {
                    Block* cactusBlock = Block::blocksList[blockId];
                    if (!cactusBlock) {
                        continue;
                    }
                    auto cactusBox = cactusBlock->getCollisionBoundingBoxFromPool(worldObj, x, y, z);
                    if (cactusBox && boundingBox.expand(0.001, 0.001, 0.001).intersectsWith(*cactusBox)) {
                        touchedCactus = true;
                    }
                }
            }
        }
    }

    if (touchedCactus) {
        attackEntityFrom(nullptr, 1);
    }

    if (touchedFire) {
        onStruckByFire();
        setOnFire(300);
    } else if (fire > 0 && isInWater) {
        fire = 0;
    }

    if (isInLava()) {
        onStruckByFire();
        setOnFire(600);
        fallDistance = 0.0f;
    }

    if (fire > 0) {
        if (isInWater) {
            fire = 0;
        } else {
            if ((fire % 20) == 0) {
                onStruckByFire();
            }
            --fire;
        }
    }
}
