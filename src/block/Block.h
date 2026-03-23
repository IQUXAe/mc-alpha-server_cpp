#pragma once

#include <vector>
#include <memory>
#include <string>
#include <optional>

// Forward declarations
class Material;
class World;
class Entity;
class EntityPlayer;
class AxisAlignedBB;
class Vec3D;

class Block {
public:
    // Global static arrays mirroring Java's Block class
    static Block* blocksList[256];
    static bool tickOnLoad[256];
    static bool isBlockContainer[256];
    static int lightOpacity[256];
    static int lightValue[256];

    // Pre-defined blocks
    static Block* stone;
    static Block* grass;
    static Block* dirt;
    static Block* cobblestone;
    static Block* planks;
    static Block* bedrock;
    static Block* sand;
    static Block* gravel;
    static Block* wood;
    static Block* leaves;
    static Block* glass;
    // World gen blocks
    static Block* oreGold;
    static Block* oreIron;
    static Block* oreCoal;
    static Block* oreDiamond;
    static Block* oreRedstone;
    static Block* blockClay;
    static Block* cactus;
    static Block* reed;
    static Block* pumpkin;
    static Block* snow;
    static Block* ice;
    static Block* cobblestoneMossy;
    static Block* mobSpawner;
    static Block* plantYellow;
    static Block* plantRed;
    static Block* mushroomBrown;
    static Block* mushroomRed;
    static Block* sapling;

    // Instance variables
    const int blockID;
    Material* blockMaterial;
    float blockHardness;
    float blockResistance;
    double minX, minY, minZ;
    double maxX, maxY, maxZ;

    Block(int id, Material* material);
    virtual ~Block() = default;

    static void initBlocks(); // Must be called once on server startup

    virtual Block* setHardness(float hardness);
    virtual Block* setResistance(float resistance);
    virtual Block* setLightOpacity(int opacity);
    virtual Block* setLightValue(float value);
    virtual Block* setTickOnLoad(bool tick);

    void setBlockBounds(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);

    virtual std::optional<AxisAlignedBB> getCollisionBoundingBoxFromPool(World* world, int x, int y, int z);
    virtual void getCollidingBoundingBoxes(World* world, int x, int y, int z, const AxisAlignedBB& mask, std::vector<AxisAlignedBB>& list);

    virtual bool isCollidable() const { return true; }
    virtual bool canCollideCheck(int metadata, bool something) const { return isCollidable(); }

    virtual void onBlockAdded(World* world, int x, int y, int z) {}
    virtual void onBlockRemoval(World* world, int x, int y, int z) {}
    virtual void onNeighborBlockChange(World* world, int x, int y, int z, int neighborId) {}
    virtual void onBlockDestroyedByPlayer(World* world, int x, int y, int z, int metadata) {}
    virtual bool blockActivated(World* world, int x, int y, int z, EntityPlayer* player) { return false; }
    
    virtual void updateTick(World* world, int x, int y, int z) {}
    virtual int tickRate() const { return 10; }

    virtual int quantityDropped() const { return 1; }
    virtual int idDropped(int metadata) const { return blockID; }
    // Returns the damage value of the item dropped (0 = no durability/variant).
    // Mirrors Java's Block.damageDropped(). Most blocks return 0.
    virtual int damageDropped(int metadata) const { return 0; }
    virtual bool canHarvestBlock(EntityPlayer* player) const { return true; }
    virtual void dropBlockAsItem(World* world, int x, int y, int z, int metadata);
    virtual void dropBlockAsItemWithChance(World* world, int x, int y, int z, int metadata, float chance);

    virtual float checkHardness(EntityPlayer* player) const;

    virtual void onBlockPlaced(World* world, int x, int y, int z, int side) {}
    virtual bool canBlockStay(World* world, int x, int y, int z) const { return true; }
    virtual bool isReplaceable() const { return false; }
};
