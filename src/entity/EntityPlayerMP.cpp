#include "EntityPlayerMP.h"

#include "EntityItem.h"
#include "EntityMobs.h"
#include "EntityAnimals.h"
#include "../MinecraftServer.h"
#include "../network/packets/AllPackets.h"
#include "../world/World.h"

#include <cstdlib>
#include <cmath>
#include <memory>

namespace {

constexpr int kRespawnInvulnerabilityTicks = 60;
constexpr int kSwingAnimationTicks = 7;

double randomDropVelocity() {
    return (static_cast<double>(std::rand()) / static_cast<double>(RAND_MAX) - 0.5) * 0.2;
}

} // namespace

void EntityPlayerMP::tick() {
    EntityPlayer::tick();

    if (itemInWorldManager) {
        itemInWorldManager->tick();
    }

    if (respawnInvulnerabilityTicks > 0) {
        --respawnInvulnerabilityTicks;
    }
    if (armSwingTicks > 0) {
        --armSwingTicks;
    } else {
        isSwinging = false;
    }
}

void EntityPlayerMP::onDeath() {
    auto dropStack = [this](std::unique_ptr<ItemStack>& stack) {
        if (!stack || stack->stackSize <= 0 || !worldObj) {
            stack.reset();
            return;
        }

        auto entity = std::make_unique<EntityItem>(stack->itemID, stack->stackSize, stack->itemDamage);
        entity->setPosition(posX, posY + 0.5, posZ);
        entity->motionX = randomDropVelocity();
        entity->motionY = 0.2 + static_cast<double>(std::rand()) / static_cast<double>(RAND_MAX) * 0.1;
        entity->motionZ = randomDropVelocity();
        entity->pickupDelay = 40;
        worldObj->spawnEntityInWorld(std::move(entity));
        stack.reset();
    };

    for (auto& stack : inventory.mainInventory) {
        dropStack(stack);
    }
    for (auto& stack : inventory.armorInventory) {
        dropStack(stack);
    }
    for (auto& stack : inventory.craftingInventory) {
        dropStack(stack);
    }

    if (netHandler) {
        netHandler->sendInventory();
    }

    if (mcServer && mcServer->configManager) {
        if (lastDeathMessage_.empty()) {
            lastDeathMessage_ = username + " died";
        }
        mcServer->configManager->broadcastChatMessage("\u00a7e" + lastDeathMessage_);
    }

    EntityLiving::onDeath();
}

void EntityPlayerMP::attackEntityFrom(Entity* attacker, int amount) {
    if (amount <= 0 || isDead || health <= 0) {
        return;
    }

    if (respawnInvulnerabilityTicks > 0) {
        return;
    }

    updateDeathMessage(attacker);

    if (attacker && dynamic_cast<EntityPlayerMP*>(attacker) == nullptr) {
        int difficulty = mcServer ? mcServer->getDifficulty() : 2;
        if (difficulty <= 0) {
            amount = 0;
        } else if (difficulty == 1) {
            amount = amount / 3 + 1;
        } else if (difficulty >= 3) {
            amount = amount * 3 / 2;
        }
    }

    if (amount <= 0) {
        return;
    }

    const int armor = inventory.getTotalArmorValue();
    const int scaledDamage = amount * (25 - armor) + armorDamageCarry;
    const int damageAfterArmor = scaledDamage / 25;
    armorDamageCarry = scaledDamage % 25;
    inventory.damageArmor(amount);
    EntityPlayer::attackEntityFrom(attacker, damageAfterArmor);
    if (netHandler) {
        netHandler->sendPacket(std::make_unique<Packet8UpdateHealth>(health));
    }
}

void EntityPlayerMP::updateDeathMessage(Entity* attacker) {
    if (auto* playerAttacker = dynamic_cast<EntityPlayerMP*>(attacker)) {
        lastDeathMessage_ = username + " was slain by " + playerAttacker->username;
        return;
    }

    if (auto* mobAttacker = dynamic_cast<EntityMob*>(attacker)) {
        const std::string mobName = mobAttacker->getEntityStringId().empty() ? "mob" : mobAttacker->getEntityStringId();
        lastDeathMessage_ = username + " was slain by " + mobName;
        return;
    }

    if (auto* animalAttacker = dynamic_cast<EntityAnimals*>(attacker)) {
        const std::string animalName = animalAttacker->getEntityStringId().empty() ? "animal" : animalAttacker->getEntityStringId();
        lastDeathMessage_ = username + " was slain by " + animalName;
        return;
    }

    if (fallDistance > 3.0f) {
        lastDeathMessage_ = username + " hit the ground too hard";
        return;
    }

    if (worldObj && worldObj->getBlockIdNoChunkLoad(static_cast<int>(std::floor(posX)),
                                                    static_cast<int>(std::floor(boundingBox.minY + 0.001)),
                                                    static_cast<int>(std::floor(posZ))) == 81) {
        lastDeathMessage_ = username + " was pricked to death";
        return;
    }

    if (isInsideMaterial(&Material::water) && air <= 0) {
        lastDeathMessage_ = username + " drowned";
        return;
    }

    if (isInLava()) {
        lastDeathMessage_ = username + " tried to swim in lava";
        return;
    }

    if (fire > 0) {
        lastDeathMessage_ = username + " went up in flames";
        return;
    }

    lastDeathMessage_ = username + " died";
}

void EntityPlayerMP::swingItem() {
    if (isSwinging && armSwingTicks > 0) {
        return;
    }

    isSwinging = true;
    armSwingTicks = kSwingAnimationTicks;

    if (mcServer && mcServer->entityTracker) {
        mcServer->entityTracker->broadcastPacket(
            this, std::make_unique<Packet18ArmAnimation>(entityId, 1));
    }
}

void EntityPlayerMP::resetCombatState() {
    respawnInvulnerabilityTicks = kRespawnInvulnerabilityTicks;
    attackCooldownTicks = 0;
    armSwingTicks = 0;
    isSwinging = false;
    lastDeathMessage_.clear();
}

bool EntityPlayerMP::canAttackNow() const {
    return !isDead && health > 0;
}

void EntityPlayerMP::markAttackPerformed() {
    attackCooldownTicks = 0;
}
