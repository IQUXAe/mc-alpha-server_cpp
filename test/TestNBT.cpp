#include <gtest/gtest.h>
#include "core/NBT.h"
#include "network/Packet.h"

TEST(NBTTest, ByteRoundTrip) {
    NBTByte tag(42);
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_Byte);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(read->getType(), NBTTagType::TAG_Byte);
    EXPECT_EQ(std::dynamic_pointer_cast<NBTByte>(read)->value, 42);
}

TEST(NBTTest, ShortRoundTrip) {
    NBTShort tag(-32000);
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_Short);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(std::dynamic_pointer_cast<NBTShort>(read)->value, -32000);
}

TEST(NBTTest, IntRoundTrip) {
    NBTInt tag(1234567890);
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_Int);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(std::dynamic_pointer_cast<NBTInt>(read)->value, 1234567890);
}

TEST(NBTTest, LongRoundTrip) {
    NBTLong tag(987654321012345678LL);
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_Long);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(std::dynamic_pointer_cast<NBTLong>(read)->value, 987654321012345678LL);
}

TEST(NBTTest, FloatRoundTrip) {
    NBTFloat tag(3.14159f);
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_Float);
    ASSERT_NE(read, nullptr);
    EXPECT_FLOAT_EQ(std::dynamic_pointer_cast<NBTFloat>(read)->value, 3.14159f);
}

TEST(NBTTest, DoubleRoundTrip) {
    NBTDouble tag(2.718281828459045);
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_Double);
    ASSERT_NE(read, nullptr);
    EXPECT_DOUBLE_EQ(std::dynamic_pointer_cast<NBTDouble>(read)->value, 2.718281828459045);
}

TEST(NBTTest, StringRoundTrip) {
    NBTString tag("Hello NBT!");
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_String);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(std::dynamic_pointer_cast<NBTString>(read)->value, "Hello NBT!");
}

TEST(NBTTest, EmptyStringRoundTrip) {
    NBTString tag("");
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_String);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(std::dynamic_pointer_cast<NBTString>(read)->value, "");
}

TEST(NBTTest, ByteArrayRoundTrip) {
    std::vector<uint8_t> d = {1, 2, 3, 255, 0, 128};
    NBTByteArray tag(d);
    ByteBuffer buf;
    tag.writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_ByteArray);
    ASSERT_NE(read, nullptr);
    auto r = std::dynamic_pointer_cast<NBTByteArray>(read)->value;
    ASSERT_EQ(r.size(), d.size());
    for (size_t i = 0; i < d.size(); ++i) EXPECT_EQ(r[i], d[i]);
}

TEST(NBTTest, CompoundRoundTrip) {
    auto outer = std::make_shared<NBTCompound>();
    outer->setByte("byteVal", 127);
    outer->setShort("shortVal", -30000);
    outer->setInt("intVal", 999999);
    outer->setString("strVal", "hello");
    outer->setFloat("floatVal", 1.5f);
    outer->setDouble("doubleVal", 2.5);

    ByteBuffer buf;
    outer->writeRoot(buf, "Root");
    buf.readPos = 0;

    auto readBack = NBTCompound::readRoot(buf);
    ASSERT_NE(readBack, nullptr);
    EXPECT_EQ(readBack->getByte("byteVal"), 127);
    EXPECT_EQ(readBack->getShort("shortVal"), -30000);
    EXPECT_EQ(readBack->getInt("intVal"), 999999);
    EXPECT_EQ(readBack->getString("strVal"), "hello");
    EXPECT_FLOAT_EQ(readBack->getFloat("floatVal"), 1.5f);
    EXPECT_DOUBLE_EQ(readBack->getDouble("doubleVal"), 2.5);
}

TEST(NBTTest, CompoundMissingKey) {
    auto comp = std::make_shared<NBTCompound>();
    EXPECT_EQ(comp->getInt("nonexistent"), 0);
    EXPECT_EQ(comp->getString("nonexistent"), "");
    EXPECT_EQ(comp->getByte("nonexistent"), 0);
}

TEST(NBTTest, ListRoundTrip) {
    auto list = std::make_shared<NBTList>();
    list->tagType = NBTTagType::TAG_Int;
    list->tags.push_back(std::make_shared<NBTInt>(10));
    list->tags.push_back(std::make_shared<NBTInt>(20));
    list->tags.push_back(std::make_shared<NBTInt>(30));

    ByteBuffer buf;
    list->writeContents(buf);
    buf.readPos = 0;

    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_List);
    ASSERT_NE(read, nullptr);
    auto rl = std::dynamic_pointer_cast<NBTList>(read);
    ASSERT_NE(rl, nullptr);
    ASSERT_EQ(rl->tags.size(), 3);
    EXPECT_EQ(std::dynamic_pointer_cast<NBTInt>(rl->tags[0])->value, 10);
    EXPECT_EQ(std::dynamic_pointer_cast<NBTInt>(rl->tags[2])->value, 30);
}

TEST(NBTTest, EmptyListRoundTrip) {
    auto list = std::make_shared<NBTList>();
    ByteBuffer buf;
    list->writeContents(buf);
    buf.readPos = 0;
    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_List);
    ASSERT_NE(read, nullptr);
    auto rl = std::dynamic_pointer_cast<NBTList>(read);
    EXPECT_TRUE(rl->tags.empty());
}

TEST(NBTTest, NestedCompound) {
    auto inner = std::make_shared<NBTCompound>();
    inner->setString("name", "inner");
    inner->setInt("value", 42);

    auto outer = std::make_shared<NBTCompound>();
    outer->setCompound("child", inner);

    ByteBuffer buf;
    outer->writeRoot(buf, "Root");
    buf.readPos = 0;

    auto readBack = NBTCompound::readRoot(buf);
    ASSERT_NE(readBack, nullptr);
    auto child = readBack->getCompound("child");
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->getString("name"), "inner");
    EXPECT_EQ(child->getInt("value"), 42);
}

TEST(NBTTest, CompoundSetBoolean) {
    auto comp = std::make_shared<NBTCompound>();
    comp->setBoolean("flag", true);
    comp->setBoolean("noflag", false);
    EXPECT_EQ(comp->getByte("flag"), 1);
    EXPECT_EQ(comp->getByte("noflag"), 0);
}

TEST(NBTTest, CompoundSetByteArray) {
    auto comp = std::make_shared<NBTCompound>();
    std::vector<uint8_t> d = {0xDE, 0xAD, 0xBE, 0xEF};
    comp->setByteArray("data", d);

    ByteBuffer buf;
    comp->writeRoot(buf, "");
    buf.readPos = 0;

    auto readBack = NBTCompound::readRoot(buf);
    ASSERT_NE(readBack, nullptr);
    auto r = readBack->getByteArray("data");
    ASSERT_EQ(r.size(), 4);
    for (size_t i = 0; i < 4; ++i) EXPECT_EQ(r[i], d[i]);
}

TEST(NBTTest, ListOfCompounds) {
    auto list = std::make_shared<NBTList>();
    list->tagType = NBTTagType::TAG_Compound;

    for (int i = 0; i < 3; ++i) {
        auto c = std::make_shared<NBTCompound>();
        c->setInt("index", i);
        list->tags.push_back(c);
    }

    ByteBuffer buf;
    list->writeContents(buf);
    buf.readPos = 0;

    auto read = NBTCompound::readTag(buf, NBTTagType::TAG_List);
    auto rl = std::dynamic_pointer_cast<NBTList>(read);
    ASSERT_NE(rl, nullptr);
    ASSERT_EQ(rl->tags.size(), 3);
    for (int i = 0; i < 3; ++i) {
        auto rc = std::dynamic_pointer_cast<NBTCompound>(rl->tags[i]);
        ASSERT_NE(rc, nullptr);
        EXPECT_EQ(rc->getInt("index"), i);
    }
}

TEST(NBTTest, IteratorInvalidationFix) {
    auto comp = std::make_shared<NBTCompound>();
    comp->setString("Items", "some_value");
    
    auto it = comp->tags.find("Items");
    ASSERT_NE(it, comp->tags.end());
    
    auto end_it = comp->tags.end();
    EXPECT_NE(it, end_it);
    EXPECT_EQ(it->first, "Items");
    EXPECT_EQ(std::dynamic_pointer_cast<NBTString>(it->second)->value, "some_value");
}

