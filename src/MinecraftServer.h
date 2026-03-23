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

    bool onlineMode = true;
    bool spawnAnimals = true;
    bool pvpEnabled = true;
    int viewDistance = 10;
    int autoSaveInterval = 6000;
    bool saveModifiedChunksOnly = false;
    std::string playerSaveDir;  // world/<levelname>/players

    MinecraftServer();
    ~MinecraftServer();

    // Main server loop
    void run();
    void stop();

    // Command handling
    void addCommand(const std::string& command);

    // World accessors (stubs until world system is implemented)
    int32_t getSpawnX() const { return spawnX_; }
    int32_t getSpawnY() const { return spawnY_; }
    int32_t getSpawnZ() const { return spawnZ_; }
    int64_t getWorldSeed() const { return worldSeed_; }
    int8_t getWorldDimension() const { return worldDimension_; }
    int64_t getWorldTime() const;

private:
    std::atomic<bool> running_{true};
    int tickCounter_ = 0;

    // World state (simplified until full world system)
    int32_t spawnX_ = 0, spawnY_ = 64, spawnZ_ = 0;
    int64_t worldSeed_ = 0;
    int8_t worldDimension_ = 0;
    // worldTime_ removed — time is owned by worldMngr->worldTime

    // Commands
    std::mutex commandMutex_;
    std::vector<std::string> pendingCommands_;

    bool initialize();
    void serverTick();
    void processCommands();
    void handleCommand(const std::string& command);
};
