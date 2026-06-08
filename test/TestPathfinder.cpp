#include <gtest/gtest.h>
#define private public
#include "world/World.h"
#include "world/path/Pathfinder.h"
#include "MinecraftServer.h"
#include <filesystem>

TEST(PathfinderTest, BasicAStarPathfinding) {
    MinecraftServer server;
    std::string testWorldPath = "test_pathfinder_world";
    std::filesystem::remove_all(testWorldPath);

    {
        World world(&server, testWorldPath, 42ULL);

        // Pre-generate spawn chunk or chunk (0, 0)
        Chunk* chunk = world.getChunk(0, 0, true);
        ASSERT_NE(chunk, nullptr);

        // Build a flat floor at y = 63 out of stone (ID 1)
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                world.setBlock(x, 63, z, 1);
                // Clear air above it
                for (int y = 64; y < 70; ++y) {
                    world.setBlock(x, y, z, 0);
                }
            }
        }

        // Create a test entity (similar to a zombie, width 0.6, height 1.8)
        Entity entity;
        entity.width = 0.6f;
        entity.height = 1.8f;
        entity.setPosition(2.5, 64.0, 2.5);

        // Pathfinder instance
        Pathfinder pathfinder(world);

        std::cout << "Starting pathfind 1..." << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();
        auto path = pathfinder.createEntityPathTo(entity, 12.5, 64.0, 12.5, 30.0f);
        auto end_time = std::chrono::high_resolution_clock::now();
        std::cout << "Pathfind 1 completed in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count()
                  << " ms." << std::endl;

        ASSERT_NE(path, nullptr);
        EXPECT_FALSE(path->isFinished());

        // Check first position
        auto posOpt = path->getPosition(entity);
        ASSERT_TRUE(posOpt.has_value());
        
        // Target path point
        EXPECT_EQ(path->points_.back().xCoord, 12);
        EXPECT_EQ(path->points_.back().yCoord, 64);
        EXPECT_EQ(path->points_.back().zCoord, 12);

        // Build an obstacle course: a 3-block high wall in the middle
        // from (8, 64, 0) to (8, 64, 10)
        for (int z = 0; z <= 10; ++z) {
            world.setBlock(8, 64, z, 1);
            world.setBlock(8, 65, z, 1);
            world.setBlock(8, 66, z, 1);
        }

        // Pathfind again from (2.5, 64.0, 2.5) to (12.5, 64.0, 2.5)
        // With the wall in the way, it should navigate around it (via z > 10)
        std::cout << "Starting pathfind 2..." << std::endl;
        start_time = std::chrono::high_resolution_clock::now();
        auto path2 = pathfinder.createEntityPathTo(entity, 12.5, 64.0, 2.5, 30.0f);
        end_time = std::chrono::high_resolution_clock::now();
        std::cout << "Pathfind 2 completed in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count()
                  << " ms." << std::endl;
        ASSERT_NE(path2, nullptr);

        // Verify it avoids the wall (x = 8 must not have z <= 10 in the path points)
        for (const auto& pt : path2->points_) {
            if (pt.xCoord == 8) {
                EXPECT_GT(pt.zCoord, 10);
            }
        }
    }

    std::filesystem::remove_all(testWorldPath);
}
