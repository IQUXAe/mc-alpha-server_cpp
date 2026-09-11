#include "Item.h"
#include "../block/Block.h"
#include "../block/BlockSign.h"
#include "../world/World.h"
#include "../world/TileEntitySign.h"
#include "../entity/EntityPlayerMP.h"
#include "../entity/EntityBoat.h"
#include "../entity/EntityItem.h"
#include "ItemStack.h"
#include "AxisAlignedBB.h"
#include "Material.h"
#include "MathHelper.h"
#include "RustBridge.h"
#include <iostream>
#include <random>
#include <numbers>

namespace {

// Verb-driver trampolines for item_verbs.rs (single definition here).
// The guard swaps in the calling world/player per call; RNG draws stay on
// World::rand to preserve the historical sequence.
struct VerbContext {
    World* world = nullptr;
    EntityPlayerMP* player = nullptr;
};

thread_local VerbContext gVerbCtx;

extern "C" int32_t verbNextInt(int32_t bound) {
    std::uniform_int_distribution<int> dist(0, bound - 1);
    return dist(gVerbCtx.world->rand);
}

extern "C" double verbNextF64() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gVerbCtx.world->rand);
}

extern "C" uint8_t verbGetBlockId(int32_t x, int32_t y, int32_t z) {
    return gVerbCtx.world->getBlockId(x, y, z);
}

extern "C" bool verbSetNotify(int32_t x, int32_t y, int32_t z, uint8_t id) {
    return gVerbCtx.world->setBlockWithNotify(x, y, z, id);
}

extern "C" bool verbSetMetaNotify(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t meta) {
    return gVerbCtx.world->setBlockAndMetadataWithNotify(x, y, z, id, meta);
}

extern "C" bool verbSetQuiet(int32_t x, int32_t y, int32_t z, uint8_t id) {
    return gVerbCtx.world->setBlockWithNotifyNoClientUpdate(x, y, z, id);
}

extern "C" void verbSetMeta(int32_t x, int32_t y, int32_t z, uint8_t meta) {
    gVerbCtx.world->setBlockMetadata(x, y, z, meta);
}

extern "C" bool verbDoesAttach(int32_t x, int32_t y, int32_t z) {
    return gVerbCtx.world->doesBlockAllowAttachment(x, y, z);
}

extern "C" bool verbMaterialBurning(int32_t x, int32_t y, int32_t z) {
    return gVerbCtx.world->getBlockMaterial(x, y, z)->getBurning();
}

extern "C" bool verbMaterialSolid(int32_t x, int32_t y, int32_t z) {
    return gVerbCtx.world->getBlockMaterial(x, y, z)->isSolid();
}

extern "C" bool verbCollidableBox(int32_t x, int32_t y, int32_t z) {
    const int id = gVerbCtx.world->getBlockId(x, y, z);
    Block* b = (id > 0 && id < 256) ? Block::blocksList[id] : nullptr;
    return b && b->isCollidable()
        && b->getCollisionBoundingBoxFromPool(gVerbCtx.world, x, y, z).has_value();
}

extern "C" bool verbBlockCanStay(uint8_t id, int32_t x, int32_t y, int32_t z) {
    Block* b = Block::blocksList[id];
    return b && b->canBlockStay(gVerbCtx.world, x, y, z);
}

extern "C" bool verbPlacementClear(uint8_t id, int32_t x, int32_t y, int32_t z) {
    Block* b = Block::blocksList[id];
    if (!b) return false;
    auto bb = b->getCollisionBoundingBoxFromPool(gVerbCtx.world, x, y, z);
    return !bb || gVerbCtx.world->isPlacementVolumeClear(*bb);
}

extern "C" void verbBlockPlaced(uint8_t id, int32_t x, int32_t y, int32_t z, int32_t side) {
    Block* b = Block::blocksList[id];
    if (b) b->onBlockPlaced(gVerbCtx.world, x, y, z, side);
}

extern "C" bool verbHaveBlock(uint8_t id) {
    return Block::blocksList[id] != nullptr;
}

extern "C" void verbSpawnItem(int32_t itemId, int32_t count, int32_t damage,
                              double fx, double fy, double fz, double mx, double my, double mz) {
    auto entity = std::make_unique<EntityItem>(itemId, count, damage);
    entity->setPosition(fx, fy, fz);
    entity->worldObj = gVerbCtx.world;
    entity->motionX = mx;
    entity->motionY = my;
    entity->motionZ = mz;
    gVerbCtx.world->spawnEntityInWorld(std::move(entity));
}

extern "C" void verbSendTePacket(int32_t x, int32_t y, int32_t z) {
    if (!gVerbCtx.world || !gVerbCtx.player || !gVerbCtx.player->netHandler) {
        return;
    }
    TileEntity* te = gVerbCtx.world->getTileEntity(x, y, z);
    if (te) {
        gVerbCtx.player->netHandler->sendTileEntityPacket(te);
    }
}

extern "C" bool verbRayTrace(double sx, double sy, double sz, double ex, double ey, double ez,
                             int32_t* outX, int32_t* outY, int32_t* outZ) {
    if (!gVerbCtx.world || !outX || !outY || !outZ) {
        return false;
    }
    auto hit = gVerbCtx.world->rayTraceBlocks(Vec3D(sx, sy, sz), Vec3D(ex, ey, ez), true);
    if (!hit) {
        return false;
    }
    *outX = hit->blockX;
    *outY = hit->blockY;
    *outZ = hit->blockZ;
    return true;
}

const RustBridge::ItemUseWorld& verbWorld() {
    static const RustBridge::ItemUseWorld table = {
        &verbNextInt,
        &verbNextF64,
        &verbGetBlockId,
        &verbSetNotify,
        &verbSetMetaNotify,
        &verbSetQuiet,
        &verbSetMeta,
        &verbDoesAttach,
        &verbMaterialBurning,
        &verbMaterialSolid,
        &verbCollidableBox,
        &verbBlockCanStay,
        &verbPlacementClear,
        &verbBlockPlaced,
        &verbHaveBlock,
        &verbSpawnItem,
        &verbSendTePacket,
        &verbRayTrace,
    };
    return table;
}

struct VerbGuard {
    VerbGuard(World* world, EntityPlayerMP* player) {
        gVerbCtx.world = world;
        gVerbCtx.player = player;
    }
    ~VerbGuard() { gVerbCtx = VerbContext{}; }
};

} // namespace

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

namespace {

class ItemHoe : public Item {
public:
    ItemHoe(int id, int durability) : Item(id) {
        maxStackSize = 1;
        maxDamage = durability;
    }

    bool onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) override {
        if (!stack || !player || !world) {
            return false;
        }
        VerbGuard guard(world, player);
        if (!RustBridge::itemHoeUse(&verbWorld(), Item::seeds ? Item::seeds->itemID : 0, x, y, z)) {
            return false;
        }
        stack->damageItem(1);
        if (stack->stackSize <= 0) {
            player->destroyCurrentEquippedItem();
        }
        return true;
    }
};

class ItemSeeds : public Item {
public:
    explicit ItemSeeds(int id) : Item(id) {}

    bool onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) override {
        if (!stack || !world || side != 1) {
            return false;
        }
        VerbGuard guard(world, player);
        if (!RustBridge::itemSeedsUse(&verbWorld(), x, y, z, side)) {
            return false;
        }
        if (stack->stackSize > 0) {
            --stack->stackSize;
        }
        return true;
    }
};

} // namespace

Item::Item(int id) : itemID(id + 256) {
    if (id < 0 || id >= 32000 - 256) throw std::runtime_error("Invalid item ID");
    if (itemsList[itemID] != nullptr) throw std::runtime_error("Item slot " + std::to_string(itemID) + " occupied");
    itemsList[itemID] = this;
}

void Item::initItems() {
    shovelSteel = (new ItemSpade(256 + 0, 2));
    pickaxeSteel = (new ItemPickaxe(256 + 1, 2));
    axeSteel = (new ItemAxe(256 + 2, 2));
    class ItemFlintAndSteel : public Item {
    public:
        using Item::Item;
        bool onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) override {
            if (!stack || !world) return false;
            VerbGuard guard(world, player);
            RustBridge::FlintOut out{};
            if (!RustBridge::itemFlintUse(&verbWorld(), stack->itemDamage, maxDamage, x, y, z, side, &out)) {
                return false;
            }
            // Damage the item
            stack->itemDamage = out.new_damage;
            if (out.broke) {
                stack->stackSize = 0;
            }
            return true;
        }
    };
    flintAndSteel = (new ItemFlintAndSteel(3))->setMaxStackSize(1)->setMaxDamage(64);
    appleRed = (new ItemFood(4, 4));
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
    bowlSoup = (new ItemSoup(26, 10));
    swordGold = (new ItemSword(256 + 27, 0));
    shovelGold = (new ItemSpade(256 + 28, 0));
    pickaxeGold = (new ItemPickaxe(256 + 29, 0));
    axeGold = (new ItemAxe(256 + 30, 0));
    silk = (new Item(31));
    feather = (new Item(32));
    gunpowder = (new Item(33));
    hoeWood = (new ItemHoe(34, 59));
    hoeStone = (new ItemHoe(35, 131));
    hoeSteel = (new ItemHoe(36, 250));
    hoeDiamond = (new ItemHoe(37, 1561));
    hoeGold = (new ItemHoe(38, 64));
    seeds = (new ItemSeeds(39));
    wheat = (new Item(40));
    bread = (new ItemFood(41, 5));
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
    porkRaw = (new ItemFood(63, 3));
    porkCooked = (new ItemFood(64, 8));
    painting = (new Item(65));
    appleGold = (new ItemFood(66, 42));
    sign = new ItemSign(323);  // Item ID 323 = 256 + 67
    doorWood = (new Item(68))->setMaxStackSize(1);
    bucketEmpty = (new Item(69))->setMaxStackSize(1);
    bucketWater = (new Item(70))->setMaxStackSize(1);
    bucketLava = (new Item(71))->setMaxStackSize(1);
    minecartEmpty = (new Item(72))->setMaxStackSize(1);
    saddle = (new Item(73))->setMaxStackSize(1);
    doorSteel = (new Item(74))->setMaxStackSize(1);
    redstone = (new Item(75));
    snowball = (new Item(76))->setMaxStackSize(16);
    boat = (new ItemBoat(77));
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
    fishRaw = (new ItemFood(93, 2));
    fishCooked = (new ItemFood(94, 5));

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

ItemStack Item::onItemRightClick(ItemStack* stack, World* world, EntityPlayerMP* player) {
    return stack ? stack->copy() : ItemStack();
}

ItemFood::ItemFood(int id, int healAmount) : Item(id), healAmount_(healAmount) {
    maxStackSize = 1;
}

ItemStack ItemFood::onItemRightClick(ItemStack* stack, World* world, EntityPlayerMP* player) {
    if (!stack || !player) {
        return stack ? stack->copy() : ItemStack();
    }

    ItemStack result = stack->copy();
    const RustBridge::FoodBite bite = RustBridge::itemFoodBite(result.stackSize, healAmount_);
    result.stackSize = bite.new_count;
    if (bite.heal > 0) {
        player->heal(bite.heal);
    }
    return result;
}

ItemStack ItemSoup::onItemRightClick(ItemStack* stack, World* world, EntityPlayerMP* player) {
    ItemFood::onItemRightClick(stack, world, player);
    return Item::bowlEmpty ? ItemStack(Item::bowlEmpty) : ItemStack();
}

void ItemTool::hitEntity(ItemStack* stack, EntityLiving* entity) {
    if (stack) {
        stack->damageItem(2);
    }
}

void ItemSword::hitEntity(ItemStack* stack, EntityLiving* entity) {
    if (stack) {
        stack->damageItem(1);
    }
}

bool ItemPickaxe::canHarvestBlock(int blockId) const {
    if (blockId == 49) return toolLevel == 3;
    if (blockId == 56 || blockId == 57 || blockId == 14 || blockId == 41) return toolLevel >= 2;
    if (blockId == 15 || blockId == 42 || blockId == 73 || blockId == 74) return toolLevel >= 1;
    Block* b = (blockId > 0 && blockId < 256) ? Block::blocksList[blockId] : nullptr;
    return b && (b->blockMaterial == &Material::rock || b->blockMaterial == &Material::iron);
}

bool ItemPickaxe::isEffectiveAgainst(Block* block) const {
    return block->blockMaterial == &Material::rock
        || block->blockMaterial == &Material::iron;
}

bool ItemSpade::isEffectiveAgainst(Block* block) const {
    return block->blockMaterial == &Material::ground
        || block->blockMaterial == &Material::sand
        || block->blockMaterial == &Material::snow
        || block->blockMaterial == &Material::builtSnow;
}

bool ItemAxe::isEffectiveAgainst(Block* block) const {
    return block->blockMaterial == &Material::wood;
}

bool ItemSign::onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) {
    if (!stack || !player || !world) {
        return false;
    }
    VerbGuard guard(world, player);
    if (!RustBridge::itemSignUse(&verbWorld(), x, y, z, side, player->rotationYaw)) {
        return false;
    }
    // Double-check stack is still valid before consuming (race condition protection)
    if (stack->stackSize > 0) {
        stack->stackSize--;
    }
    return true;
}

float ItemTool::getStrVsBlock(int blockId) const {
    for (int id : effectiveBlockIDs)
        if (id == blockId) return digSpeed;
    if (blockId > 0 && blockId < 256) {
        Block* b = Block::blocksList[blockId];
        if (b && isEffectiveAgainst(b)) return digSpeed;
    }
    return 1.0f;
}

// ItemBlock registers directly at blockID slot (not offset+256)
ItemBlock::ItemBlock(int blockId) : blockID(blockId) {
    itemID = blockId;
    itemsList[blockId] = this;
}

bool ItemBlock::onItemUse(ItemStack* stack, EntityPlayerMP* player, World* world, int x, int y, int z, int side) {
    if (!stack || !player || !world) {
        return false;
    }
    VerbGuard guard(world, player);
    if (!RustBridge::itemBlockUse(&verbWorld(), blockID, stack->stackSize, x, y, z, side, player->rotationYaw)) {
        return false;
    }
    // Double-check stack is still valid before consuming (race condition protection)
    if (stack->stackSize > 0) {
        stack->stackSize--;
    }
    return true;
}

ItemBoat::ItemBoat(int id) : Item(id) {
    maxStackSize = 1;
}

ItemStack ItemBoat::onItemRightClick(ItemStack* stack, World* world, EntityPlayerMP* player) {
    if (!stack || !world || !player) {
        return stack ? stack->copy() : ItemStack();
    }

    RustBridge::BoatThrow aim{};
    VerbGuard guard(world, player);
    if (!RustBridge::itemBoatAim(player->prevRotationYaw, player->rotationYaw,
                                 player->prevRotationPitch, player->rotationPitch,
                                 player->prevPosX, player->posX,
                                 player->prevPosY, player->posY,
                                 player->prevPosZ, player->posZ,
                                 static_cast<double>(player->yOffset), &aim)) {
        return stack->copy();
    }

    int32_t hitX = 0, hitY = 0, hitZ = 0;
    if (!RustBridge::itemBoatThrow(&verbWorld(), aim.sx, aim.sy, aim.sz,
                                   aim.ex, aim.ey, aim.ez, &hitX, &hitY, &hitZ)) {
        return stack->copy();
    }

    auto boatEntity = std::make_unique<EntityBoat>(world,
        static_cast<double>(hitX) + 0.5,
        static_cast<double>(hitY) + 1.5,
        static_cast<double>(hitZ) + 0.5);
    world->spawnEntityInWorld(std::move(boatEntity));

    ItemStack result = stack->copy();
    if (result.stackSize > 0) {
        --result.stackSize;
    }
    return result;
}
