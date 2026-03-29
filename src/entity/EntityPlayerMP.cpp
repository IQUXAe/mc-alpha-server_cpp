#include "EntityPlayerMP.h"

#include "EntityItem.h"
#include "../MinecraftServer.h"
#include "../network/packets/AllPackets.h"
#include "../world/World.h"

#include <cstdlib>
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

    EntityLiving::onDeath();
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
}

bool EntityPlayerMP::canAttackNow() const {
    return !isDead && health > 0;
}

void EntityPlayerMP::markAttackPerformed() {
    attackCooldownTicks = 0;
}
