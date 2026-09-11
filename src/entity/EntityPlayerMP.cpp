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
        const RustBridge::DropVelocity vel = RustBridge::playerDropVelocity(
            RustBridge::rngNextDouble(), RustBridge::rngNextDouble(), RustBridge::rngNextDouble());
        entity->motionX = vel.mx;
        entity->motionY = vel.my;
        entity->motionZ = vel.mz;
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
    uint8_t attackerKind = 255;
    std::string attackerName;
    if (auto* playerAttacker = dynamic_cast<EntityPlayerMP*>(attacker)) {
        attackerKind = 0;
        attackerName = playerAttacker->username;
    } else if (auto* mobAttacker = dynamic_cast<EntityMob*>(attacker)) {
        attackerKind = 1;
        attackerName = mobAttacker->getEntityStringId();
        if (attackerName.empty()) {
            attackerName = "mob";
        }
    } else if (auto* animalAttacker = dynamic_cast<EntityAnimals*>(attacker)) {
        attackerKind = 2;
        attackerName = animalAttacker->getEntityStringId();
        if (attackerName.empty()) {
            attackerName = "animal";
        }
    }

    const bool onCactus = worldObj
        && worldObj->getBlockIdNoChunkLoad(static_cast<int>(std::floor(posX)),
                                           static_cast<int>(std::floor(boundingBox.minY + 0.001)),
                                           static_cast<int>(std::floor(posZ))) == 81;
    const bool drowning = isInsideMaterial(&Material::water) && air <= 0;
    const uint8_t cause = RustBridge::playerDeathCause(
        attacker != nullptr, attackerKind, fallDistance,
        onCactus, drowning, isInLava(), fire > 0);

    switch (cause) {
        case 0:
        case 1:
        case 2:
            lastDeathMessage_ = username + " was slain by " + attackerName;
            break;
        case 3:
            lastDeathMessage_ = username + " hit the ground too hard";
            break;
        case 4:
            lastDeathMessage_ = username + " was pricked to death";
            break;
        case 5:
            lastDeathMessage_ = username + " drowned";
            break;
        case 6:
            lastDeathMessage_ = username + " tried to swim in lava";
            break;
        case 7:
            lastDeathMessage_ = username + " went up in flames";
            break;
        default:
            lastDeathMessage_ = username + " died";
            break;
    }
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
