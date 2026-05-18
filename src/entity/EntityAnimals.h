#pragma once

#include "EntityCreature.h"
#include "EntityItem.h"
#include "EntityPlayerMP.h"
#include "../core/Item.h"
#include "../block/Block.h"

class EntityAnimals : public EntityCreature {
public:
    explicit EntityAnimals(World* world) : EntityCreature(world) {
        worldObj = world;
        stepHeight = 1.0f;
        maxHurtResistantTime = 12;
    }

    virtual int getMobTypeId() const override = 0;
    virtual std::string getEntityStringId() const override = 0;

    // Java EntityAnimals.func_159_a — prefer grass blocks
    float getBlockPathWeight(int x, int y, int z) const override {
        if (!worldObj) return 0.0f;
        return worldObj->getBlockId(x, y - 1, z) == Block::grass->blockID
            ? 10.0f
            : static_cast<float>(worldObj->getBlockLightValue(x, y, z)) - 0.5f;
    }

    void tick() override {
        EntityCreature::tick();
        if (!worldObj || !isEntityAlive()) return;
        tickExtra();
        // Push nearby entities apart
        std::vector<Entity*> nearby;
        worldObj->getEntitiesWithinAABBExcludingEntity(this, boundingBox.expand(0.2, 0.0, 0.2), nearby);
        for (Entity* e : nearby) {
            if (e && e->canBePushed()) e->applyEntityCollision(this);
        }
    }

    virtual bool getCanSpawnHere() const;
    virtual void writeToNBT(NBTCompound& nbt) const override {
        nbt.setString("id", getEntityStringId());
        nbt.setDouble("PosX", posX);
        nbt.setDouble("PosY", posY);
        nbt.setDouble("PosZ", posZ);
        nbt.setDouble("MotionX", motionX);
        nbt.setDouble("MotionY", motionY);
        nbt.setDouble("MotionZ", motionZ);
        nbt.setFloat("RotationYaw", rotationYaw);
        nbt.setFloat("RotationPitch", rotationPitch);
        nbt.setShort("Health", health);
        nbt.setShort("MaxHealth", maxHealth);
    }

    virtual void readFromNBT(const NBTCompound& nbt) override {
        setPositionAndRotation(
            nbt.getDouble("PosX"), nbt.getDouble("PosY"), nbt.getDouble("PosZ"),
            nbt.getFloat("RotationYaw"), nbt.getFloat("RotationPitch"));
        motionX = nbt.getDouble("MotionX");
        motionY = nbt.getDouble("MotionY");
        motionZ = nbt.getDouble("MotionZ");
        const int16_t savedMaxHealth = nbt.getShort("MaxHealth");
        if (savedMaxHealth > 0) maxHealth = savedMaxHealth;
        health = nbt.getShort("Health");
        if (health > maxHealth) health = maxHealth;
        if (health <= 0) health = maxHealth;
    }

protected:
    virtual int getDropItemId() const { return 0; }
    virtual int getDropCount() const { return 0; }
    virtual void tickExtra() {}

    void onDeath() override;
};

inline bool EntityAnimals::getCanSpawnHere() const {
    if (!worldObj) return false;
    const int x = MathHelper::floor_double(posX);
    const int y = MathHelper::floor_double(boundingBox.minY);
    const int z = MathHelper::floor_double(posZ);
    if (worldObj->getBlockId(x, y - 1, z) != Block::grass->blockID) return false;
    if (worldObj->getBlockLightValue(x, y, z) <= 8) return false;
    std::vector<AxisAlignedBB> collisions;
    worldObj->getCollidingBoundingBoxes(const_cast<EntityAnimals*>(this), boundingBox, collisions);
    return collisions.empty() && !isTouchingLiquid();
}

inline void EntityAnimals::onDeath() {
    const int dropId = getDropItemId();
    const int dropCount = getDropCount();
    if (worldObj && dropId > 0 && dropCount > 0) {
        for (int i = 0; i < dropCount; ++i) {
            auto item = std::make_unique<EntityItem>(dropId, 1, 0);
            item->setPosition(posX, posY, posZ);
            item->pickupDelay = 10;
            worldObj->spawnEntityInWorld(std::move(item));
        }
    }
    EntityLiving::onDeath();
}

// ==================== EntityPig ====================

class EntityPig : public EntityAnimals {
public:
    bool saddled = false;

    explicit EntityPig(World* world) : EntityAnimals(world) {
        width = 0.9f;
        height = 0.9f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 90; }
    std::string getEntityStringId() const override { return "Pig"; }

    void writeToNBT(NBTCompound& nbt) const override {
        EntityAnimals::writeToNBT(nbt);
        nbt.setByte("Saddle", saddled ? 1 : 0);
    }
    void readFromNBT(const NBTCompound& nbt) override {
        EntityAnimals::readFromNBT(nbt);
        saddled = nbt.getByte("Saddle") != 0;
    }

protected:
    int getDropItemId() const override { return Item::porkRaw ? Item::porkRaw->itemID : 0; }
    int getDropCount() const override { return 1 + (std::rand() % 3); }
};

// ==================== EntitySheep ====================

class EntitySheep : public EntityAnimals {
public:
    bool sheared = false;

    explicit EntitySheep(World* world) : EntityAnimals(world) {
        width = 0.9f;
        height = 1.3f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 91; }
    std::string getEntityStringId() const override { return "Sheep"; }

    void writeToNBT(NBTCompound& nbt) const override {
        EntityAnimals::writeToNBT(nbt);
        nbt.setByte("Sheared", sheared ? 1 : 0);
    }
    void readFromNBT(const NBTCompound& nbt) override {
        EntityAnimals::readFromNBT(nbt);
        sheared = nbt.getByte("Sheared") != 0;
    }

    void attackEntityFrom(Entity* attacker, int amount) override {
        if (!sheared && worldObj && attacker && dynamic_cast<EntityLiving*>(attacker)) {
            sheared = true;
            const int woolCount = 1 + (std::rand() % 3);
            for (int i = 0; i < woolCount; ++i) {
                auto wool = std::make_unique<EntityItem>(35, 1, 0);
                wool->setPosition(posX, posY + static_cast<double>(height) * 0.75, posZ);
                wool->motionY += static_cast<double>(std::rand()) / RAND_MAX * 0.05;
                wool->motionX += (static_cast<double>(std::rand()) / RAND_MAX
                    - static_cast<double>(std::rand()) / RAND_MAX) * 0.1;
                wool->motionZ += (static_cast<double>(std::rand()) / RAND_MAX
                    - static_cast<double>(std::rand()) / RAND_MAX) * 0.1;
                worldObj->spawnEntityInWorld(std::move(wool));
            }
        }
        EntityAnimals::attackEntityFrom(attacker, amount);
    }

protected:
    int getDropItemId() const override { return sheared ? 0 : 35; }
    int getDropCount() const override { return sheared ? 0 : (1 + (std::rand() % 3)); }
};

// ==================== EntityCow ====================

class EntityCow : public EntityAnimals {
public:
    explicit EntityCow(World* world) : EntityAnimals(world) {
        width = 0.9f;
        height = 1.3f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 92; }
    std::string getEntityStringId() const override { return "Cow"; }

    bool interact(EntityPlayerMP* player) override {
        // Java: if player holds an empty bucket, fill with milk
        if (!player) return false;
        ItemStack* held = player->inventory.getCurrentItem();
        if (held && held->itemID == 325) { // bucket
            const int slot = player->inventory.currentItem;
            // Replace bucket with milk bucket (item ID 335 in Alpha 1.2.6)
            // In our C++ code, use Item::bucketMilk->itemID (or 335 if not defined)
            player->inventory.mainInventory[slot] = std::make_unique<ItemStack>(335, 1, 0);
            if (player->netHandler) player->netHandler->sendInventory();
            return true;
        }
        return false;
    }

protected:
    int getDropItemId() const override { return Item::leather ? Item::leather->itemID : 0; }
    int getDropCount() const override { return 1 + (std::rand() % 3); }
};

// ==================== EntityChicken ====================

class EntityChicken : public EntityAnimals {
public:
    int eggLayTime = 6000 + (std::rand() % 6000);

    explicit EntityChicken(World* world) : EntityAnimals(world) {
        width = 0.3f;
        height = 0.4f;
        yOffset = 0.0f;
        setPosition(posX, posY, posZ);
    }

    int getMobTypeId() const override { return 93; }
    std::string getEntityStringId() const override { return "Chicken"; }

    void writeToNBT(NBTCompound& nbt) const override {
        EntityAnimals::writeToNBT(nbt);
        nbt.setInt("EggLayTime", eggLayTime);
    }
    void readFromNBT(const NBTCompound& nbt) override {
        EntityAnimals::readFromNBT(nbt);
        eggLayTime = nbt.getInt("EggLayTime");
        if (eggLayTime <= 0) eggLayTime = 6000 + (std::rand() % 6000);
    }

    void onFall(float distance) override {}

protected:
    int getDropItemId() const override { return Item::feather ? Item::feather->itemID : 0; }
    int getDropCount() const override { return 1 + (std::rand() % 3); }
    void tickExtra() override {
        // Chickens fall slower
        if (motionY < 0.0 && !onGround) {
            motionY *= 0.6;
        }
        if (--eggLayTime <= 0) {
            if (worldObj && Item::egg) {
                auto item = std::make_unique<EntityItem>(Item::egg->itemID, 1, 0);
                item->setPosition(posX, posY, posZ);
                item->pickupDelay = 10;
                worldObj->spawnEntityInWorld(std::move(item));
            }
            eggLayTime = 6000 + (std::rand() % 6000);
        }
    }
};
