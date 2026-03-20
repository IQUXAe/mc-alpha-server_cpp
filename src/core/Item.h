#pragma once

#include <string>
#include <vector>

class Block;

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

    Item(int id);
    virtual ~Item() = default;

    Item* setMaxStackSize(int size);
    Item* setMaxDamage(int damage);

    static void initItems();
};
