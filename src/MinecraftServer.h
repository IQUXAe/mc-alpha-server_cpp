#pragma once

#include "forward.h"
#include "core/PropertyManager.h"
#include "core/ServerConstants.h"
#include "network/NetworkListenThread.h"
#include "server/ServerConfigurationManager.h"
#include "entity/EntityTracker.h"

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

class MinecraftServer {
public:
    // Destruction order is bottom-up (last declared = first destroyed):
    // 1. networkListenThread  — close network first
    // 2. entityTracker        — then tracker
    // 3. configManager        — then players
    // 4. worldMngr            — then world
    // 5. propertyManager      — last
    std::unique_ptr<PropertyManager> propertyManager;
    std::unique_ptr<World> worldMngr;
    std::unique_ptr<ServerConfigurationManager> configManager;
    std::unique_ptr<EntityTracker> entityTracker;
    std::unique_ptr<NetworkListenThread> networkListenThread;

    MinecraftServer();
    ~MinecraftServer();

    // Main server loop
    void run();
    void stop();

    // Command handling
    void addCommand(const std::string& command);

    // World state accessors
    int32_t getSpawnX() const { return spawnX_; }
    int32_t getSpawnY() const { return spawnY_; }
    int32_t getSpawnZ() const { return spawnZ_; }
    int64_t getWorldSeed() const { return worldSeed_; }
    int8_t  getWorldDimension() const { return worldDimension_; }
    int64_t getWorldTime() const;

    // Config accessors
    bool isOnlineMode() const { return onlineMode_; }
    bool isSpawnAnimals() const { return spawnAnimals_; }
    bool isSpawnMonsters() const { return spawnMonsters_; }
    bool isPvpEnabled() const { return pvpEnabled_; }
    int  getViewDistance() const { return viewDistance_; }
    int  getAutoSaveInterval() const { return autoSaveInterval_; }
    bool isSaveModifiedChunksOnly() const { return saveModifiedChunksOnly_; }
    int  getSpawnProtectionRadius() const { return spawnProtectionRadius_; }
    const std::string& getPlayerSaveDir() const { return playerSaveDir_; }

private:
    std::atomic<bool> running_{true};
    int tickCounter_ = 0;

    // World state
    int32_t spawnX_ = 0, spawnY_ = 64, spawnZ_ = 0;
    int64_t worldSeed_ = 0;
    int8_t  worldDimension_ = 0;

    // Config (loaded from server.properties)
    bool        onlineMode_             = true;
    bool        spawnAnimals_           = true;
    bool        spawnMonsters_          = true;
    bool        pvpEnabled_             = true;
    int         viewDistance_           = 10;
    int         autoSaveInterval_       = 900;
    bool        saveModifiedChunksOnly_ = false;
    int         spawnProtectionRadius_  = 16;  // default vanilla spawn protection radius
    std::string playerSaveDir_;         // world/<levelname>/players

    // Commands
    std::mutex commandMutex_;
    std::vector<std::string> pendingCommands_;

    bool initialize();
    void serverTick();
    void processCommands();
    void handleCommand(const std::string& command);
};
