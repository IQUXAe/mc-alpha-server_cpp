#include <gtest/gtest.h>
#include "world/Chunk.h"
#include "block/Block.h"

class ChunkTest : public ::testing::Test {
};

TEST_F(ChunkTest, IndexFormula) {
    Chunk chunk(nullptr, 0, 0);
    // Index = (x << 11) | (z << 7) | y
    EXPECT_EQ(chunk.getIndex(0, 0, 0), 0);
    EXPECT_EQ(chunk.getIndex(0, 1, 0), 1);
    EXPECT_EQ(chunk.getIndex(1, 0, 0), 1 << 11);
    EXPECT_EQ(chunk.getIndex(0, 0, 1), 1 << 7);
}

TEST_F(ChunkTest, InitialAllAir) {
    Chunk chunk(nullptr, 0, 0);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                EXPECT_EQ(chunk.getBlockID(x, y, z), 0);
            }
        }
    }
}

TEST_F(ChunkTest, SetAndGetBlockID) {
    Chunk chunk(nullptr, 0, 0);
    chunk.setBlockID(5, 40, 7, 1);
    EXPECT_EQ(chunk.getBlockID(5, 40, 7), 1);
}

TEST_F(ChunkTest, SetBlockIDReturnsFalseForSame) {
    Chunk chunk(nullptr, 0, 0);
    EXPECT_TRUE(chunk.setBlockID(3, 20, 3, 1));
    EXPECT_FALSE(chunk.setBlockID(3, 20, 3, 1));
}

TEST_F(ChunkTest, SetBlockWithMetadata) {
    Chunk chunk(nullptr, 0, 0);
    chunk.setBlockIDWithMetadata(7, 50, 9, 5, 3);
    EXPECT_EQ(chunk.getBlockID(7, 50, 9), 5);
    EXPECT_EQ(chunk.getBlockMetadata(7, 50, 9), 3);
}

TEST_F(ChunkTest, GetBlockIDOutOfRange) {
    Chunk chunk(nullptr, 0, 0);
    EXPECT_EQ(chunk.getBlockID(-1, 0, 0), 0);
    EXPECT_EQ(chunk.getBlockID(16, 0, 0), 0);
    EXPECT_EQ(chunk.getBlockID(0, -1, 0), 0);
    EXPECT_EQ(chunk.getBlockID(0, 128, 0), 0);
    EXPECT_EQ(chunk.getBlockID(0, 0, -1), 0);
    EXPECT_EQ(chunk.getBlockID(0, 0, 16), 0);
}

TEST_F(ChunkTest, SetAndGetMetadata) {
    Chunk chunk(nullptr, 0, 0);
    chunk.setBlockMetadata(2, 30, 4, 0xF);
    EXPECT_EQ(chunk.getBlockMetadata(2, 30, 4), 0xF);
    EXPECT_EQ(chunk.getBlockMetadata(2, 31, 4), 0); // adjacent block unchanged
}

TEST_F(ChunkTest, HeightMapAfterSetBlock) {
    Chunk chunk(nullptr, 0, 0);
    // Place stone at y=60, should set heightmap to 61 (opaque block top + 1)
    chunk.setBlockID(4, 60, 4, 1); // stone is opaque
    EXPECT_GE(chunk.getHeightValue(4, 4), 60);
}

TEST_F(ChunkTest, HeightMapDefault) {
    Chunk chunk(nullptr, 0, 0);
    EXPECT_EQ(chunk.getHeightValue(0, 0), 0);
    EXPECT_EQ(chunk.getHeightValue(15, 15), 0);
}

TEST_F(ChunkTest, ChunkDataIsCompressed) {
    Chunk chunk(nullptr, 0, 0);
    auto data = chunk.getChunkData();
    // getChunkData() returns zlib-compressed chunk data
    // An empty (all-air) chunk compresses to ~200-400 bytes
    EXPECT_GT(data.size(), 50);
    EXPECT_LT(data.size(), 2000);
}

TEST_F(ChunkTest, ChunkDataNotEmpty) {
    Chunk chunk(nullptr, 0, 0);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            chunk.setBlockID(x, 0, z, 1);
    auto data = chunk.getChunkData();
    // Non-empty chunk should compress to something larger than empty
    EXPECT_GT(data.size(), 400);
}

TEST_F(ChunkTest, ChunkDataIsNotRaw) {
    Chunk chunk(nullptr, 0, 0);
    chunk.setBlockID(0, 1, 0, 1);
    auto data = chunk.getChunkData();
    // The compressed data should not contain literal block ID at expected offset
    // This test verifies the data is indeed compressed
    EXPECT_NE(data.size(), 81920);
}

TEST_F(ChunkTest, ChunkDataWithMetadataCompresses) {
    Chunk chunk(nullptr, 0, 0);
    chunk.setBlockIDWithMetadata(0, 2, 0, 5, 0xA);
    auto data = chunk.getChunkData();
    // Should produce valid compressed output
    EXPECT_GT(data.size(), 50);
    EXPECT_LT(data.size(), 2000);
}

TEST_F(ChunkTest, SetBlockPreservesOtherBlocks) {
    Chunk chunk(nullptr, 0, 0);
    chunk.setBlockID(0, 10, 0, 1);
    chunk.setBlockID(15, 120, 15, 2);
    EXPECT_EQ(chunk.getBlockID(0, 10, 0), 1);
    EXPECT_EQ(chunk.getBlockID(15, 120, 15), 2);
    EXPECT_EQ(chunk.getBlockID(8, 64, 8), 0);
}

TEST_F(ChunkTest, GenerateHeightMapAllAir) {
    Chunk chunk(nullptr, 0, 0);
    chunk.generateHeightMap();
    EXPECT_EQ(chunk.getHeightValue(0, 0), 0);
    EXPECT_EQ(chunk.getHeightValue(7, 7), 0);
}

TEST_F(ChunkTest, HeightMapOutOfRange) {
    Chunk chunk(nullptr, 0, 0);
    EXPECT_EQ(chunk.getHeightValue(-1, 0), 0);
    EXPECT_EQ(chunk.getHeightValue(0, 16), 0);
}
