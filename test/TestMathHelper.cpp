#include <gtest/gtest.h>
#include "core/MathHelper.h"

class MathHelperTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        MathHelper::init();
    }
};

TEST_F(MathHelperTest, SinZero) {
    EXPECT_NEAR(MathHelper::sin(0.0f), 0.0f, 1e-6f);
}

TEST_F(MathHelperTest, SinPiHalf) {
    EXPECT_NEAR(MathHelper::sin(3.14159265f / 2.0f), 1.0f, 1e-4f);
}

TEST_F(MathHelperTest, SinPi) {
    EXPECT_NEAR(MathHelper::sin(3.14159265f), 0.0f, 1e-4f);
}

TEST_F(MathHelperTest, CosZero) {
    EXPECT_NEAR(MathHelper::cos(0.0f), 1.0f, 1e-6f);
}

TEST_F(MathHelperTest, CosPi) {
    EXPECT_NEAR(MathHelper::cos(3.14159265f), -1.0f, 1e-4f);
}

TEST_F(MathHelperTest, CosPiHalf) {
    EXPECT_NEAR(MathHelper::cos(3.14159265f / 2.0f), 0.0f, 1e-4f);
}

TEST_F(MathHelperTest, FloorFloatPositive) {
    EXPECT_EQ(MathHelper::floor_float(3.7f), 3);
    EXPECT_EQ(MathHelper::floor_float(3.0f), 3);
}

TEST_F(MathHelperTest, FloorFloatNegative) {
    EXPECT_EQ(MathHelper::floor_float(-3.7f), -4);
    EXPECT_EQ(MathHelper::floor_float(-3.0f), -3);
}

TEST_F(MathHelperTest, FloorDoublePositive) {
    EXPECT_EQ(MathHelper::floor_double(3.7), 3);
    EXPECT_EQ(MathHelper::floor_double(3.0), 3);
}

TEST_F(MathHelperTest, FloorDoubleNegative) {
    EXPECT_EQ(MathHelper::floor_double(-3.7), -4);
    EXPECT_EQ(MathHelper::floor_double(-3.0), -3);
}

TEST_F(MathHelperTest, SqrtFloat) {
    EXPECT_NEAR(MathHelper::sqrt_float(25.0f), 5.0f, 1e-6f);
    EXPECT_NEAR(MathHelper::sqrt_float(2.0f), 1.41421356f, 1e-5f);
}

TEST_F(MathHelperTest, SqrtDouble) {
    EXPECT_NEAR(MathHelper::sqrt_double(25.0), 5.0, 1e-6);
    EXPECT_NEAR(MathHelper::sqrt_double(2.0), 1.414213562, 1e-6);
}

TEST_F(MathHelperTest, Abs) {
    EXPECT_EQ(MathHelper::abs(5.0f), 5.0f);
    EXPECT_EQ(MathHelper::abs(-5.0f), 5.0f);
    EXPECT_EQ(MathHelper::abs(0.0f), 0.0f);
}

TEST_F(MathHelperTest, AbsMax) {
    EXPECT_DOUBLE_EQ(MathHelper::abs_max(3.0, -5.0), 5.0);
    EXPECT_DOUBLE_EQ(MathHelper::abs_max(-2.0, 4.0), 4.0);
    EXPECT_DOUBLE_EQ(MathHelper::abs_max(0.0, 0.0), 0.0);
}

TEST_F(MathHelperTest, SinCosIdentity) {
    float angle = 1.234f;
    float s = MathHelper::sin(angle);
    float c = MathHelper::cos(angle);
    EXPECT_NEAR(s * s + c * c, 1.0f, 1e-4f);
}
