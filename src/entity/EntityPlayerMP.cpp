#include "EntityPlayerMP.h"

#include "EntityItem.h"
#include "EntityMobs.h"
#include "EntityAnimals.h"
#include "../MinecraftServer.h"
#include "../core/RustBridge.h"
#include "../network/RustPackets.h"
#include "../world/World.h"

#include <cmath>
#include <memory>

namespace {

constexpr int kRespawnInvulnerabilityTicks = 60;
constexpr int kSwingAnimationTicks = 7;

double randomDropVelocity() {
    return (RustBridge::rngNextDouble() - 0.5) * 0.2;
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
        entity->motionY = 0.2 + RustBridge::rngNextDouble() * 0.1;
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

    const bool attackerIsPlayer = (attacker && dynamic_cast<EntityPlayerMP*>(attacker) != nullptr);
    const int difficulty = mcServer ? mcServer->getDifficulty() : 2;
    const int armor = inventory.getTotalArmorValue();

    const auto combatResult = RustBridge::calculateCombatDamage(
        amount,
        attackerIsPlayer,
        difficulty,
        armor,
        armorDamageCarry
    );

    if (combatResult.scaled_damage <= 0) {
        return;
    }

    armorDamageCarry = combatResult.new_armor_damage_carry;
    inventory.damageArmor(combatResult.scaled_damage);
    EntityPlayer::attackEntityFrom(attacker, combatResult.damage_after_armor);
    if (netHandler) {
        netHandler->sendPacket(RustPackets::updateHealth(health));
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
            this, RustPackets::armAnimation(entityId, 1));
    }
}

void EntityPlayerMP::resetCombatState() {
    respawnInvulnerabilityTicks = kRespawnInvulnerabilityTicks;
    armSwingTicks = 0;
    isSwinging = false;
    lastDeathMessage_.clear();
}

bool EntityPlayerMP::canAttackNow() const {
    return !isDead && health > 0;
}

void EntityPlayerMP::markAttackPerformed() {
}
