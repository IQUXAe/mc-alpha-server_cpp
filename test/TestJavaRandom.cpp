#include <gtest/gtest.h>
#include "core/JavaRandom.h"

TEST(JavaRandomTest, DeterministicSequence) {
    JavaRandom rng(12345);
    int a = rng.nextInt();
    int b = rng.nextInt();
    int c = rng.nextInt();
    // Same seed always produces same sequence
    JavaRandom rng2(12345);
    EXPECT_EQ(rng2.nextInt(), a);
    EXPECT_EQ(rng2.nextInt(), b);
    EXPECT_EQ(rng2.nextInt(), c);
}

TEST(JavaRandomTest, SeedZero) {
    JavaRandom rng(0);
    int a = rng.nextInt();
    int b = rng.nextInt();
    // Same seed always produces same sequence
    JavaRandom rng2(0);
    EXPECT_EQ(rng2.nextInt(), a);
    EXPECT_EQ(rng2.nextInt(), b);
}

TEST(JavaRandomTest, NextIntBoundPowerOfTwo) {
    JavaRandom rng(42);
    for (int i = 0; i < 100; ++i) {
        int v = rng.nextInt(16);
        EXPECT_GE(v, 0);
        EXPECT_LT(v, 16);
    }
}

TEST(JavaRandomTest, NextIntBoundNonPowerOfTwo) {
    JavaRandom rng(99);
    for (int i = 0; i < 100; ++i) {
        int v = rng.nextInt(10);
        EXPECT_GE(v, 0);
        EXPECT_LT(v, 10);
    }
}

TEST(JavaRandomTest, NextLongDeterministic) {
    JavaRandom rng(12345);
    JavaRandom rng2(12345);
    EXPECT_EQ(rng.nextLong(), rng2.nextLong());
}

TEST(JavaRandomTest, NextDouble) {
    JavaRandom rng(12345);
    double d = rng.nextDouble();
    EXPECT_GE(d, 0.0);
    EXPECT_LT(d, 1.0);
    JavaRandom rng2(12345);
    EXPECT_DOUBLE_EQ(rng2.nextDouble(), d);
}

TEST(JavaRandomTest, NextDoubleRange) {
    JavaRandom rng(42);
    for (int i = 0; i < 1000; ++i) {
        double d = rng.nextDouble();
        EXPECT_GE(d, 0.0);
        EXPECT_LT(d, 1.0);
    }
}

TEST(JavaRandomTest, NextFloat) {
    JavaRandom rng(12345);
    float f = rng.nextFloat();
    EXPECT_GE(f, 0.0f);
    EXPECT_LT(f, 1.0f);
}

TEST(JavaRandomTest, AdvanceMatchesMultipleNext) {
    JavaRandom rng1(777);
    JavaRandom rng2(777);

    rng1.advance(10);
    int advanced = rng1.nextInt();

    for (int i = 0; i < 10; ++i) rng2.nextInt();
    int manual = rng2.nextInt();

    EXPECT_EQ(advanced, manual);
}

TEST(JavaRandomTest, Advance4MatchesFourNext) {
    JavaRandom rng1(888);
    JavaRandom rng2(888);

    rng1.advance4();
    int advanced = rng1.nextInt();

    for (int i = 0; i < 4; ++i) rng2.nextInt();
    int manual = rng2.nextInt();

    EXPECT_EQ(advanced, manual);
}

TEST(JavaRandomTest, Advance6MatchesSixNext) {
    JavaRandom rng1(999);
    JavaRandom rng2(999);

    rng1.advance6();
    int advanced = rng1.nextInt();

    for (int i = 0; i < 6; ++i) rng2.nextInt();
    int manual = rng2.nextInt();

    EXPECT_EQ(advanced, manual);
}

TEST(JavaRandomTest, SetSeedResets) {
    JavaRandom rng(42);
    int first = rng.nextInt();
    rng.setSeed(42);
    int second = rng.nextInt();
    EXPECT_EQ(first, second);
}

TEST(JavaRandomTest, LargeAdvance) {
    JavaRandom rng1(123);
    JavaRandom rng2(123);

    rng1.advance(1000000);
    int advanced = rng1.nextInt();

    for (int i = 0; i < 1000000; ++i) rng2.nextInt();
    int manual = rng2.nextInt();

    EXPECT_EQ(advanced, manual);
}
