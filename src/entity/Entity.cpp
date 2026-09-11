#include "Entity.h"
#include "../block/Block.h"
#include "../core/Material.h"
#include "../core/MathHelper.h"
#include "../core/RustBridge.h"
#include "../world/World.h"
#include <vector>
#include <algorithm>

namespace {

RustBridge::FfiAabb toFfiBox(const AxisAlignedBB& box) {
    return RustBridge::FfiAabb{box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ};
}

AxisAlignedBB fromFfiBox(const RustBridge::FfiAabb& box) {
    return AxisAlignedBB(box.min_x, box.min_y, box.min_z, box.max_x, box.max_y, box.max_z);
}

} // namespace

std::atomic<int32_t> Entity::nextEntityId{1};

Entity* Entity::getRidingEntity() const {
    return ridingEntityId >= 0 && worldObj ? worldObj->getEntityById(ridingEntityId) : nullptr;
}

Entity* Entity::getRiddenByEntity() const {
    return riddenByEntityId >= 0 && worldObj ? worldObj->getEntityById(riddenByEntityId) : nullptr;
}

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
        // Collision resolution owned by Rust (entity_physics.rs): Y, then X,
        // then Z passes over the boxes gathered here for the expanded box.
        auto resolveMovement = [&](AxisAlignedBB box, double moveX, double moveY, double moveZ) {
            std::vector<AxisAlignedBB> boxes;
            worldObj->getCollidingBoundingBoxes(this, box.addCoord(moveX, moveY, moveZ), boxes);

            std::vector<RustBridge::FfiAabb> ffiBoxes;
            ffiBoxes.reserve(boxes.size());
            for (const auto& collisionBox : boxes) {
                ffiBoxes.push_back(toFfiBox(collisionBox));
            }
            RustBridge::ResolvedMove resolved{};
            RustBridge::FfiAabb ffiBox = toFfiBox(box);
            RustBridge::entityResolveMove(ffiBox, moveX, moveY, moveZ,
                                          ffiBoxes.data(), ffiBoxes.size(), &resolved);
            box = fromFfiBox(resolved.box_);

            return std::tuple{box, resolved.dx, resolved.dy, resolved.dz};
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
        if (blockId > 0 && blockId < 256) {
            Block* block = Block::blocksList[blockId];
            if (block) {
                block->onEntityWalking(worldObj, blockX, blockY, blockZ, this);
            }
        }
    }

    updateEnvironmentalState();
}

void Entity::applyEntityCollision(Entity* other) {
    if (!other || other == this) {
        return;
    }
    RustBridge::PushOut push{};
    if (!RustBridge::entityPush(posX, posZ, other->posX, other->posZ,
                                canBePushed(), other->canBePushed(), &push)) {
        return;
    }
    addVelocity(push.dvx1, 0.0, push.dvz1);
    other->addVelocity(push.dvx2, 0.0, push.dvz2);
}

void Entity::updateRiderPosition() {
    if (Entity* rider = getRiddenByEntity()) {
        rider->setPosition(posX, posY + getMountedYOffset(), posZ);
    }
}

void Entity::mountEntity(Entity* vehicle) {
    Entity* currentVehicle = getRidingEntity();

    if (currentVehicle == vehicle) {
        if (vehicle && vehicle->riddenByEntityId == entityId) {
            vehicle->riddenByEntityId = -1;
        }
        ridingEntityId = -1;
        return;
    }

    if (currentVehicle && currentVehicle->riddenByEntityId == entityId) {
        currentVehicle->riddenByEntityId = -1;
    }

    if (vehicle && vehicle->riddenByEntityId >= 0 && vehicle->riddenByEntityId != entityId) {
        if (Entity* currentRider = vehicle->getRiddenByEntity()) {
            currentRider->ridingEntityId = -1;
        }
    }

    ridingEntityId = vehicle ? vehicle->entityId : -1;
    if (vehicle) {
        vehicle->riddenByEntityId = entityId;
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
    float fallEvent = -1.0f;
    fallDistance = RustBridge::entityFallStep(onGround, dy, fallDistance, &fallEvent);
    if (fallEvent >= 0.0f) {
        onFall(fallEvent);
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
