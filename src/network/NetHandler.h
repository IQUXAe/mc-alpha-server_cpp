#pragma once

#include "alpha_bridge.h"
#include <string>

// Abstract base class for packet handlers
class NetHandler {
public:
    virtual ~NetHandler() = default;

    virtual void processPacket(const RustPacket& pkt) {
        switch (pkt.packet_id) {
            case 0: break; // KeepAlive
            case 1: handleLogin(pkt.data.login); break;
            case 2: handleHandshake(pkt.data.handshake); break;
            case 3: handleChat(pkt.data.chat); break;
            case 5: handlePlayerInventory(pkt.data.inventory); break;
            case 7: handleUseEntity(pkt.data.use_entity); break;
            case 9: handleRespawn(); break;
            case 10: handleFlying(pkt.data.flying); break;
            case 11: handlePlayerPosition(pkt.data.position); break;
            case 12: handlePlayerLook(pkt.data.look); break;
            case 13: handlePlayerLookMove(pkt.data.look_move); break;
            case 14: handleBlockDig(pkt.data.block_dig); break;
            case 15: handlePlace(pkt.data.place); break;
            case 16: handleBlockItemSwitch(pkt.data.item_switch); break;
            case 18: handleArmAnimation(pkt.data.arm_anim); break;
            case 21: handlePickupSpawn(pkt.data.pickup_spawn); break;
            case 59: handleComplexEntity(pkt.data.complex_entity); break;
            case 255: handleKickDisconnect(pkt.data.kick); break;
            default: unexpectedPacket(); break;
        }
    }

    // Login/handshake phase
    virtual void handleHandshake(const RustPacket2Handshake& pkt) { unexpectedPacket(); }
    virtual void handleLogin(const RustPacket1Login& pkt) { unexpectedPacket(); }

    // Game phase
    virtual void handleChat(const RustPacket3Chat& pkt) { unexpectedPacket(); }
    virtual void handleRespawn() { unexpectedPacket(); }
    virtual void handleUseEntity(const RustPacket7UseEntity& pkt) { unexpectedPacket(); }
    virtual void handleFlying(const RustPacket10Flying& pkt) { unexpectedPacket(); }
    virtual void handlePlayerPosition(const RustPacket11PlayerPosition& pkt) { unexpectedPacket(); }
    virtual void handlePlayerLook(const RustPacket12PlayerLook& pkt) { unexpectedPacket(); }
    virtual void handlePlayerLookMove(const RustPacket13PlayerLookMove& pkt) { unexpectedPacket(); }
    virtual void handleBlockDig(const RustPacket14BlockDig& pkt) { unexpectedPacket(); }
    virtual void handlePlace(const RustPacket15Place& pkt) { unexpectedPacket(); }
    virtual void handleBlockItemSwitch(const RustPacket16BlockItemSwitch& pkt) { unexpectedPacket(); }
    virtual void handleArmAnimation(const RustPacket18ArmAnimation& pkt) { unexpectedPacket(); }
    virtual void handlePlayerInventory(const RustPacket5PlayerInventory& pkt) { unexpectedPacket(); }
    virtual void handlePickupSpawn(const RustPacket21PickupSpawn& pkt) { unexpectedPacket(); }
    virtual void handleComplexEntity(const RustPacket59ComplexEntity& pkt) { unexpectedPacket(); }

    // Disconnect
    virtual void handleKickDisconnect(const RustPacket255KickDisconnect& pkt) { unexpectedPacket(); }
    virtual void handleErrorMessage(const std::string& reason) = 0;
    virtual bool shouldBypassReadTimeout() const { return false; }

protected:
    void unexpectedPacket() {
        handleErrorMessage("Unexpected packet");
    }
};
