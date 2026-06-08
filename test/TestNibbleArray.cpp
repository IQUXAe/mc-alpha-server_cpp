#include <gtest/gtest.h>
#include "core/NibbleArray.h"

TEST(NibbleArrayTest, DefaultConstructorEmpty) {
    NibbleArray arr;
    EXPECT_FALSE(arr.isValid());
}

TEST(NibbleArrayTest, SizeConstructor) {
    NibbleArray arr(32768);
    EXPECT_TRUE(arr.isValid());
    EXPECT_EQ(arr.size, 16384);
}

TEST(NibbleArrayTest, SetAndGet) {
    NibbleArray arr(32768);
    arr.setNibble(5, 64, 7, 0xA);
    EXPECT_EQ(arr.getNibble(5, 64, 7), 0xA);
}

TEST(NibbleArrayTest, SetAndGetAllZeros) {
    NibbleArray arr(32768);
    EXPECT_EQ(arr.getNibble(0, 0, 0), 0);
    EXPECT_EQ(arr.getNibble(15, 127, 15), 0);
}

TEST(NibbleArrayTest, SetAndGetMaxValues) {
    NibbleArray arr(32768);
    arr.setNibble(15, 127, 15, 0xF);
    EXPECT_EQ(arr.getNibble(15, 127, 15), 0xF);
}

TEST(NibbleArrayTest, PreserveOtherNibblesInByte) {
    NibbleArray arr(32768);
    arr.setNibble(0, 0, 0, 0xA);
    arr.setNibble(0, 1, 0, 0xB);
    EXPECT_EQ(arr.getNibble(0, 0, 0), 0xA);
    EXPECT_EQ(arr.getNibble(0, 1, 0), 0xB);
}

TEST(NibbleArrayTest, IndexFormulaMapping) {
    // Verify index: (x << 11) | (z << 7) | y
    // Each nibble pair shares a byte. Even y = low nibble, odd y = high nibble
    NibbleArray arr(32768);
    arr.setNibble(1, 0, 2, 0xC);
    EXPECT_EQ(arr.getNibble(1, 0, 2), 0xC);
}

TEST(NibbleArrayTest, DataVectorConstructor) {
    std::vector<uint8_t> d(16384, 0xAB);
    NibbleArray arr(std::move(d));
    EXPECT_TRUE(arr.isValid());
}

TEST(NibbleArrayTest, View) {
    NibbleArray arr(32768);
    auto view = arr.view();
    EXPECT_EQ(view.size(), 16384);
}
