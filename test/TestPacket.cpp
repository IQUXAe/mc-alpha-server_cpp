#include <gtest/gtest.h>
#include "network/Packet.h"
#include "network/packets/AllPackets.h"

class PacketTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        Packet::registerPackets();
    }
};

TEST_F(PacketTest, Packet3ChatRoundTrip) {
    Packet3Chat original("Hello, Minecraft!");
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet3Chat read;
    read.readPacketData(buf);

    EXPECT_EQ(read.message, "Hello, Minecraft!");
}

TEST_F(PacketTest, Packet3ChatEmpty) {
    Packet3Chat original("");
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet3Chat read;
    read.readPacketData(buf);
    EXPECT_EQ(read.message, "");
}

TEST_F(PacketTest, Packet1LoginRoundTrip) {
    Packet1Login original("Player1", "pass", 42, 12345, 0);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet1Login read;
    read.readPacketData(buf);

    EXPECT_EQ(read.username, "Player1");
    EXPECT_EQ(read.password, "pass");
    EXPECT_EQ(read.protocolVersion, 42);
    EXPECT_EQ(read.mapSeed, 12345);
    EXPECT_EQ(read.dimension, 0);
}

TEST_F(PacketTest, Packet4UpdateTime) {
    Packet4UpdateTime original(1234567890LL);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet4UpdateTime read;
    read.readPacketData(buf);
    EXPECT_EQ(read.time, 1234567890LL);
}

TEST_F(PacketTest, Packet6SpawnPosition) {
    Packet6SpawnPosition original(100, 64, -200);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet6SpawnPosition read;
    read.readPacketData(buf);
    EXPECT_EQ(read.x, 100);
    EXPECT_EQ(read.y, 64);
    EXPECT_EQ(read.z, -200);
}

TEST_F(PacketTest, Packet255KickDisconnect) {
    Packet255KickDisconnect original("Server shutting down");
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet255KickDisconnect read;
    read.readPacketData(buf);
    EXPECT_EQ(read.reason, "Server shutting down");
}

TEST_F(PacketTest, Packet20NamedEntitySpawn) {
    Packet20NamedEntitySpawn original;
    original.entityId = 100;
    original.name = "Player";
    original.x = 1000;
    original.y = 2000;
    original.z = 3000;
    original.rotation = 64;
    original.pitch = 32;
    original.currentItem = 264;

    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet20NamedEntitySpawn read;
    read.readPacketData(buf);
    EXPECT_EQ(read.entityId, 100);
    EXPECT_EQ(read.name, "Player");
    EXPECT_EQ(read.x, 1000);
    EXPECT_EQ(read.y, 2000);
    EXPECT_EQ(read.z, 3000);
    EXPECT_EQ(read.currentItem, 264);
}

TEST_F(PacketTest, Packet53BlockChange) {
    Packet53BlockChange original(100, 64, -200, 1, 3);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet53BlockChange read;
    read.readPacketData(buf);
    EXPECT_EQ(read.x, 100);
    EXPECT_EQ(read.y, 64);
    EXPECT_EQ(read.z, -200);
    EXPECT_EQ(read.type, 1);
    EXPECT_EQ(read.metadata, 3);
}

TEST_F(PacketTest, Packet50PreChunk) {
    Packet50PreChunk original(10, -20, true);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet50PreChunk read;
    read.readPacketData(buf);
    EXPECT_EQ(read.x, 10);
    EXPECT_EQ(read.z, -20);
    EXPECT_TRUE(read.load);

    Packet50PreChunk unload(-5, 3, false);
    ByteBuffer buf2;
    unload.writePacketData(buf2);
    buf2.readPos = 0;
    read.readPacketData(buf2);
    EXPECT_EQ(read.x, -5);
    EXPECT_FALSE(read.load);
}

TEST_F(PacketTest, Packet29DestroyEntity) {
    Packet29DestroyEntity original(42);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet29DestroyEntity read;
    read.readPacketData(buf);
    EXPECT_EQ(read.entityId, 42);
}

TEST_F(PacketTest, Packet22Collect) {
    Packet22Collect original(100, 200);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet22Collect read;
    read.readPacketData(buf);
    EXPECT_EQ(read.collectedEntityId, 100);
    EXPECT_EQ(read.collectorEntityId, 200);
}

TEST_F(PacketTest, Packet8UpdateHealth) {
    Packet8UpdateHealth original(20);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet8UpdateHealth read;
    read.readPacketData(buf);
    EXPECT_EQ(read.health, 20);
}

TEST_F(PacketTest, Packet28EntityVelocity) {
    Packet28EntityVelocity original(50, 0.5, -0.3, 0.0);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet28EntityVelocity read;
    read.readPacketData(buf);
    EXPECT_EQ(read.entityId, 50);
    EXPECT_EQ(read.motionX, static_cast<int16_t>(0.5 * 8000.0));
    EXPECT_EQ(read.motionY, static_cast<int16_t>(-0.3 * 8000.0));
    EXPECT_EQ(read.motionZ, 0);
}

TEST_F(PacketTest, PacketFactoryCreate) {
    auto pkt = Packet::createPacket(3);
    ASSERT_NE(pkt, nullptr);
    EXPECT_EQ(pkt->getPacketId(), 3);
}

TEST_F(PacketTest, PacketFactoryInvalidId) {
    auto pkt = Packet::createPacket(999);
    EXPECT_EQ(pkt, nullptr);
}

TEST_F(PacketTest, PacketIdMapping) {
    EXPECT_EQ(Packet::classToId[typeid(Packet0KeepAlive).name()], 0);
    EXPECT_EQ(Packet::classToId[typeid(Packet3Chat).name()], 3);
    EXPECT_EQ(Packet::classToId[typeid(Packet255KickDisconnect).name()], 255);
}

TEST_F(PacketTest, Packet5PlayerInventoryEmpty) {
    Packet5PlayerInventory original;
    original.type = 0;
    original.slots = {};

    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet5PlayerInventory read;
    read.readPacketData(buf);
    EXPECT_EQ(read.type, 0);
    EXPECT_TRUE(read.slots.empty());
}

TEST_F(PacketTest, Packet5PlayerInventoryWithSlots) {
    Packet5PlayerInventory pkt;
    pkt.type = 0;
    pkt.slots = {
        {264, 1, 0},  // diamond
        {-1, 0, 0},   // empty slot
        {1, 64, 0}    // stone
    };

    ByteBuffer buf;
    pkt.writePacketData(buf);
    buf.readPos = 0;

    Packet5PlayerInventory read;
    read.readPacketData(buf);
    ASSERT_EQ(read.slots.size(), 3);
    EXPECT_EQ(read.slots[0].itemId, 264);
    EXPECT_EQ(read.slots[0].count, 1);
    EXPECT_EQ(read.slots[1].itemId, -1);
    EXPECT_EQ(read.slots[2].itemId, 1);
    EXPECT_EQ(read.slots[2].count, 64);
}

TEST_F(PacketTest, Packet13PlayerLookMove) {
    Packet13PlayerLookMove original(100.5, 64.0, 65.62, -200.3, 45.0f, 30.0f, true);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet13PlayerLookMove read;
    read.readPacketData(buf);
    EXPECT_DOUBLE_EQ(read.x, 100.5);
    EXPECT_DOUBLE_EQ(read.y, 64.0);
    EXPECT_DOUBLE_EQ(read.z, -200.3);
    EXPECT_FLOAT_EQ(read.yaw, 45.0f);
    EXPECT_FLOAT_EQ(read.pitch, 30.0f);
    EXPECT_TRUE(read.onGround);
    EXPECT_TRUE(read.moving);
    EXPECT_TRUE(read.rotating);
}

TEST_F(PacketTest, Packet14BlockDig) {
    Packet14BlockDig original;
    original.status = 0;
    original.x = 100;
    original.y = 64;
    original.z = -200;
    original.face = 1;

    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet14BlockDig read;
    read.readPacketData(buf);
    EXPECT_EQ(read.status, 0);
    EXPECT_EQ(read.x, 100);
    EXPECT_EQ(read.y, 64);
    EXPECT_EQ(read.z, -200);
    EXPECT_EQ(read.face, 1);
}

TEST_F(PacketTest, Packet60Explosion) {
    Packet60Explosion original;
    original.x = 10.5;
    original.y = 64.0;
    original.z = -20.5;
    original.radius = 4.0f;
    original.destroyedBlocks = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet60Explosion read;
    read.readPacketData(buf);
    EXPECT_DOUBLE_EQ(read.x, 10.5);
    EXPECT_DOUBLE_EQ(read.y, 64.0);
    EXPECT_DOUBLE_EQ(read.z, -20.5);
    EXPECT_FLOAT_EQ(read.radius, 4.0f);
    ASSERT_EQ(read.destroyedBlocks.size(), 3);
    EXPECT_EQ(read.destroyedBlocks[0].dx, 1);
}

TEST_F(PacketTest, Packet24MobSpawn) {
    Packet24MobSpawn original;
    original.entityId = 50;
    original.type = 51; // creeper
    original.x = 1000;
    original.y = 2000;
    original.z = 3000;
    original.yaw = 0;
    original.pitch = 0;

    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet24MobSpawn read;
    read.readPacketData(buf);
    EXPECT_EQ(read.entityId, 50);
    EXPECT_EQ(read.type, 51);
    EXPECT_EQ(read.x, 1000);
}

TEST_F(PacketTest, Packet10FlyingRoundTrip) {
    Packet10Flying original;
    original.onGround = true;
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet10Flying read;
    read.readPacketData(buf);
    EXPECT_TRUE(read.onGround);
}

TEST_F(PacketTest, Packet0KeepAlive) {
    Packet0KeepAlive original;
    ByteBuffer buf;
    original.writePacketData(buf);
    ASSERT_EQ(buf.data.size(), 0);
}

TEST_F(PacketTest, Packet39AttachEntity) {
    Packet39AttachEntity original;
    original.entityId = 10;
    original.vehicleEntityId = 20;

    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet39AttachEntity read;
    read.readPacketData(buf);
    EXPECT_EQ(read.entityId, 10);
    EXPECT_EQ(read.vehicleEntityId, 20);
}

TEST_F(PacketTest, Packet2Handshake) {
    Packet2Handshake original("TestUser");
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet2Handshake read;
    read.readPacketData(buf);
    EXPECT_EQ(read.username, "TestUser");
}

TEST_F(PacketTest, Packet17AddToInventory) {
    Packet17AddToInventory original(264, 5, 0);
    ByteBuffer buf;
    original.writePacketData(buf);
    buf.readPos = 0;

    Packet17AddToInventory read;
    read.readPacketData(buf);
    EXPECT_EQ(read.itemId, 264);
    EXPECT_EQ(read.count, 5);
    EXPECT_EQ(read.damage, 0);
}
