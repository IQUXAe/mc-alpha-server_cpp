#include <gtest/gtest.h>
#include "block/Block.h"
#include "core/Item.h"

class GlobalBlockInit : public ::testing::Environment {
public:
    void SetUp() override {
        Block::initBlocks();
        Item::initItems();
    }
};

// Register global environment (runs once before all tests)
static testing::Environment* const env =
    testing::AddGlobalTestEnvironment(new GlobalBlockInit);
