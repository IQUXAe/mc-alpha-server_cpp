#include <gtest/gtest.h>
#include "network/Packet.h"

TEST(ByteBufferTest, WriteReadByte) {
    ByteBuffer buf;
    buf.writeByte(-128);
    buf.writeByte(127);
    ASSERT_EQ(buf.readPos, 0);
    EXPECT_EQ(buf.readByte(), -128);
    EXPECT_EQ(buf.readByte(), 127);
}

TEST(ByteBufferTest, WriteReadUByte) {
    ByteBuffer buf;
    buf.writeUByte(0);
    buf.writeUByte(255);
    EXPECT_EQ(buf.readUByte(), 0);
    EXPECT_EQ(buf.readUByte(), 255);
}

TEST(ByteBufferTest, WriteReadShort) {
    ByteBuffer buf;
    buf.writeShort(-32768);
    buf.writeShort(32767);
    buf.writeShort(0);
    EXPECT_EQ(buf.readShort(), -32768);
    EXPECT_EQ(buf.readShort(), 32767);
    EXPECT_EQ(buf.readShort(), 0);
}

TEST(ByteBufferTest, WriteReadInt) {
    ByteBuffer buf;
    buf.writeInt(-2147483648);
    buf.writeInt(2147483647);
    buf.writeInt(0);
    EXPECT_EQ(buf.readInt(), -2147483648);
    EXPECT_EQ(buf.readInt(), 2147483647);
    EXPECT_EQ(buf.readInt(), 0);
}

TEST(ByteBufferTest, WriteReadLong) {
    ByteBuffer buf;
    buf.writeLong(-9223372036854775807LL - 1);
    buf.writeLong(9223372036854775807LL);
    buf.writeLong(0);
    EXPECT_EQ(buf.readLong(), -9223372036854775807LL - 1);
    EXPECT_EQ(buf.readLong(), 9223372036854775807LL);
    EXPECT_EQ(buf.readLong(), 0);
}

TEST(ByteBufferTest, WriteReadFloat) {
    ByteBuffer buf;
    buf.writeFloat(3.14159f);
    buf.writeFloat(0.0f);
    buf.writeFloat(-1.0f);
    EXPECT_FLOAT_EQ(buf.readFloat(), 3.14159f);
    EXPECT_FLOAT_EQ(buf.readFloat(), 0.0f);
    EXPECT_FLOAT_EQ(buf.readFloat(), -1.0f);
}

TEST(ByteBufferTest, WriteReadDouble) {
    ByteBuffer buf;
    buf.writeDouble(3.14159265358979);
    buf.writeDouble(0.0);
    EXPECT_DOUBLE_EQ(buf.readDouble(), 3.14159265358979);
    EXPECT_DOUBLE_EQ(buf.readDouble(), 0.0);
}

TEST(ByteBufferTest, WriteReadBool) {
    ByteBuffer buf;
    buf.writeBool(true);
    buf.writeBool(false);
    EXPECT_TRUE(buf.readBool());
    EXPECT_FALSE(buf.readBool());
}

TEST(ByteBufferTest, WriteReadUTF) {
    ByteBuffer buf;
    buf.writeUTF("Hello, World!");
    EXPECT_EQ(buf.readUTF(), "Hello, World!");
}

TEST(ByteBufferTest, WriteReadEmptyUTF) {
    ByteBuffer buf;
    buf.writeUTF("");
    EXPECT_EQ(buf.readUTF(), "");
}

TEST(ByteBufferTest, WriteReadUTFWithUnicode) {
    ByteBuffer buf;
    buf.writeUTF("Minecraft \u00a7aAlpha");
    EXPECT_EQ(buf.readUTF(), "Minecraft \u00a7aAlpha");
}

TEST(ByteBufferTest, WriteReadBytes) {
    ByteBuffer buf;
    std::vector<uint8_t> d = {1, 2, 3, 4, 5};
    buf.writeBytes(d);
    auto r = buf.readBytes(5);
    ASSERT_EQ(r.size(), 5);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(r[i], d[i]);
}

TEST(ByteBufferTest, BigEndianShort) {
    ByteBuffer buf;
    buf.writeShort(0x1234);
    ASSERT_GE(buf.data.size(), 2);
    EXPECT_EQ(buf.data[0], 0x12);
    EXPECT_EQ(buf.data[1], 0x34);
}

TEST(ByteBufferTest, BigEndianInt) {
    ByteBuffer buf;
    buf.writeInt(0x12345678);
    EXPECT_EQ(buf.data[0], 0x12);
    EXPECT_EQ(buf.data[1], 0x34);
    EXPECT_EQ(buf.data[2], 0x56);
    EXPECT_EQ(buf.data[3], 0x78);
}

TEST(ByteBufferTest, BufferUnderflow) {
    ByteBuffer buf;
    buf.writeByte(42);
    buf.readByte();
    EXPECT_THROW(buf.readInt(), std::runtime_error);
}

TEST(ByteBufferTest, Remaining) {
    ByteBuffer buf;
    buf.writeShort(100);
    buf.writeInt(200);
    EXPECT_EQ(buf.remaining(), 6);
    buf.readShort();
    EXPECT_EQ(buf.remaining(), 4);
}

TEST(ByteBufferTest, ReadWriteSpan) {
    ByteBuffer buf;
    uint8_t arr[] = {10, 20, 30, 40};
    buf.writeBytes(arr, 4);
    uint8_t out[4] = {};
    buf.readBytes(out, 4);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(out[i], arr[i]);
}
