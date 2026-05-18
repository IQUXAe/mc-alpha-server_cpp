#include <gtest/gtest.h>
#include "core/Vec3D.h"

TEST(Vec3DTest, DefaultConstructor) {
    Vec3D v;
    EXPECT_DOUBLE_EQ(v.xCoord, 0.0);
    EXPECT_DOUBLE_EQ(v.yCoord, 0.0);
    EXPECT_DOUBLE_EQ(v.zCoord, 0.0);
}

TEST(Vec3DTest, ParameterConstructor) {
    Vec3D v(1.0, 2.0, 3.0);
    EXPECT_DOUBLE_EQ(v.xCoord, 1.0);
    EXPECT_DOUBLE_EQ(v.yCoord, 2.0);
    EXPECT_DOUBLE_EQ(v.zCoord, 3.0);
}

TEST(Vec3DTest, NegativeZeroNormalization) {
    Vec3D v(-0.0, -0.0, -0.0);
    EXPECT_DOUBLE_EQ(v.xCoord, 0.0);
    EXPECT_DOUBLE_EQ(v.yCoord, 0.0);
    EXPECT_DOUBLE_EQ(v.zCoord, 0.0);
}

TEST(Vec3DTest, AddVector) {
    Vec3D v(1.0, 2.0, 3.0);
    Vec3D r = v.addVector(10.0, 20.0, 30.0);
    EXPECT_DOUBLE_EQ(r.xCoord, 11.0);
    EXPECT_DOUBLE_EQ(r.yCoord, 22.0);
    EXPECT_DOUBLE_EQ(r.zCoord, 33.0);
}

TEST(Vec3DTest, DistanceTo) {
    Vec3D a(0.0, 0.0, 0.0);
    Vec3D b(3.0, 4.0, 0.0);
    EXPECT_DOUBLE_EQ(a.distanceTo(b), 5.0);
}

TEST(Vec3DTest, SquareDistanceTo) {
    Vec3D a(0.0, 0.0, 0.0);
    Vec3D b(3.0, 4.0, 0.0);
    EXPECT_DOUBLE_EQ(a.squareDistanceTo(b), 25.0);
}

TEST(Vec3DTest, NormalizeZero) {
    Vec3D v;
    Vec3D n = v.normalize();
    EXPECT_DOUBLE_EQ(n.xCoord, 0.0);
    EXPECT_DOUBLE_EQ(n.yCoord, 0.0);
    EXPECT_DOUBLE_EQ(n.zCoord, 0.0);
}

TEST(Vec3DTest, NormalizeUnit) {
    Vec3D v(3.0, 0.0, 0.0);
    Vec3D n = v.normalize();
    EXPECT_NEAR(n.xCoord, 1.0, 1e-6);
    EXPECT_NEAR(n.lengthVector(), 1.0, 1e-6);
}

TEST(Vec3DTest, LengthVector) {
    Vec3D v(3.0, 4.0, 0.0);
    EXPECT_DOUBLE_EQ(v.lengthVector(), 5.0);
}

TEST(Vec3DTest, GetIntermediateWithXValue) {
    Vec3D from(0, 0, 0);
    Vec3D to(10, 10, 10);
    auto r = from.getIntermediateWithXValue(to, 5.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->xCoord, 5.0, 1e-6);
    EXPECT_NEAR(r->yCoord, 5.0, 1e-6);
    EXPECT_NEAR(r->zCoord, 5.0, 1e-6);
}

TEST(Vec3DTest, GetIntermediateOutOfRange) {
    Vec3D from(0, 0, 0);
    Vec3D to(10, 10, 10);
    EXPECT_FALSE(from.getIntermediateWithXValue(to, -1.0).has_value());
    EXPECT_FALSE(from.getIntermediateWithXValue(to, 11.0).has_value());
}

TEST(Vec3DTest, GetIntermediateWithYValue) {
    Vec3D from(0, 0, 0);
    Vec3D to(10, 10, 10);
    auto r = from.getIntermediateWithYValue(to, 5.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->yCoord, 5.0, 1e-6);
}

TEST(Vec3DTest, GetIntermediateWithZValue) {
    Vec3D from(0, 0, 0);
    Vec3D to(10, 10, 10);
    auto r = from.getIntermediateWithZValue(to, 5.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->zCoord, 5.0, 1e-6);
}

TEST(Vec3DTest, SetComponents) {
    Vec3D v;
    v.setComponents(7.0, 8.0, 9.0);
    EXPECT_DOUBLE_EQ(v.xCoord, 7.0);
    EXPECT_DOUBLE_EQ(v.yCoord, 8.0);
    EXPECT_DOUBLE_EQ(v.zCoord, 9.0);
}

TEST(Vec3DTest, CreateVectorHelper) {
    Vec3D v = Vec3D::createVectorHelper(1.0, 2.0, 3.0);
    EXPECT_DOUBLE_EQ(v.xCoord, 1.0);
    EXPECT_DOUBLE_EQ(v.yCoord, 2.0);
    EXPECT_DOUBLE_EQ(v.zCoord, 3.0);
}

TEST(Vec3DTest, SquareDistanceToCoords) {
    Vec3D v(1, 2, 3);
    EXPECT_DOUBLE_EQ(v.squareDistanceTo(4.0, 6.0, 3.0), 25.0);
}
