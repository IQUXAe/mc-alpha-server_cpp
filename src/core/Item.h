#pragma once

#include <string>
#include <vector>
#include "Material.h"

class Block;
class World;
class Entity;
class EntityLiving;
class EntityPlayerMP;
class ItemStack;

class Item {
public:
    static Item* itemsList[32000];
    
    // Standard Alpha items
    static Item* shovelSteel;
    static Item* pickaxeSteel;
    static Item* axeSteel;
    static Item* flintAndSteel;
    static Item* appleRed;
    static Item* bow;
    static Item* arrow;
    static Item* coal;
    static Item* diamond;
    static Item* ingotIron;
    static Item* ingotGold;
    static Item* swordSteel;
    static Item* swordWood;
    static Item* shovelWood;
    static Item* pickaxeWood;
    static Item* axeWood;
    static Item* swordStone;
    static Item* shovelStone;
    static Item* pickaxeStone;
    static Item* axeStone;
    static Item* swordDiamond;
    static Item* shovelDiamond;
    static Item* pickaxeDiamond;
    static Item* axeDiamond;
    static Item* stick;
    static Item* bowlEmpty;
    static Item* bowlSoup;
    static Item* swordGold;
    static Item* shovelGold;
    static Item* pickaxeGold;
    static Item* axeGold;
    static Item* silk;
    static Item* feather;
    static Item* gunpowder;
    static Item* hoeWood;
    static Item* hoeStone;
    static Item* hoeSteel;
    static Item* hoeDiamond;
    static Item* hoeGold;
    static Item* seeds;
    static Item* wheat;
    static Item* bread;
    static Item* helmetLeather;
    static Item* plateLeather;
    static Item* legsLeather;
    static Item* bootsLeather;
    static Item* helmetChain;
    static Item* plateChain;
    static Item* legsChain;
    static Item* bootsChain;
    static Item* helmetSteel;
    static Item* plateSteel;
    static Item* legsSteel;
    static Item* bootsSteel;
    static Item* helmetDiamond;
    static Item* plateDiamond;
    static Item* legsDiamond;
    static Item* bootsDiamond;
    static Item* helmetGold;
    static Item* plateGold;
    static Item* legsGold;
    static Item* bootsGold;
    static Item* flint;
    static Item* porkRaw;
    static Item* porkCooked;
    static Item* painting;
    static Item* appleGold;
    static Item* sign;
    static Item* doorWood;
    static Item* bucketEmpty;
    static Item* bucketWater;
    static Item* bucketLava;
    static Item* minecartEmpty;
    static Item* saddle;
    static Item* doorSteel;
    static Item* redstone;
    static Item* snowball;
    static Item* boat;
    static Item* leather;
    static Item* bucketMilk;
    static Item* brick;
    static Item* clay;
    static Item* reed;
    static Item* paper;
    static Item* book;
    static Item* slimeBall;
    static Item* minecartCrate;
    static Item* minecartPowered;
    static Item* egg;
    static Item* compass;
    static Item* fishingRod;
    static Item* pocketSundial;
    static Item* lightstoneDust;
    static Item* fishRaw;
    static Item* fishCooked;

    int itemID;
    int maxStackSize = 64;
    int maxDamage = 0;

    Item(int id); // id is offset from 256, registers at itemID = id+256
    virtual ~Item() = default;

    Item* setMaxStackSize(int size);
    Item* setMaxDamage(int damage);
    int getMaxStackSize() const { return maxStackSize; }

    virtual bool onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side);
    virtual ItemStack onItemRightClick(ItemStack* stack, World* world, EntityPlayerMP* player);
    virtual int getDamageVsEntity(Entity* entity) const { return 1; }
    virtual void hitEntity(ItemStack* stack, EntityLiving* entity) {}

    static void initItems();

protected:
    Item() {} // for subclasses that handle registration themselves
};

// Represents a placeable block as an item (mirrors Java's ItemBlock)
class ItemBlock : public Item {
public:
    int blockID;

    ItemBlock(int blockId);
    bool onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) override;
};

// Sign item: places sign post on top face, wall sign on side faces
class ItemSign : public Item {
public:
    ItemSign(int id) : Item() {
        itemID = id;
        maxStackSize = 1;
        itemsList[id] = this;
    }
    bool onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) override;
};

class ItemFood : public Item {
public:
    ItemFood(int id, int healAmount);

    ItemStack onItemRightClick(ItemStack* stack, World* world, EntityPlayerMP* player) override;

protected:
    int healAmount_;
};

class ItemSoup : public ItemFood {
public:
    ItemSoup(int id, int healAmount) : ItemFood(id, healAmount) {}

    ItemStack onItemRightClick(ItemStack* stack, World* world, EntityPlayerMP* player) override;
};

// Tool material levels: 0=wood, 1=stone, 2=steel, 3=diamond, 4=gold
class ItemTool : public Item {
public:
    float digSpeed;
    int toolLevel;
    std::vector<int> effectiveBlockIDs;

    ItemTool(int id, float speed, int level, std::vector<int> blocks)
        : digSpeed(speed), toolLevel(level), effectiveBlockIDs(std::move(blocks)) {
        itemID = id;
        maxStackSize = 1;
        maxDamage = 32 << level;
        if (level == 3) maxDamage *= 4;
        damageVsEntity_ = 1 + level;
        itemsList[id] = this;
    }

    // Returns dig speed multiplier for a block.
    // Checks explicit list first, then material-based fallback.
    float getStrVsBlock(int blockId) const;
    int getDamageVsEntity(Entity* entity) const override { return damageVsEntity_; }
    void hitEntity(ItemStack* stack, EntityLiving* entity) override;

    virtual bool canHarvestBlock(int blockId) const { return false; }

protected:
    virtual bool isEffectiveAgainst(Block* block) const { return false; }
    int damageVsEntity_ = 0;
};

class ItemPickaxe : public ItemTool {
public:
    ItemPickaxe(int id, int level) : ItemTool(id, (level + 1) * 2.0f, level,
        {4,43,44,1,48,15,42,7,14,56,57,79,87}) {}

    bool canHarvestBlock(int blockId) const override;
protected:
    // Pickaxe is effective against all rock and iron material blocks
    bool isEffectiveAgainst(Block* block) const override;
};

class ItemSpade : public ItemTool {
public:
    ItemSpade(int id, int level) : ItemTool(id, (level + 1) * 2.0f, level,
        {2,3,12,13,78,80,82}) {}

    bool canHarvestBlock(int blockId) const override { return blockId == 78 || blockId == 80; }
protected:
    // Spade is effective against ground/sand/snow material blocks
    bool isEffectiveAgainst(Block* block) const override;
};

class ItemAxe : public ItemTool {
public:
    ItemAxe(int id, int level) : ItemTool(id, (level + 1) * 2.0f, level,
        {5,47,17,54}) {}
protected:
    // Axe is effective against wood material blocks
    bool isEffectiveAgainst(Block* block) const override;
};

class ItemSword : public Item {
public:
    ItemSword(int id, int level) {
        itemID = id;
        maxStackSize = 1;
        maxDamage = 32 << level;
        if (level == 3) maxDamage *= 4;
        itemsList[id] = this;
        damageVsEntity_ = 4 + level * 2;
    }
    int getDamageVsEntity(Entity* entity) const override { return damageVsEntity_; }
    void hitEntity(ItemStack* stack, EntityLiving* entity) override;
private:
    int damageVsEntity_ = 4;
};

class ItemBoat : public Item {
public:
    explicit ItemBoat(int id);

    ItemStack onItemRightClick(ItemStack* stack, World* world, EntityPlayerMP* player) override;
};
