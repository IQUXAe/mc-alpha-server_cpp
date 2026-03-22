#pragma once

#include "../forward.h"
#include <string>

// Abstract base class for packet handlers (equivalent to Java's NetHandler)
class NetHandler {
public:
    virtual ~NetHandler() = default;

    // Login/handshake phase
    virtual void handleHandshake(Packet2Handshake& pkt) { unexpectedPacket(); }
    virtual void handleLogin(Packet1Login& pkt) { unexpectedPacket(); }

    // Game phase
    virtual void handleChat(Packet3Chat& pkt) { unexpectedPacket(); }
    virtual void handleFlying(Packet10Flying& pkt) { unexpectedPacket(); }
    virtual void handlePlayerPosition(Packet11PlayerPosition& pkt) { unexpectedPacket(); }
    virtual void handlePlayerLook(Packet12PlayerLook& pkt) { unexpectedPacket(); }
    virtual void handlePlayerLookMove(Packet13PlayerLookMove& pkt) { unexpectedPacket(); }
    virtual void handleBlockDig(Packet14BlockDig& pkt) { unexpectedPacket(); }
    virtual void handlePlace(Packet15Place& pkt) { unexpectedPacket(); }
    virtual void handleBlockItemSwitch(Packet16BlockItemSwitch& pkt) { unexpectedPacket(); }
    virtual void handleArmAnimation(Packet18ArmAnimation& pkt) { unexpectedPacket(); }
    virtual void handlePlayerInventory(Packet5PlayerInventory& pkt) { unexpectedPacket(); }
    virtual void handlePickupSpawn(Packet21PickupSpawn& pkt) { unexpectedPacket(); }

    // Disconnect
    virtual void handleKickDisconnect(Packet255KickDisconnect& pkt) { unexpectedPacket(); }
    virtual void handleErrorMessage(const std::string& reason) = 0;

    virtual void handleGenericPacket(Packet& pkt) { unexpectedPacket(); }

private:
    void unexpectedPacket() {
        // Default: kick on unexpected packet in Java
    }
};
