#pragma once

#include "Block.h"
#include "../core/Material.h"
#include "../world/World.h"
#include "../core/MathHelper.h"

#include <cstdlib>

class BlockFire : public Block {
public:
    // Per-block-ID: chance to encourage fire, ability to catch fire
    int chanceToEncourageFire[256]{};
    int abilityToCatchFire[256]{};

    BlockFire(int id, int tex)
        : Block(id, &Material::fire) {
        setTickOnLoad(true);
        // Burn rates matching Java BlockFire constructor
        setBurnRate(5, 5, 20);   // planks
        setBurnRate(17, 5, 5);   // wood (log)
        setBurnRate(18, 30, 60); // leaves
        setBurnRate(47, 30, 20); // bookshelf
        setBurnRate(46, 15, 100);// TNT
        setBurnRate(35, 30, 60); // cloth (wool)
    }

    std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World*, int, int, int) override {
        return std::nullopt;
    }

    bool isCollidable() const override { return false; }

    int quantityDropped() const override { return 0; }

    int tickRate() const override { return 10; }

    void updateTick(World* world, int x, int y, int z) override {
        const bool onNetherrack = world->getBlockId(x, y - 1, z) == 87;
        int meta = world->getBlockMetadata(x, y, z);

        // Age the fire: metadata 0..15
        if (meta < 15) {
            world->setBlockMetadata(x, y, z, static_cast<uint8_t>(meta + 1));
            world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
        }

        if (!onNetherrack && !hasBurnableNeighbor(world, x, y, z)) {
            if (!world->doesBlockAllowAttachment(x, y - 1, z) || meta > 3) {
                world->setBlockWithNotify(x, y, z, 0);
            }
        } else if (!onNetherrack
                && !(chanceToEncourageFire[world->getBlockId(x, y - 1, z)] > 0)
                && meta == 15 && (std::rand() % 4) == 0) {
            world->setBlockWithNotify(x, y, z, 0);
        } else {
            if (meta % 2 == 0 && meta > 2) {
                tryToCatchBlockOnFire(world, x + 1, y, z, 300);
                tryToCatchBlockOnFire(world, x - 1, y, z, 300);
                tryToCatchBlockOnFire(world, x, y - 1, z, 250);
                tryToCatchBlockOnFire(world, x, y + 1, z, 250);
                tryToCatchBlockOnFire(world, x, y, z - 1, 300);
                tryToCatchBlockOnFire(world, x, y, z + 1, 300);

                for (int nx = x - 1; nx <= x + 1; ++nx) {
                    for (int nz = z - 1; nz <= z + 1; ++nz) {
                        for (int ny = y - 1; ny <= y + 4; ++ny) {
                            if (nx == x && ny == y && nz == z) continue;
                            int chance = 100;
                            if (ny > y + 1) {
                                chance += (ny - (y + 1)) * 100;
                            }
                            int neighborChance = getChanceOfNeighborsEncouragingFire(world, nx, ny, nz);
                            if (neighborChance > 0 && (std::rand() % chance) < neighborChance) {
                                world->setBlockWithNotify(nx, ny, nz, blockID);
                            }
                        }
                    }
                }
            }
        }
    }

    bool canPlaceBlockAt(World* world, int x, int y, int z) override {
        return world->doesBlockAllowAttachment(x, y - 1, z) || hasBurnableNeighbor(world, x, y, z);
    }

    void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) override {
        if (!world->doesBlockAllowAttachment(x, y - 1, z) && !hasBurnableNeighbor(world, x, y, z)) {
            world->setBlockWithNotify(x, y, z, 0);
        }
    }

    void onBlockAdded(World* world, int x, int y, int z) override {
        if (!world->doesBlockAllowAttachment(x, y - 1, z) && !hasBurnableNeighbor(world, x, y, z)) {
            world->setBlockWithNotify(x, y, z, 0);
        } else {
            world->scheduleBlockUpdate(x, y, z, blockID, tickRate());
        }
    }

    // Check if any adjacent block can burn
    bool hasBurnableNeighbor(World* world, int x, int y, int z) {
        return getChanceToEncourageFire(world, x + 1, y, z, 0) > 0
            || getChanceToEncourageFire(world, x - 1, y, z, 0) > 0
            || getChanceToEncourageFire(world, x, y - 1, z, 0) > 0
            || getChanceToEncourageFire(world, x, y + 1, z, 0) > 0
            || getChanceToEncourageFire(world, x, y, z - 1, 0) > 0
            || getChanceToEncourageFire(world, x, y, z + 1, 0) > 0;
    }

    void tryToCatchBlockOnFire(World* world, int x, int y, int z, int chance) {
        int blockId = world->getBlockId(x, y, z);
        int ability = abilityToCatchFire[blockId];
        if (ability > 0 && (std::rand() % chance) < ability) {
            bool isTnt = (blockId == 46);
            if ((std::rand() % 2) == 0) {
                world->setBlockWithNotify(x, y, z, blockID);
            } else {
                world->setBlockWithNotify(x, y, z, 0);
            }
            if (isTnt) {
                Block::blocksList[46]->onBlockDestroyedByPlayer(world, x, y, z, 0);
            }
        }
    }

    int getChanceOfNeighborsEncouragingFire(World* world, int x, int y, int z) {
        if (world->getBlockId(x, y, z) != 0) return 0;
        int result = 0;
        result = getChanceToEncourageFire(world, x + 1, y, z, result);
        result = getChanceToEncourageFire(world, x - 1, y, z, result);
        result = getChanceToEncourageFire(world, x, y - 1, z, result);
        result = getChanceToEncourageFire(world, x, y + 1, z, result);
        result = getChanceToEncourageFire(world, x, y, z - 1, result);
        result = getChanceToEncourageFire(world, x, y, z + 1, result);
        return result;
    }

    int getChanceToEncourageFire(World* world, int x, int y, int z, int currentMax) {
        int blockId = world->getBlockId(x, y, z);
        int val = chanceToEncourageFire[blockId];
        return val > currentMax ? val : currentMax;
    }

    void setBurnRate(int blockId, int chance, int ability) {
        if (blockId >= 0 && blockId < 256) {
            chanceToEncourageFire[blockId] = chance;
            abilityToCatchFire[blockId] = ability;
        }
    }
};
