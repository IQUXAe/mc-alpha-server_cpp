#include <gtest/gtest.h>
#include "core/AxisAlignedBB.h"
#include "core/MathHelper.h"

class AxisAlignedBBTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        MathHelper::init();
    }
};

TEST_F(AxisAlignedBBTest, DefaultConstructor) {
    AxisAlignedBB bb;
    EXPECT_DOUBLE_EQ(bb.minX, 0.0);
    EXPECT_DOUBLE_EQ(bb.maxX, 0.0);
}

TEST_F(AxisAlignedBBTest, ParameterConstructor) {
    AxisAlignedBB bb(0, 0, 0, 1, 1, 1);
    EXPECT_DOUBLE_EQ(bb.minX, 0.0);
    EXPECT_DOUBLE_EQ(bb.maxX, 1.0);
    EXPECT_DOUBLE_EQ(bb.minY, 0.0);
    EXPECT_DOUBLE_EQ(bb.maxY, 1.0);
}

TEST_F(AxisAlignedBBTest, IntersectsWith) {
    AxisAlignedBB a(0, 0, 0, 2, 2, 2);
    AxisAlignedBB b(1, 1, 1, 3, 3, 3);
    EXPECT_TRUE(a.intersectsWith(b));
}

TEST_F(AxisAlignedBBTest, NotIntersects) {
    AxisAlignedBB a(0, 0, 0, 1, 1, 1);
    AxisAlignedBB b(2, 2, 2, 3, 3, 3);
    EXPECT_FALSE(a.intersectsWith(b));
}

TEST_F(AxisAlignedBBTest, TouchingEdgesNotIntersect) {
    AxisAlignedBB a(0, 0, 0, 1, 1, 1);
    AxisAlignedBB b(1, 0, 0, 2, 1, 1);
    EXPECT_FALSE(a.intersectsWith(b));
}

TEST_F(AxisAlignedBBTest, Expand) {
    AxisAlignedBB bb(0, 0, 0, 1, 1, 1);
    AxisAlignedBB e = bb.expand(1, 1, 1);
    EXPECT_DOUBLE_EQ(e.minX, -1.0);
    EXPECT_DOUBLE_EQ(e.maxX, 2.0);
    EXPECT_DOUBLE_EQ(e.minY, -1.0);
    EXPECT_DOUBLE_EQ(e.maxY, 2.0);
}

TEST_F(AxisAlignedBBTest, GetOffsetBoundingBox) {
    AxisAlignedBB bb(0, 0, 0, 1, 1, 1);
    AxisAlignedBB o = bb.getOffsetBoundingBox(5, 5, 5);
    EXPECT_DOUBLE_EQ(o.minX, 5.0);
    EXPECT_DOUBLE_EQ(o.maxX, 6.0);
}

TEST_F(AxisAlignedBBTest, CalculateXOffsetBlockRightOfEntity) {
    // Block at [2,3], Entity at [0,1], entity moving right dx=3.0
    // Calling block.calculateXOffset(entity, dx)
    AxisAlignedBB block(2, 0, 0, 3, 1, 1);
    AxisAlignedBB entity(0, 0, 0, 1, 1, 1);
    double dx = block.calculateXOffset(entity, 3.0);
    // Block's minX=2, entity's maxX=1, gap=1, so movement clamped to 1
    EXPECT_NEAR(dx, 1.0, 1e-6);
}

TEST_F(AxisAlignedBBTest, CalculateXOffsetBlockLeftOfEntity) {
    // Block at [-2,-1], Entity at [0,1], entity moving left dx=-3.0
    // Calling block.calculateXOffset(entity, dx)
    AxisAlignedBB block(-2, 0, 0, -1, 1, 1);
    AxisAlignedBB entity(0, 0, 0, 1, 1, 1);
    double dx = block.calculateXOffset(entity, -3.0);
    // Block's maxX=-1, entity's minX=0, gap=-1, so movement clamped from -3 to -1
    EXPECT_NEAR(dx, -1.0, 1e-6);
}

TEST_F(AxisAlignedBBTest, CalculateXOffsetNoCollision) {
    AxisAlignedBB block(5, 0, 0, 6, 1, 1);
    AxisAlignedBB entity(0, 0, 0, 1, 1, 1);
    double dx = block.calculateXOffset(entity, 3.0);
    // No Y/Z overlap check fails in calculateXOffset since block.maxY=1>0 and entity.maxY=1>0
    // Actually they do overlap in Y/Z, but block.minX=5, entity.maxX=1, so gap=4
    EXPECT_NEAR(dx, 3.0, 1e-6);
}

TEST_F(AxisAlignedBBTest, CalculateYOffsetBlockAboveEntity) {
    AxisAlignedBB block(0, 2, 0, 1, 3, 1);
    AxisAlignedBB entity(0, 0, 0, 1, 1, 1);
    double dy = block.calculateYOffset(entity, 3.0);
    EXPECT_NEAR(dy, 1.0, 1e-6);
}

TEST_F(AxisAlignedBBTest, CalculateYOffsetBlockBelowEntity) {
    AxisAlignedBB block(0, -2, 0, 1, -1, 1);
    AxisAlignedBB entity(0, 0, 0, 1, 1, 1);
    double dy = block.calculateYOffset(entity, -3.0);
    EXPECT_NEAR(dy, -1.0, 1e-6);
}

TEST_F(AxisAlignedBBTest, CalculateZOffsetBlockSouthOfEntity) {
    AxisAlignedBB block(0, 0, 2, 1, 1, 3);
    AxisAlignedBB entity(0, 0, 0, 1, 1, 1);
    double dz = block.calculateZOffset(entity, 3.0);
    EXPECT_NEAR(dz, 1.0, 1e-6);
}

TEST_F(AxisAlignedBBTest, NoCollisionXOffset) {
    AxisAlignedBB a(0, 0, 0, 1, 1, 1);
    AxisAlignedBB b(5, 5, 5, 6, 6, 6);
    double dx = a.calculateXOffset(b, 1.0);
    EXPECT_DOUBLE_EQ(dx, 1.0);
}

TEST_F(AxisAlignedBBTest, AddCoord) {
    AxisAlignedBB bb(0, 0, 0, 1, 1, 1);
    AxisAlignedBB a = bb.addCoord(-1, 2, 0);
    EXPECT_DOUBLE_EQ(a.minX, -1.0);
    EXPECT_DOUBLE_EQ(a.maxX, 1.0);
    EXPECT_DOUBLE_EQ(a.minY, 0.0);
    EXPECT_DOUBLE_EQ(a.maxY, 3.0);
}

TEST_F(AxisAlignedBBTest, ShrinkPositiveReducesMax) {
    AxisAlignedBB bb(0, 0, 0, 2, 2, 2);
    AxisAlignedBB s = bb.shrink(0.5, 0.5, 0.5);
    // For positive values, shrink reduces max side
    EXPECT_DOUBLE_EQ(s.minX, 0.0);
    EXPECT_DOUBLE_EQ(s.maxX, 1.5);
}

TEST_F(AxisAlignedBBTest, ShrinkNegativeIncreasesMin) {
    AxisAlignedBB bb(0, 0, 0, 2, 2, 2);
    AxisAlignedBB s = bb.shrink(-0.5, -0.5, -0.5);
    // For negative values, shrink increases min side (pushes inward)
    EXPECT_DOUBLE_EQ(s.minX, 0.5);
    EXPECT_DOUBLE_EQ(s.maxX, 2.0);
}

TEST_F(AxisAlignedBBTest, Copy) {
    AxisAlignedBB bb(1, 2, 3, 4, 5, 6);
    AxisAlignedBB c = bb.copy();
    EXPECT_DOUBLE_EQ(c.minX, 1.0);
    EXPECT_DOUBLE_EQ(c.maxZ, 6.0);
}

TEST_F(AxisAlignedBBTest, SetBB) {
    AxisAlignedBB bb;
    AxisAlignedBB other(1, 2, 3, 4, 5, 6);
    bb.setBB(other);
    EXPECT_DOUBLE_EQ(bb.minX, 1.0);
    EXPECT_DOUBLE_EQ(bb.maxZ, 6.0);
}

TEST_F(AxisAlignedBBTest, Offset) {
    AxisAlignedBB bb(0, 0, 0, 1, 1, 1);
    bb.offset(10, 20, 30);
    EXPECT_DOUBLE_EQ(bb.minX, 10.0);
    EXPECT_DOUBLE_EQ(bb.minY, 20.0);
    EXPECT_DOUBLE_EQ(bb.minZ, 30.0);
    EXPECT_DOUBLE_EQ(bb.maxX, 11.0);
    EXPECT_DOUBLE_EQ(bb.maxY, 21.0);
    EXPECT_DOUBLE_EQ(bb.maxZ, 31.0);
}

TEST_F(AxisAlignedBBTest, ClipRayHit) {
    AxisAlignedBB bb(0, 0, 0, 1, 1, 1);
    Vec3D from(-1, 0.5, 0.5);
    Vec3D to(2, 0.5, 0.5);
    auto r = bb.clip(from, to);
    ASSERT_TRUE(r.has_value());
    // Face map: index 0 (minX) -> 4, index 1 (maxX) -> 5
    // Ray enters at minX=0 -> face index 0 -> side = 4
    EXPECT_EQ(r->sideHit, 4);
    EXPECT_NEAR(r->hitVec.xCoord, 0.0, 1e-6);
}

TEST_F(AxisAlignedBBTest, ClipRayMiss) {
    AxisAlignedBB bb(0, 0, 0, 1, 1, 1);
    Vec3D from(-1, 5, 5);
    Vec3D to(2, 5, 5);
    auto r = bb.clip(from, to);
    EXPECT_FALSE(r.has_value());
}

TEST_F(AxisAlignedBBTest, GetBoundingBox) {
    AxisAlignedBB bb = AxisAlignedBB::getBoundingBox(1, 2, 3, 4, 5, 6);
    EXPECT_DOUBLE_EQ(bb.minX, 1.0);
    EXPECT_DOUBLE_EQ(bb.maxY, 5.0);
}

TEST_F(AxisAlignedBBTest, SetBounds) {
    AxisAlignedBB bb;
    bb.setBounds(1, 2, 3, 4, 5, 6);
    EXPECT_DOUBLE_EQ(bb.minX, 1.0);
    EXPECT_DOUBLE_EQ(bb.maxX, 4.0);
}
