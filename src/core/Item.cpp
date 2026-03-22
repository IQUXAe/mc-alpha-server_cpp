#include "Item.h"
#include "../block/Block.h"
#include "../world/World.h"
#include "../entity/EntityPlayerMP.h"
#include "ItemStack.h"
#include "AxisAlignedBB.h"
#include "Material.h"
#include <iostream>

Item* Item::itemsList[32000] = {nullptr};
Item* Item::shovelSteel = nullptr;
Item* Item::pickaxeSteel = nullptr;
Item* Item::axeSteel = nullptr;
Item* Item::flintAndSteel = nullptr;
Item* Item::appleRed = nullptr;
Item* Item::bow = nullptr;
Item* Item::arrow = nullptr;
Item* Item::coal = nullptr;
Item* Item::diamond = nullptr;
Item* Item::ingotIron = nullptr;
Item* Item::ingotGold = nullptr;
Item* Item::swordSteel = nullptr;
Item* Item::swordWood = nullptr;
Item* Item::shovelWood = nullptr;
Item* Item::pickaxeWood = nullptr;
Item* Item::axeWood = nullptr;
Item* Item::swordStone = nullptr;
Item* Item::shovelStone = nullptr;
Item* Item::pickaxeStone = nullptr;
Item* Item::axeStone = nullptr;
Item* Item::swordDiamond = nullptr;
Item* Item::shovelDiamond = nullptr;
Item* Item::pickaxeDiamond = nullptr;
Item* Item::axeDiamond = nullptr;
Item* Item::stick = nullptr;
Item* Item::bowlEmpty = nullptr;
Item* Item::bowlSoup = nullptr;
Item* Item::swordGold = nullptr;
Item* Item::shovelGold = nullptr;
Item* Item::pickaxeGold = nullptr;
Item* Item::axeGold = nullptr;
Item* Item::silk = nullptr;
Item* Item::feather = nullptr;
Item* Item::gunpowder = nullptr;
Item* Item::hoeWood = nullptr;
Item* Item::hoeStone = nullptr;
Item* Item::hoeSteel = nullptr;
Item* Item::hoeDiamond = nullptr;
Item* Item::hoeGold = nullptr;
Item* Item::seeds = nullptr;
Item* Item::wheat = nullptr;
Item* Item::bread = nullptr;
Item* Item::helmetLeather = nullptr;
Item* Item::plateLeather = nullptr;
Item* Item::legsLeather = nullptr;
Item* Item::bootsLeather = nullptr;
Item* Item::helmetChain = nullptr;
Item* Item::plateChain = nullptr;
Item* Item::legsChain = nullptr;
Item* Item::bootsChain = nullptr;
Item* Item::helmetSteel = nullptr;
Item* Item::plateSteel = nullptr;
Item* Item::legsSteel = nullptr;
Item* Item::bootsSteel = nullptr;
Item* Item::helmetDiamond = nullptr;
Item* Item::plateDiamond = nullptr;
Item* Item::legsDiamond = nullptr;
Item* Item::bootsDiamond = nullptr;
Item* Item::helmetGold = nullptr;
Item* Item::plateGold = nullptr;
Item* Item::legsGold = nullptr;
Item* Item::bootsGold = nullptr;
Item* Item::flint = nullptr;
Item* Item::porkRaw = nullptr;
Item* Item::porkCooked = nullptr;
Item* Item::painting = nullptr;
Item* Item::appleGold = nullptr;
Item* Item::sign = nullptr;
Item* Item::doorWood = nullptr;
Item* Item::bucketEmpty = nullptr;
Item* Item::bucketWater = nullptr;
Item* Item::bucketLava = nullptr;
Item* Item::minecartEmpty = nullptr;
Item* Item::saddle = nullptr;
Item* Item::doorSteel = nullptr;
Item* Item::redstone = nullptr;
Item* Item::snowball = nullptr;
Item* Item::boat = nullptr;
Item* Item::leather = nullptr;
Item* Item::bucketMilk = nullptr;
Item* Item::brick = nullptr;
Item* Item::clay = nullptr;
Item* Item::reed = nullptr;
Item* Item::paper = nullptr;
Item* Item::book = nullptr;
Item* Item::slimeBall = nullptr;
Item* Item::minecartCrate = nullptr;
Item* Item::minecartPowered = nullptr;
Item* Item::egg = nullptr;
Item* Item::compass = nullptr;
Item* Item::fishingRod = nullptr;
Item* Item::pocketSundial = nullptr;
Item* Item::lightstoneDust = nullptr;
Item* Item::fishRaw = nullptr;
Item* Item::fishCooked = nullptr;

Item::Item(int id) : itemID(id + 256) {
    if (id < 0 || id >= 32000 - 256) throw std::runtime_error("Invalid item ID");
    if (itemsList[itemID] != nullptr) throw std::runtime_error("Item slot " + std::to_string(itemID) + " occupied");
    itemsList[itemID] = this;
}

void Item::initItems() {
    shovelSteel = (new ItemSpade(256 + 0, 2));
    pickaxeSteel = (new ItemPickaxe(256 + 1, 2));
    axeSteel = (new ItemAxe(256 + 2, 2));
    flintAndSteel = (new Item(3))->setMaxStackSize(1)->setMaxDamage(64);
    appleRed = (new Item(4));
    bow = (new Item(5))->setMaxStackSize(1)->setMaxDamage(384);
    arrow = (new Item(6));
    coal = (new Item(7));
    diamond = (new Item(8));
    ingotIron = (new Item(9));
    ingotGold = (new Item(10));
    swordSteel = (new ItemSword(256 + 11, 2));
    swordWood = (new ItemSword(256 + 12, 0));
    shovelWood = (new ItemSpade(256 + 13, 0));
    pickaxeWood = (new ItemPickaxe(256 + 14, 0));
    axeWood = (new ItemAxe(256 + 15, 0));
    swordStone = (new ItemSword(256 + 16, 1));
    shovelStone = (new ItemSpade(256 + 17, 1));
    pickaxeStone = (new ItemPickaxe(256 + 18, 1));
    axeStone = (new ItemAxe(256 + 19, 1));
    swordDiamond = (new ItemSword(256 + 20, 3));
    shovelDiamond = (new ItemSpade(256 + 21, 3));
    pickaxeDiamond = (new ItemPickaxe(256 + 22, 3));
    axeDiamond = (new ItemAxe(256 + 23, 3));
    stick = (new Item(24));
    bowlEmpty = (new Item(25));
    bowlSoup = (new Item(26))->setMaxStackSize(1);
    swordGold = (new ItemSword(256 + 27, 0));
    shovelGold = (new ItemSpade(256 + 28, 0));
    pickaxeGold = (new ItemPickaxe(256 + 29, 0));
    axeGold = (new ItemAxe(256 + 30, 0));
    silk = (new Item(31));
    feather = (new Item(32));
    gunpowder = (new Item(33));
    hoeWood = (new Item(34))->setMaxStackSize(1)->setMaxDamage(59);
    hoeStone = (new Item(35))->setMaxStackSize(1)->setMaxDamage(131);
    hoeSteel = (new Item(36))->setMaxStackSize(1)->setMaxDamage(250);
    hoeDiamond = (new Item(37))->setMaxStackSize(1)->setMaxDamage(1561);
    hoeGold = (new Item(38))->setMaxStackSize(1)->setMaxDamage(32);
    seeds = (new Item(39));
    wheat = (new Item(40));
    bread = (new Item(41));
    helmetLeather = (new Item(42))->setMaxStackSize(1)->setMaxDamage(33);
    plateLeather = (new Item(43))->setMaxStackSize(1)->setMaxDamage(48);
    legsLeather = (new Item(44))->setMaxStackSize(1)->setMaxDamage(45);
    bootsLeather = (new Item(45))->setMaxStackSize(1)->setMaxDamage(39);
    helmetChain = (new Item(46))->setMaxStackSize(1)->setMaxDamage(66);
    plateChain = (new Item(47))->setMaxStackSize(1)->setMaxDamage(96);
    legsChain = (new Item(48))->setMaxStackSize(1)->setMaxDamage(90);
    bootsChain = (new Item(49))->setMaxStackSize(1)->setMaxDamage(78);
    helmetSteel = (new Item(50))->setMaxStackSize(1)->setMaxDamage(132);
    plateSteel = (new Item(51))->setMaxStackSize(1)->setMaxDamage(192);
    legsSteel = (new Item(52))->setMaxStackSize(1)->setMaxDamage(180);
    bootsSteel = (new Item(53))->setMaxStackSize(1)->setMaxDamage(156);
    helmetDiamond = (new Item(54))->setMaxStackSize(1)->setMaxDamage(264);
    plateDiamond = (new Item(55))->setMaxStackSize(1)->setMaxDamage(384);
    legsDiamond = (new Item(56))->setMaxStackSize(1)->setMaxDamage(360);
    bootsDiamond = (new Item(57))->setMaxStackSize(1)->setMaxDamage(312);
    helmetGold = (new Item(58))->setMaxStackSize(1)->setMaxDamage(66);
    plateGold = (new Item(59))->setMaxStackSize(1)->setMaxDamage(96);
    legsGold = (new Item(60))->setMaxStackSize(1)->setMaxDamage(90);
    bootsGold = (new Item(61))->setMaxStackSize(1)->setMaxDamage(78);
    flint = (new Item(62));
    porkRaw = (new Item(63));
    porkCooked = (new Item(64));
    painting = (new Item(65));
    appleGold = (new Item(66));
    sign = (new Item(67))->setMaxStackSize(1);
    doorWood = (new Item(68))->setMaxStackSize(1);
    bucketEmpty = (new Item(69))->setMaxStackSize(1);
    bucketWater = (new Item(70))->setMaxStackSize(1);
    bucketLava = (new Item(71))->setMaxStackSize(1);
    minecartEmpty = (new Item(72))->setMaxStackSize(1);
    saddle = (new Item(73))->setMaxStackSize(1);
    doorSteel = (new Item(74))->setMaxStackSize(1);
    redstone = (new Item(75));
    snowball = (new Item(76))->setMaxStackSize(16);
    boat = (new Item(77))->setMaxStackSize(1);
    leather = (new Item(78));
    bucketMilk = (new Item(79))->setMaxStackSize(1);
    brick = (new Item(80));
    clay = (new Item(81));
    reed = (new Item(82));
    paper = (new Item(83));
    book = (new Item(84));
    slimeBall = (new Item(85));
    minecartCrate = (new Item(86))->setMaxStackSize(1);
    minecartPowered = (new Item(87))->setMaxStackSize(1);
    egg = (new Item(88))->setMaxStackSize(16);
    compass = (new Item(89));
    fishingRod = (new Item(90))->setMaxStackSize(1)->setMaxDamage(64);
    pocketSundial = (new Item(91));
    lightstoneDust = (new Item(92));
    fishRaw = (new Item(93));
    fishCooked = (new Item(94));

    // Register ItemBlock for every block (IDs 1-255 map to item IDs 1-255)
    // In Java: Item.itemsList[blockID] = new ItemBlock(blockID - 256, blockID)
    // In our system: Item(id) sets itemID = id+256, so ItemBlock(blockId-256, blockId)
    for (int i = 1; i < 256; i++) {
        if (Block::blocksList[i] != nullptr && itemsList[i] == nullptr) {
            new ItemBlock(i);
        }
    }

    std::cout << "[INFO] Registered all standard items." << std::endl;
}

Item* Item::setMaxStackSize(int size) {
    maxStackSize = size;
    return this;
}

Item* Item::setMaxDamage(int damage) {
    maxDamage = damage;
    return this;
}

bool Item::onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) {
    return false;
}

bool ItemPickaxe::canHarvestBlock(int blockId) const {
    if (blockId == 49) return toolLevel == 3;
    if (blockId == 56 || blockId == 57 || blockId == 14 || blockId == 41) return toolLevel >= 2;
    if (blockId == 15 || blockId == 42 || blockId == 73 || blockId == 74) return toolLevel >= 1;
    Block* b = (blockId > 0 && blockId < 256) ? Block::blocksList[blockId] : nullptr;
    return b && (b->blockMaterial == &Material::rock || b->blockMaterial == &Material::iron);
}

// ItemBlock registers directly at blockID slot (not offset+256)
ItemBlock::ItemBlock(int blockId) : blockID(blockId) {
    itemID = blockId;
    itemsList[blockId] = this;
}

bool ItemBlock::onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) {
    // Special case: placing on snow replaces it
    int existingId = world->getBlockId(x, y, z);
    if (existingId != 79 /* snow */) { // not snow layer
        // Offset target position by face direction (mirrors Java ItemBlock)
        switch (side) {
            case 0: y--; break;
            case 1: y++; break;
            case 2: z--; break;
            case 3: z++; break;
            case 4: x--; break;
            case 5: x++; break;
        }
    }

    if (stack->stackSize == 0) return false;
    if (y < 0 || y >= 128) return false;

    // Target position must be air or replaceable
    int targetId = world->getBlockId(x, y, z);
    Block* targetBlock = targetId > 0 ? Block::blocksList[targetId] : nullptr;
    if (targetId != 0 && !(targetBlock && targetBlock->isReplaceable())) return false;

    Block* block = Block::blocksList[blockID];
    if (!block) return false;

    // Check block can stay here
    if (!block->canBlockStay(world, x, y, z)) return false;

    // Check player bbox doesn't intersect placed block
    auto bb = block->getCollisionBoundingBoxFromPool(world, x, y, z);
    if (bb) {
        double px = player->posX, py = player->posY, pz = player->posZ;
        double hw = player->width / 2.0;
        AxisAlignedBB playerBB(px - hw, py, pz - hw, px + hw, py + player->height, pz + hw);
        if (playerBB.intersectsWith(*bb)) return false;
    }

    if (world->setBlockWithNotify(x, y, z, blockID)) {
        block->onBlockPlaced(world, x, y, z, side);
        stack->stackSize--;
        return true;
    }
    return false;
}
