#pragma once

#include "Entity.h"
#include "../world/World.h"
#include "../block/Block.h"

class EntityFallingSand : public Entity {
public:
    int blockId;
    int fallTime = 0;

    EntityFallingSand(int blockId, double x, double y, double z)
        : blockId(blockId) {
        width  = 0.98f;
        height = 0.98f;
        yOffset = height / 2.0f;
        setPosition(x, y, z);
        motionX = motionY = motionZ = 0.0;
        noClip = false;
    }

    void tick() override {
        Entity::tick();
        if (blockId == 0) { isDead = true; return; }

        ++fallTime;
        motionY -= 0.04;
        moveEntity(motionX, motionY, motionZ);
        motionX *= 0.98;
        motionY *= 0.98;
        motionZ *= 0.98;

        if (!worldObj) return;

        int bx = (int)std::floor(posX);
        int by = (int)std::floor(posY);
        int bz = (int)std::floor(posZ);

        if (onGround) {
            isDead = true;
            if (by >= 0 && by < 128) {
                int landId = worldObj->getBlockId(bx, by, bz);
                Block* landBlock = landId > 0 ? Block::blocksList[landId] : nullptr;
                bool canPlace = (landId == 0) || (landBlock && landBlock->isReplaceable());
                if (canPlace && Block::blocksList[blockId]) {
                    worldObj->setBlockWithNotify(bx, by, bz, blockId);
                } else {
                    auto drop = std::make_unique<EntityItem>(blockId, 1, 0);
                    drop->setPosition(posX, posY + 0.5, posZ);
                    worldObj->spawnEntityInWorld(std::move(drop));
                }
            }
        } else if (fallTime > 100) {
            isDead = true;
            auto drop = std::make_unique<EntityItem>(blockId, 1, 0);
            drop->setPosition(posX, posY, posZ);
            worldObj->spawnEntityInWorld(std::move(drop));
        }
    }
};
