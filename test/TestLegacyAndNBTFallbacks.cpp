#include <gtest/gtest.h>
#define private public
#include "world/World.h"
#include "world/Chunk.h"
#include "core/InventoryPlayer.h"
#include "MinecraftServer.h"
#include "core/PropertyManager.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

// Test case 1: Tile Entity coordinates packing uniqueness
TEST(LegacyAndNBTTest, TileEntityCoordsPackingUniqueness) {
    MinecraftServer server;
    World world(&server, "test_temp_world_te_key", 12345ULL);

    // Coordinate points to test (large coords above 32768)
    int coordA = 50000;
    int coordB = 50000 + 32768; // in old version, (coordB & 0xFFFF) == (coordA & 0xFFFF) -> collision!

    uint64_t keyA = world.getTileEntityKey(coordA, 64, coordA);
    uint64_t keyB = world.getTileEntityKey(coordB, 64, coordA);
    uint64_t keyC = world.getTileEntityKey(coordA, 64, coordB);

    // In our new 28/8/28 bit allocation, keys must be completely unique
    EXPECT_NE(keyA, keyB);
    EXPECT_NE(keyA, keyC);
    EXPECT_NE(keyB, keyC);

    // Test coordinates around border (134 million limit)
    int coordLimit = 100000000; // 100 million
    uint64_t keyLimitA = world.getTileEntityKey(coordLimit, 128, -coordLimit);
    uint64_t keyLimitB = world.getTileEntityKey(-coordLimit, 128, coordLimit);
    EXPECT_NE(keyLimitA, keyLimitB);

    // Negative coordinates
    uint64_t keyNegA = world.getTileEntityKey(-50000, 64, -50000);
    uint64_t keyNegB = world.getTileEntityKey(-50000 + 32768, 64, -50000);
    EXPECT_NE(keyNegA, keyNegB);

    std::filesystem::remove_all("test_temp_world_te_key");
}

// Test case 2: Player Inventory default serialization (NBTList)
TEST(LegacyAndNBTTest, PlayerInventorySerialization) {
    // 2.1 Default writing as NBTList & loading back
    InventoryPlayer inventory(nullptr);

    // Place some test items
    inventory.mainInventory[0] = std::make_unique<ItemStack>(264, 32, 5); // Diamond, count=32, damage=5
    inventory.mainInventory[1] = std::make_unique<ItemStack>(263, 64, 0); // Coal, count=64, damage=0

    auto nbtList = std::make_shared<NBTList>();
    inventory.writeToNBT(nbtList);

    // Assert sequential format
    EXPECT_EQ(nbtList->tagType, NBTTagType::TAG_Compound);
    EXPECT_EQ(nbtList->tags.size(), 2);

    InventoryPlayer inventory2(nullptr);
    inventory2.readFromNBT(nbtList);

    ASSERT_NE(inventory2.mainInventory[0], nullptr);
    EXPECT_EQ(inventory2.mainInventory[0]->itemID, 264);
    EXPECT_EQ(inventory2.mainInventory[0]->stackSize, 32);
    EXPECT_EQ(inventory2.mainInventory[0]->itemDamage, 5);

    ASSERT_NE(inventory2.mainInventory[1], nullptr);
    EXPECT_EQ(inventory2.mainInventory[1]->itemID, 263);
    EXPECT_EQ(inventory2.mainInventory[1]->stackSize, 64);
    EXPECT_EQ(inventory2.mainInventory[1]->itemDamage, 0);
}

// Test case 3: Direct legacy Gzip world storage read/write
TEST(LegacyAndNBTTest, LegacyGzipDirectStorage) {
    std::string testWorldPath = "test_world_legacy_gzip";
    std::filesystem::remove_all(testWorldPath);

    MinecraftServer server;
    std::string propFile = "test_legacy.properties";
    {
        std::ofstream out(propFile);
        out << "use-legacy-storage=true\n";
    }

    server.propertyManager = std::make_unique<PropertyManager>(propFile);

    {
        World world(&server, testWorldPath, 42);
        EXPECT_TRUE(world.useLegacyStorage_);
        EXPECT_NE(world.chunkLoader_, nullptr);
        EXPECT_EQ(world.db_, nullptr);

        // Load chunk, modify block, and save
        Chunk* chunk = world.getChunk(5, 5, true);
        ASSERT_NE(chunk, nullptr);
        chunk->setBlockID(0, 10, 0, 5); // wood block
        chunk->isModified = true;

        world.saveChunkImmediate(chunk);
    }

    // Verify expected Gzip file structure in base-36 folders:
    // x=5 -> "5", z=5 -> "5". file: c.5.5.dat under test_world_legacy_gzip/5/5/
    std::filesystem::path expectedFile = std::filesystem::path(testWorldPath) / "5" / "5" / "c.5.5.dat";
    EXPECT_TRUE(std::filesystem::exists(expectedFile));

    // Reload world and load chunk back
    {
        World world2(&server, testWorldPath, 42);
        Chunk* chunk2 = world2.getChunk(5, 5, false);
        ASSERT_NE(chunk2, nullptr);
        EXPECT_EQ(chunk2->getBlockID(0, 10, 0), 5);
    }

    // Clean up
    std::filesystem::remove_all(testWorldPath);
    std::filesystem::remove(propFile);
}

// Test case 4: On-the-fly legacy migration to LevelDB with timestamped backup and cleanup
TEST(LegacyAndNBTTest, OnTheFlyMigrationAndBackup) {
    std::string testWorldPath = "test_world_migration";
    std::filesystem::remove_all(testWorldPath);
    std::filesystem::create_directories(testWorldPath);

    // Pre-populate legacy world folder with direct gzip chunk
    std::string propFileLegacy = "test_migration_legacy.properties";
    {
        std::ofstream out(propFileLegacy);
        out << "use-legacy-storage=true\n";
    }

    {
        MinecraftServer serverLegacy;
        serverLegacy.propertyManager = std::make_unique<PropertyManager>(propFileLegacy);
        World worldLegacy(&serverLegacy, testWorldPath, 42);
        Chunk* chunk = worldLegacy.getChunk(10, -10, true);
        ASSERT_NE(chunk, nullptr);
        chunk->setBlockID(5, 20, 5, 2); // sand block
        chunk->isModified = true;
        worldLegacy.saveChunkImmediate(chunk);
    }

    // Verify chunk gzip file exists at base-36 path
    // x=10 -> base-36 "a", 10 & 63 = 10 -> "a"
    // z=-10 -> base-36 "-a", -10 & 63 = 54 -> base-36 "1i"
    std::filesystem::path legacyFile = std::filesystem::path(testWorldPath) / "a" / "1i" / "c.a.-a.dat";
    ASSERT_TRUE(std::filesystem::exists(legacyFile));

    std::filesystem::remove(propFileLegacy);

    // Setup migration properties
    std::string propFileMigration = "test_migration.properties";
    {
        std::ofstream out(propFileMigration);
        out << "use-legacy-storage=false\n";
        out << "convert-legacy-world=true\n";
    }

    {
        MinecraftServer serverMigration;
        serverMigration.propertyManager = std::make_unique<PropertyManager>(propFileMigration);

        // Constructing world converts the Gzip world on-the-fly to LevelDB + ZSTD
        World worldMigration(&serverMigration, testWorldPath, 42);

        EXPECT_NE(worldMigration.db_, nullptr);
        EXPECT_FALSE(worldMigration.useLegacyStorage_);

        // Chunk should load perfectly from LevelDB
        Chunk* chunkMigrated = worldMigration.getChunk(10, -10, false);
        ASSERT_NE(chunkMigrated, nullptr);
        EXPECT_EQ(chunkMigrated->getBlockID(5, 20, 5), 2);
    }

    // Legacy file must be cleaned up
    EXPECT_FALSE(std::filesystem::exists(legacyFile));

    // Backup folder must exist
    bool backupFound = false;
    for (const auto& entry : std::filesystem::directory_iterator(".")) {
        if (entry.is_directory() && entry.path().filename().string().starts_with("test_world_migration_backup_")) {
            backupFound = true;
            std::filesystem::remove_all(entry.path());
        }
    }
    EXPECT_TRUE(backupFound);

    // Clean up
    std::filesystem::remove_all(testWorldPath);
    std::filesystem::remove(propFileMigration);
}
