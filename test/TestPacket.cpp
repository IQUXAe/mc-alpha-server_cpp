#include <gtest/gtest.h>
#include "network/RustPackets.h"
#include "network/NetHandler.h"
#include "alpha_bridge.h"

#include <vector>
#include <string>

// Test mock to verify NetHandler dispatch
class MockNetHandler : public NetHandler {
public:
    std::string lastChat;
    bool respawnCalled = false;
    int32_t lastUsePlayer = 0, lastUseTarget = 0;
    bool lastUseLeftClick = false;
    bool lastFlyingOnGround = false;
    double lastPosX = 0, lastPosY = 0, lastPosStance = 0, lastPosZ = 0;
    float lastLookYaw = 0, lastLookPitch = 0;
    int8_t lastDigStatus = 0, lastDigY = 0, lastDigFace = 0;
    int32_t lastDigX = 0, lastDigZ = 0;
    int16_t lastPlaceItem = 0;
    int32_t lastPlaceX = 0, lastPlaceZ = 0;
    int8_t lastPlaceY = 0, lastPlaceDir = 0;
    int32_t lastSwitchEntity = 0;
    int16_t lastSwitchItem = 0;
    int32_t lastArmEntity = 0;
    int8_t lastArmAnimate = 0;
    std::string lastKickReason;
    int32_t lastInvType = 0;
    int16_t lastInvCount = 0;
    int32_t lastPickupEntity = 0;
    int16_t lastPickupItem = 0;
    int32_t lastComplexX = 0, lastComplexZ = 0;
    int16_t lastComplexY = 0;
    size_t lastComplexLen = 0;

    void handleChat(const RustPacket3Chat& pkt) override {
        lastChat = pkt.message ? pkt.message : "";
    }
    void handleRespawn() override {
        respawnCalled = true;
    }
    void handleUseEntity(const RustPacket7UseEntity& pkt) override {
        lastUsePlayer = pkt.player_entity_id;
        lastUseTarget = pkt.target_entity_id;
        lastUseLeftClick = pkt.is_left_click;
    }
    void handleFlying(const RustPacket10Flying& pkt) override {
        lastFlyingOnGround = pkt.on_ground;
    }
    void handlePlayerPosition(const RustPacket11PlayerPosition& pkt) override {
        lastPosX = pkt.x;
        lastPosY = pkt.y;
        lastPosStance = pkt.stance;
        lastPosZ = pkt.z;
    }
    void handlePlayerLook(const RustPacket12PlayerLook& pkt) override {
        lastLookYaw = pkt.yaw;
        lastLookPitch = pkt.pitch;
    }
    void handlePlayerLookMove(const RustPacket13PlayerLookMove& pkt) override {
        lastPosX = pkt.x;
        lastPosY = pkt.y;
        lastPosStance = pkt.stance;
        lastPosZ = pkt.z;
        lastLookYaw = pkt.yaw;
        lastLookPitch = pkt.pitch;
    }
    void handleBlockDig(const RustPacket14BlockDig& pkt) override {
        lastDigStatus = pkt.status;
        lastDigX = pkt.x;
        lastDigY = pkt.y;
        lastDigZ = pkt.z;
        lastDigFace = pkt.face;
    }
    void handlePlace(const RustPacket15Place& pkt) override {
        lastPlaceItem = pkt.item_id;
        lastPlaceX = pkt.x;
        lastPlaceY = pkt.y;
        lastPlaceZ = pkt.z;
        lastPlaceDir = pkt.direction;
    }
    void handleBlockItemSwitch(const RustPacket16BlockItemSwitch& pkt) override {
        lastSwitchEntity = pkt.entity_id;
        lastSwitchItem = pkt.item_id;
    }
    void handleArmAnimation(const RustPacket18ArmAnimation& pkt) override {
        lastArmEntity = pkt.entity_id;
        lastArmAnimate = pkt.animate;
    }
    void handleKickDisconnect(const RustPacket255KickDisconnect& pkt) override {
        lastKickReason = pkt.reason ? pkt.reason : "";
    }
    void handlePlayerInventory(const RustPacket5PlayerInventory& pkt) override {
        lastInvType = pkt.type;
        lastInvCount = pkt.item_count;
    }
    void handlePickupSpawn(const RustPacket21PickupSpawn& pkt) override {
        lastPickupEntity = pkt.entity_id;
        lastPickupItem = pkt.item_id;
    }
    void handleComplexEntity(const RustPacket59ComplexEntity& pkt) override {
        lastComplexX = pkt.x;
        lastComplexY = pkt.y;
        lastComplexZ = pkt.z;
        lastComplexLen = pkt.nbt_len;
    }
    void handleErrorMessage(const std::string& /*reason*/) override {}
};

// RustPackets Builder Tests

TEST(RustPacketsTest, KeepAlive) {
    auto p = RustPackets::keepAlive();
    EXPECT_EQ(p.packet_id, 0);
}

TEST(RustPacketsTest, Login) {
    auto p = RustPackets::login(123, "Steve", "secret", 987654321LL, 0);
    EXPECT_EQ(p.packet_id, 1);
    EXPECT_EQ(p.data.login.protocol_version, 123);
    EXPECT_STREQ(p.data.login.username, "Steve");
    EXPECT_STREQ(p.data.login.password, "secret");
    EXPECT_EQ(p.data.login.map_seed, 987654321LL);
    EXPECT_EQ(p.data.login.dimension, 0);
}

TEST(RustPacketsTest, Handshake) {
    auto p = RustPackets::handshake("Alex");
    EXPECT_EQ(p.packet_id, 2);
    EXPECT_STREQ(p.data.handshake.username, "Alex");
}

TEST(RustPacketsTest, Chat) {
    auto p = RustPackets::chat("Hello world!");
    EXPECT_EQ(p.packet_id, 3);
    EXPECT_STREQ(p.data.chat.message, "Hello world!");
}

TEST(RustPacketsTest, UpdateTime) {
    auto p = RustPackets::updateTime(1234567890LL);
    EXPECT_EQ(p.packet_id, 4);
    EXPECT_EQ(p.data.update_time.time, 1234567890LL);
}

TEST(RustPacketsTest, PlayerInventory) {
    FfiSlotData slots[2] = {
        { 264, 1, 0 },
        { 1, 64, 0 }
    };
    auto p = RustPackets::playerInventory(-1, 2, slots);
    EXPECT_EQ(p.packet_id, 5);
    EXPECT_EQ(p.data.inventory.type, -1);
    EXPECT_EQ(p.data.inventory.item_count, 2);
    ASSERT_NE(p.data.inventory.slots, nullptr);
    EXPECT_EQ(p.data.inventory.slots[0].item_id, 264);
    EXPECT_EQ(p.data.inventory.slots[1].count, 64);
}

TEST(RustPacketsTest, SpawnPosition) {
    auto p = RustPackets::spawnPosition(100, 64, -200);
    EXPECT_EQ(p.packet_id, 6);
    EXPECT_EQ(p.data.spawn_position.x, 100);
    EXPECT_EQ(p.data.spawn_position.y, 64);
    EXPECT_EQ(p.data.spawn_position.z, -200);
}

TEST(RustPacketsTest, UseEntity) {
    auto p = RustPackets::useEntity(1, 2, true);
    EXPECT_EQ(p.packet_id, 7);
    EXPECT_EQ(p.data.use_entity.player_entity_id, 1);
    EXPECT_EQ(p.data.use_entity.target_entity_id, 2);
    EXPECT_TRUE(p.data.use_entity.is_left_click);
}

TEST(RustPacketsTest, UpdateHealth) {
    auto p = RustPackets::updateHealth(20);
    EXPECT_EQ(p.packet_id, 8);
    EXPECT_EQ(p.data.update_health.health, 20);
}

TEST(RustPacketsTest, Respawn) {
    auto p = RustPackets::respawn();
    EXPECT_EQ(p.packet_id, 9);
}

TEST(RustPacketsTest, Flying) {
    auto p = RustPackets::flying(true);
    EXPECT_EQ(p.packet_id, 10);
    EXPECT_TRUE(p.data.flying.on_ground);
}

TEST(RustPacketsTest, PlayerPosition) {
    auto p = RustPackets::playerPosition(10.5, 64.0, 65.62, -20.5, true);
    EXPECT_EQ(p.packet_id, 11);
    EXPECT_DOUBLE_EQ(p.data.position.x, 10.5);
    EXPECT_DOUBLE_EQ(p.data.position.y, 64.0);
    EXPECT_DOUBLE_EQ(p.data.position.stance, 65.62);
    EXPECT_DOUBLE_EQ(p.data.position.z, -20.5);
    EXPECT_TRUE(p.data.position.on_ground);
}

TEST(RustPacketsTest, PlayerLook) {
    auto p = RustPackets::playerLook(90.0f, -45.0f, false);
    EXPECT_EQ(p.packet_id, 12);
    EXPECT_FLOAT_EQ(p.data.look.yaw, 90.0f);
    EXPECT_FLOAT_EQ(p.data.look.pitch, -45.0f);
    EXPECT_FALSE(p.data.look.on_ground);
}

TEST(RustPacketsTest, PlayerLookMove) {
    auto p = RustPackets::playerLookMove(1.0, 2.0, 3.0, 4.0, 5.0f, 6.0f, true);
    EXPECT_EQ(p.packet_id, 13);
    EXPECT_DOUBLE_EQ(p.data.look_move.x, 1.0);
    EXPECT_DOUBLE_EQ(p.data.look_move.y, 2.0);
    EXPECT_DOUBLE_EQ(p.data.look_move.stance, 3.0);
    EXPECT_DOUBLE_EQ(p.data.look_move.z, 4.0);
    EXPECT_FLOAT_EQ(p.data.look_move.yaw, 5.0f);
    EXPECT_FLOAT_EQ(p.data.look_move.pitch, 6.0f);
    EXPECT_TRUE(p.data.look_move.on_ground);
}

TEST(RustPacketsTest, BlockDig) {
    auto p = RustPackets::blockDig(0, 100, 64, -200, 1);
    EXPECT_EQ(p.packet_id, 14);
    EXPECT_EQ(p.data.block_dig.status, 0);
    EXPECT_EQ(p.data.block_dig.x, 100);
    EXPECT_EQ(p.data.block_dig.y, 64);
    EXPECT_EQ(p.data.block_dig.z, -200);
    EXPECT_EQ(p.data.block_dig.face, 1);
}

TEST(RustPacketsTest, Place) {
    auto p = RustPackets::place(1, 10, 60, -30, 2);
    EXPECT_EQ(p.packet_id, 15);
    EXPECT_EQ(p.data.place.item_id, 1);
    EXPECT_EQ(p.data.place.x, 10);
    EXPECT_EQ(p.data.place.y, 60);
    EXPECT_EQ(p.data.place.z, -30);
    EXPECT_EQ(p.data.place.direction, 2);
}

TEST(RustPacketsTest, BlockItemSwitch) {
    auto p = RustPackets::blockItemSwitch(42, 276);
    EXPECT_EQ(p.packet_id, 16);
    EXPECT_EQ(p.data.item_switch.entity_id, 42);
    EXPECT_EQ(p.data.item_switch.item_id, 276);
}

TEST(RustPacketsTest, AddToInventory) {
    auto p = RustPackets::addToInventory(264, 5, 10);
    EXPECT_EQ(p.packet_id, 17);
    EXPECT_EQ(p.data.add_to_inventory.item_id, 264);
    EXPECT_EQ(p.data.add_to_inventory.count, 5);
    EXPECT_EQ(p.data.add_to_inventory.damage, 10);
}

TEST(RustPacketsTest, ArmAnimation) {
    auto p = RustPackets::armAnimation(100, 1);
    EXPECT_EQ(p.packet_id, 18);
    EXPECT_EQ(p.data.arm_anim.entity_id, 100);
    EXPECT_EQ(p.data.arm_anim.animate, 1);
}

TEST(RustPacketsTest, NamedEntitySpawn) {
    auto p = RustPackets::namedEntitySpawn(50, "Player1", 100, 200, 300, 10, 20, 276);
    EXPECT_EQ(p.packet_id, 20);
    EXPECT_EQ(p.data.named_entity_spawn.entity_id, 50);
    EXPECT_STREQ(p.data.named_entity_spawn.name, "Player1");
    EXPECT_EQ(p.data.named_entity_spawn.x, 100);
    EXPECT_EQ(p.data.named_entity_spawn.y, 200);
    EXPECT_EQ(p.data.named_entity_spawn.z, 300);
    EXPECT_EQ(p.data.named_entity_spawn.rotation, 10);
    EXPECT_EQ(p.data.named_entity_spawn.pitch, 20);
    EXPECT_EQ(p.data.named_entity_spawn.current_item, 276);
}

TEST(RustPacketsTest, PickupSpawn) {
    auto p = RustPackets::pickupSpawn(7, 264, 3, 10, 20, 30, 0, 0, 0);
    EXPECT_EQ(p.packet_id, 21);
    EXPECT_EQ(p.data.pickup_spawn.entity_id, 7);
    EXPECT_EQ(p.data.pickup_spawn.item_id, 264);
    EXPECT_EQ(p.data.pickup_spawn.count, 3);
    EXPECT_EQ(p.data.pickup_spawn.x, 10);
    EXPECT_EQ(p.data.pickup_spawn.y, 20);
    EXPECT_EQ(p.data.pickup_spawn.z, 30);
}

TEST(RustPacketsTest, Collect) {
    auto p = RustPackets::collect(100, 200);
    EXPECT_EQ(p.packet_id, 22);
    EXPECT_EQ(p.data.collect.collected_entity_id, 100);
    EXPECT_EQ(p.data.collect.collector_entity_id, 200);
}

TEST(RustPacketsTest, VehicleSpawn) {
    auto p = RustPackets::vehicleSpawn(10, 1, 100, 200, 300);
    EXPECT_EQ(p.packet_id, 23);
    EXPECT_EQ(p.data.vehicle_spawn.entity_id, 10);
    EXPECT_EQ(p.data.vehicle_spawn.vehicle_type, 1);
    EXPECT_EQ(p.data.vehicle_spawn.x, 100);
    EXPECT_EQ(p.data.vehicle_spawn.y, 200);
    EXPECT_EQ(p.data.vehicle_spawn.z, 300);
}

TEST(RustPacketsTest, MobSpawn) {
    auto p = RustPackets::mobSpawn(50, 90, 100, 200, 300, 15, 25);
    EXPECT_EQ(p.packet_id, 24);
    EXPECT_EQ(p.data.mob_spawn.entity_id, 50);
    EXPECT_EQ(p.data.mob_spawn.mob_type, 90);
    EXPECT_EQ(p.data.mob_spawn.x, 100);
    EXPECT_EQ(p.data.mob_spawn.y, 200);
    EXPECT_EQ(p.data.mob_spawn.z, 300);
    EXPECT_EQ(p.data.mob_spawn.yaw, 15);
    EXPECT_EQ(p.data.mob_spawn.pitch, 25);
}

TEST(RustPacketsTest, EntityVelocity) {
    auto p = RustPackets::velocity(50, 0.5, -0.3, 0.0);
    EXPECT_EQ(p.packet_id, 28);
    EXPECT_EQ(p.data.entity_velocity.entity_id, 50);
    EXPECT_EQ(p.data.entity_velocity.motion_x, static_cast<int16_t>(0.5 * 8000.0));
    EXPECT_EQ(p.data.entity_velocity.motion_y, static_cast<int16_t>(-0.3 * 8000.0));
    EXPECT_EQ(p.data.entity_velocity.motion_z, 0);
}

TEST(RustPacketsTest, DestroyEntity) {
    auto p = RustPackets::destroyEntity(42);
    EXPECT_EQ(p.packet_id, 29);
    EXPECT_EQ(p.data.destroy_entity.entity_id, 42);
}

TEST(RustPacketsTest, EntityMovementPackets) {
    auto p30 = RustPackets::entity(10);
    EXPECT_EQ(p30.packet_id, 30);
    EXPECT_EQ(p30.data.entity.entity_id, 10);

    auto p31 = RustPackets::relEntityMove(10, 1, 2, 3);
    EXPECT_EQ(p31.packet_id, 31);
    EXPECT_EQ(p31.data.rel_entity_move.entity_id, 10);
    EXPECT_EQ(p31.data.rel_entity_move.dx, 1);
    EXPECT_EQ(p31.data.rel_entity_move.dy, 2);
    EXPECT_EQ(p31.data.rel_entity_move.dz, 3);

    auto p32 = RustPackets::entityLook(10, 4, 5);
    EXPECT_EQ(p32.packet_id, 32);
    EXPECT_EQ(p32.data.entity_look.entity_id, 10);
    EXPECT_EQ(p32.data.entity_look.yaw, 4);
    EXPECT_EQ(p32.data.entity_look.pitch, 5);

    auto p33 = RustPackets::relEntityMoveLook(10, 1, 2, 3, 4, 5);
    EXPECT_EQ(p33.packet_id, 33);
    EXPECT_EQ(p33.data.rel_entity_move_look.entity_id, 10);
    EXPECT_EQ(p33.data.rel_entity_move_look.dx, 1);
    EXPECT_EQ(p33.data.rel_entity_move_look.yaw, 4);

    auto p34 = RustPackets::entityTeleport(10, 100, 200, 300, 4, 5);
    EXPECT_EQ(p34.packet_id, 34);
    EXPECT_EQ(p34.data.entity_teleport.entity_id, 10);
    EXPECT_EQ(p34.data.entity_teleport.x, 100);
    EXPECT_EQ(p34.data.entity_teleport.y, 200);
    EXPECT_EQ(p34.data.entity_teleport.z, 300);
}

TEST(RustPacketsTest, EntityStatus) {
    auto p = RustPackets::entityStatus(10, 2);
    EXPECT_EQ(p.packet_id, 38);
    EXPECT_EQ(p.data.entity_status.entity_id, 10);
    EXPECT_EQ(p.data.entity_status.status, 2);
}

TEST(RustPacketsTest, AttachEntity) {
    auto p = RustPackets::attachEntity(10, 20);
    EXPECT_EQ(p.packet_id, 39);
    EXPECT_EQ(p.data.attach_entity.entity_id, 10);
    EXPECT_EQ(p.data.attach_entity.vehicle_id, 20);
}

TEST(RustPacketsTest, PreChunk) {
    auto p = RustPackets::preChunk(10, -20, true);
    EXPECT_EQ(p.packet_id, 50);
    EXPECT_EQ(p.data.pre_chunk.x, 10);
    EXPECT_EQ(p.data.pre_chunk.z, -20);
    EXPECT_TRUE(p.data.pre_chunk.mode);
}

TEST(RustPacketsTest, BlockChange) {
    auto p = RustPackets::blockChange(100, 64, -200, 1, 3);
    EXPECT_EQ(p.packet_id, 53);
    EXPECT_EQ(p.data.block_change.x, 100);
    EXPECT_EQ(p.data.block_change.y, 64);
    EXPECT_EQ(p.data.block_change.z, -200);
    EXPECT_EQ(p.data.block_change.block_type, 1);
    EXPECT_EQ(p.data.block_change.metadata, 3);
}

TEST(RustPacketsTest, ComplexEntity) {
    uint8_t data[4] = {1, 2, 3, 4};
    auto p = RustPackets::complexEntity(10, 64, -10, data, 4);
    EXPECT_EQ(p.packet_id, 59);
    EXPECT_EQ(p.data.complex_entity.x, 10);
    EXPECT_EQ(p.data.complex_entity.y, 64);
    EXPECT_EQ(p.data.complex_entity.z, -10);
    EXPECT_EQ(p.data.complex_entity.nbt_data, data);
    EXPECT_EQ(p.data.complex_entity.nbt_len, 4u);
}

TEST(RustPacketsTest, KickDisconnect) {
    auto p = RustPackets::kickDisconnect("Server full");
    EXPECT_EQ(p.packet_id, 255);
    EXPECT_STREQ(p.data.kick.reason, "Server full");
}

// NetHandler Dispatch Tests

TEST(NetHandlerDispatchTest, DispatchChat) {
    MockNetHandler handler;
    auto pkt = RustPackets::chat("Testing dispatch");
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastChat, "Testing dispatch");
}

TEST(NetHandlerDispatchTest, DispatchRespawn) {
    MockNetHandler handler;
    auto pkt = RustPackets::respawn();
    handler.processPacket(pkt);
    EXPECT_TRUE(handler.respawnCalled);
}

TEST(NetHandlerDispatchTest, DispatchUseEntity) {
    MockNetHandler handler;
    auto pkt = RustPackets::useEntity(10, 20, true);
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastUsePlayer, 10);
    EXPECT_EQ(handler.lastUseTarget, 20);
    EXPECT_TRUE(handler.lastUseLeftClick);
}

TEST(NetHandlerDispatchTest, DispatchFlying) {
    MockNetHandler handler;
    auto pkt = RustPackets::flying(true);
    handler.processPacket(pkt);
    EXPECT_TRUE(handler.lastFlyingOnGround);
}

TEST(NetHandlerDispatchTest, DispatchPlayerPosition) {
    MockNetHandler handler;
    auto pkt = RustPackets::playerPosition(1.5, 64.0, 65.62, -2.5, true);
    handler.processPacket(pkt);
    EXPECT_DOUBLE_EQ(handler.lastPosX, 1.5);
    EXPECT_DOUBLE_EQ(handler.lastPosY, 64.0);
    EXPECT_DOUBLE_EQ(handler.lastPosStance, 65.62);
    EXPECT_DOUBLE_EQ(handler.lastPosZ, -2.5);
}

TEST(NetHandlerDispatchTest, DispatchPlayerLook) {
    MockNetHandler handler;
    auto pkt = RustPackets::playerLook(180.0f, -90.0f, false);
    handler.processPacket(pkt);
    EXPECT_FLOAT_EQ(handler.lastLookYaw, 180.0f);
    EXPECT_FLOAT_EQ(handler.lastLookPitch, -90.0f);
}

TEST(NetHandlerDispatchTest, DispatchPlayerLookMove) {
    MockNetHandler handler;
    auto pkt = RustPackets::playerLookMove(10.0, 20.0, 21.6, 30.0, 45.0f, -15.0f, true);
    handler.processPacket(pkt);
    EXPECT_DOUBLE_EQ(handler.lastPosX, 10.0);
    EXPECT_DOUBLE_EQ(handler.lastPosY, 20.0);
    EXPECT_DOUBLE_EQ(handler.lastPosStance, 21.6);
    EXPECT_DOUBLE_EQ(handler.lastPosZ, 30.0);
    EXPECT_FLOAT_EQ(handler.lastLookYaw, 45.0f);
    EXPECT_FLOAT_EQ(handler.lastLookPitch, -15.0f);
}

TEST(NetHandlerDispatchTest, DispatchBlockDig) {
    MockNetHandler handler;
    auto pkt = RustPackets::blockDig(2, 50, 64, 75, 1);
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastDigStatus, 2);
    EXPECT_EQ(handler.lastDigX, 50);
    EXPECT_EQ(handler.lastDigY, 64);
    EXPECT_EQ(handler.lastDigZ, 75);
    EXPECT_EQ(handler.lastDigFace, 1);
}

TEST(NetHandlerDispatchTest, DispatchPlace) {
    MockNetHandler handler;
    auto pkt = RustPackets::place(4, 10, 64, -10, 0);
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastPlaceItem, 4);
    EXPECT_EQ(handler.lastPlaceX, 10);
    EXPECT_EQ(handler.lastPlaceY, 64);
    EXPECT_EQ(handler.lastPlaceZ, -10);
    EXPECT_EQ(handler.lastPlaceDir, 0);
}

TEST(NetHandlerDispatchTest, DispatchBlockItemSwitch) {
    MockNetHandler handler;
    auto pkt = RustPackets::blockItemSwitch(1, 276);
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastSwitchEntity, 1);
    EXPECT_EQ(handler.lastSwitchItem, 276);
}

TEST(NetHandlerDispatchTest, DispatchArmAnimation) {
    MockNetHandler handler;
    auto pkt = RustPackets::armAnimation(5, 104);
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastArmEntity, 5);
    EXPECT_EQ(handler.lastArmAnimate, 104);
}

TEST(NetHandlerDispatchTest, DispatchKickDisconnect) {
    MockNetHandler handler;
    auto pkt = RustPackets::kickDisconnect("Quitting");
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastKickReason, "Quitting");
}

TEST(NetHandlerDispatchTest, DispatchPlayerInventory) {
    MockNetHandler handler;
    FfiSlotData slot{264, 1, 0};
    auto pkt = RustPackets::playerInventory(-1, 1, &slot);
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastInvType, -1);
    EXPECT_EQ(handler.lastInvCount, 1);
}

TEST(NetHandlerDispatchTest, DispatchPickupSpawn) {
    MockNetHandler handler;
    auto pkt = RustPackets::pickupSpawn(10, 264, 1, 10, 20, 30, 0, 0, 0);
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastPickupEntity, 10);
    EXPECT_EQ(handler.lastPickupItem, 264);
}

TEST(NetHandlerDispatchTest, DispatchComplexEntity) {
    MockNetHandler handler;
    uint8_t dummy[8] = {0};
    auto pkt = RustPackets::complexEntity(10, 64, -20, dummy, 8);
    handler.processPacket(pkt);
    EXPECT_EQ(handler.lastComplexX, 10);
    EXPECT_EQ(handler.lastComplexY, 64);
    EXPECT_EQ(handler.lastComplexZ, -20);
    EXPECT_EQ(handler.lastComplexLen, 8u);
}
