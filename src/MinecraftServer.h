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
    // Деструкция идёт снизу вверх (последний объявленный уничтожается первым):
    // 1. networkListenThread  — сначала закрываем сеть
    // 2. entityTracker        — потом трекер
    // 3. configManager        — потом игроки
    // 4. worldMngr            — потом мир
    // 5. propertyManager      — последним
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
    int64_t getWorldTime() const { return worldTime_; }

private:
    std::atomic<bool> running_{true};
    int tickCounter_ = 0;

    // World state (simplified until full world system)
    int32_t spawnX_ = 0, spawnY_ = 64, spawnZ_ = 0;
    int64_t worldSeed_ = 0;
    int8_t worldDimension_ = 0;
    int64_t worldTime_ = 0;

    // Commands
    std::mutex commandMutex_;
    std::vector<std::string> pendingCommands_;

    bool initialize();
    void serverTick();
    void processCommands();
    void handleCommand(const std::string& command);
};
