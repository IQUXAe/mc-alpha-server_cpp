#include "MinecraftServer.h"
#include "core/MathHelper.h"
#include "core/Logger.h"
#include "network/Packet.h"
#include "network/packets/AllPackets.h"
#include "entity/EntityPlayerMP.h"
#include "entity/EntityAnimals.h"
#include "entity/EntityMobs.h"
#include "world/World.h"
#include "block/Block.h"
#include "core/Item.h"

#include <chrono>
#include <thread>
#include <random>
#include <algorithm>
#include <ranges>
#include <cctype>
#include <filesystem>

static std::string_view trimLeft(std::string_view s) {
    auto it = std::ranges::find_if_not(s, [](char c){ return c == ' '; });
    return s.substr(static_cast<size_t>(it - s.begin()));
}

MinecraftServer::MinecraftServer() {
    MathHelper::init();
    Packet::registerPackets();
}

MinecraftServer::~MinecraftServer() {
    // stop() is safe to call multiple times
    stop();
    // saveWorld() is already called in run() before the loop exits
}

bool MinecraftServer::initialize() {
    Logger::info("Starting Minecraft server version 0.2.8 (Alpha 1.2.6)");

    // Load properties
    Logger::info("Loading properties");
    propertyManager = std::make_unique<PropertyManager>("server.properties");

    std::string bindAddress = propertyManager->getStringProperty("server-ip", "");
    onlineMode_             = propertyManager->getBooleanProperty("online-mode", true);
    spawnAnimals_           = propertyManager->getBooleanProperty("spawn-animals", true);
    spawnMonsters_          = propertyManager->getBooleanProperty("spawn-monsters", true);
    pvpEnabled_             = propertyManager->getBooleanProperty("pvp", true);
    difficulty_             = propertyManager->getIntProperty("difficulty", 2);
    difficulty_             = std::clamp(difficulty_, 0, 3);
    viewDistance_           = propertyManager->getIntProperty("view-distance", 10);
    if (viewDistance_ < 3)  viewDistance_ = 3;
    if (viewDistance_ > 15) viewDistance_ = 15;
    autoSaveInterval_       = propertyManager->getIntProperty("auto-save-interval", 6000);
    saveModifiedChunksOnly_ = propertyManager->getBooleanProperty("save-modified-chunks-only", false);
    spawnProtectionRadius_  = propertyManager->getIntProperty("spawn-protection-radius", 16);
    if (spawnProtectionRadius_ < 0) spawnProtectionRadius_ = 0;
    int port = propertyManager->getIntProperty("server-port", 25565);

    std::string displayAddr = bindAddress.empty() ? "*" : bindAddress;
    Logger::info("Starting Minecraft server on {}:{}", displayAddr, port);

    if (!onlineMode_) {
        Logger::warning("**** SERVER IS RUNNING IN OFFLINE/INSECURE MODE!");
        Logger::warning("The server will make no attempt to authenticate usernames. Beware.");
        Logger::warning("While this makes the game possible to play without internet access, it also opens up the ability for hackers to connect with any username they choose.");
        Logger::warning("To change this, set \"online-mode\" to \"true\" in the server.properties file.");
    }

    // Create network listener
    try {
        networkListenThread = std::make_unique<NetworkListenThread>(this, bindAddress, port);
    } catch (const std::exception& e) {
        Logger::warning("**** FAILED TO BIND TO PORT!");
        Logger::warning("The exception was: {}", e.what());
        Logger::warning("Perhaps a server is already running on that port?");
        return false;
    }

    // Create configuration manager
    configManager = std::make_unique<ServerConfigurationManager>(this);

    // Initialize world
    std::string levelName = propertyManager->getStringProperty("level-name", "world");
    std::string levelSeed = propertyManager->getStringProperty("level-seed", "");
    bool isHellWorld = propertyManager->getBooleanProperty("hellworld", false);
    worldDimension_ = isHellWorld ? -1 : 0;

    // Generate world seed from level-seed property
    if (!levelSeed.empty()) {
        // Check if seed is numeric
        try {
            worldSeed_ = std::stoll(levelSeed);
            Logger::info("Using numeric world seed: {}", worldSeed_);
        } catch (...) {
            // String seed - use Java's String.hashCode() algorithm
            // Java: s[0]*31^(n-1) + s[1]*31^(n-2) + ... + s[n-1]
            int64_t hash = 0;
            for (char c : levelSeed) {
                hash = hash * 31 + static_cast<int8_t>(c);
            }
            worldSeed_ = hash;
            Logger::info("Using string world seed: \"{}\" -> {}", levelSeed, worldSeed_);
        }
    } else {
        // Empty seed - will be generated randomly by World
        worldSeed_ = 0;
        Logger::info("No level-seed specified, world will generate random seed");
    }

    Logger::info("Preparing start region");

    // Initialize block and item properties
    Block::initBlocks();
    Item::initItems();

    // Initialize world
    Logger::info("Preparing level \"{}\" with seed config: {}", levelName, levelSeed);
    worldMngr = std::make_unique<World>(this, "world/" + levelName, worldSeed_);
    worldSeed_ = worldMngr->randomSeed;

    // Register TileEntity change callback: broadcasts Packet59 to nearby players.
    // Matches Java: IWorldAccess.func_686_a -> WorldManager -> configManager.sentTileEntityToPlayer
    worldMngr->onTileEntityChanged = [this](int x, int y, int z, TileEntity* te) {
        configManager->sendTileEntityToNearbyPlayers(x, y, z, te);
    };

    // Player saves go inside the world folder, mirroring Java's PlayerNBTManager
    playerSaveDir_ = "world/" + levelName + "/players";
    std::filesystem::create_directories(playerSaveDir_);

    Logger::info("Actual world seed: {}", worldSeed_);
    Logger::info("Spawn position: {}, {}, {}", spawnX_, spawnY_, spawnZ_);

    // Initialize entity tracker
    entityTracker = std::make_unique<EntityTracker>(this);
    worldMngr->registerLoadedEntitiesWithTracker(entityTracker.get());

    // Spawn position is already set by World constructor, but we ensure it here
    spawnX_ = worldMngr->spawnX;
    spawnY_ = worldMngr->spawnY;
    spawnZ_ = worldMngr->spawnZ;

    Logger::info("Done! For help, type \"help\" or \"?\"");
    return true;
}

void MinecraftServer::run() {
    if (!initialize()) {
        Logger::severe("Failed to initialize server");
        return;
    }

    auto lastTime = std::chrono::steady_clock::now();
    int64_t lag = 0;

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();

        if (elapsed > 2000) {
            Logger::warning("Can't keep up! Did the system time change, or is the server overloaded?");
            elapsed = 2000;
        }

        if (elapsed < 0) {
            Logger::warning("Time ran backwards! Did the system time change?");
            elapsed = 0;
        }

        lag += elapsed;
        lastTime = now;

        while (lag >= ServerConstants::MILLISECONDS_PER_TICK) {
            lag -= ServerConstants::MILLISECONDS_PER_TICK;
            serverTick();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Shutdown
    Logger::info("Stopping server");

    // 1. Kick all players so NetServerHandler unregisters from configManager
    if (configManager) {
        // Copy the list since kick() modifies playerEntities
        auto players = configManager->playerEntities;
        for (auto* p : players) {
            if (p && p->netHandler)
                p->netHandler->kick("Server shutting down");
        }
        configManager->syncHeldItems();
        configManager->savePlayerStates();
    }

    // 2. Stop network — no new packets after this
    networkListenThread.reset();

    // 3. Save world
    if (worldMngr) worldMngr->saveWorld();

    Logger::info("Server stopped.");
}

void MinecraftServer::stop() {
    running_ = false;
}

int64_t MinecraftServer::getWorldTime() const {
    return worldMngr ? worldMngr->worldTime : 0;
}

void MinecraftServer::serverTick() {
    ++tickCounter_;

    // Send time update every 20 ticks (1 second) — time is incremented by World::tick()
    if (tickCounter_ % ServerConstants::TICKS_PER_SECOND == 0) {
        configManager->broadcastPacket(std::make_unique<Packet4UpdateTime>(getWorldTime()));
    }

    if (worldMngr) {
        worldMngr->tick();
    }

    if (configManager) {
        for (auto* player : configManager->playerEntities) {
            if (player && !player->isDead) {
                player->tick();
            }
        }
    }

    if (configManager && autoSaveInterval_ > 0 && tickCounter_ % autoSaveInterval_ == 0) {
        configManager->savePlayerStates();
    }
    
    // Update entity tracking
    if (entityTracker) {
        entityTracker->tick();
    }

    // Process network
    networkListenThread->networkTick();

    // Process console commands
    processCommands();
}

void MinecraftServer::addCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    pendingCommands_.push_back(command);
}

void MinecraftServer::processCommands() {
    std::vector<std::string> commands;
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        commands.swap(pendingCommands_);
    }

    for (const auto& cmd : commands) {
        handleCommand(cmd);
    }
}

void MinecraftServer::handleCommand(const std::string& cmd) {
    std::string lower = cmd;
    std::ranges::transform(lower, lower.begin(), ::tolower);
    std::string_view sv(cmd);

    auto argOf = [&](std::string_view prefix) -> std::string {
        return std::string(trimLeft(sv.substr(prefix.size())));
    };
    auto splitTwo = [](std::string_view s) -> std::pair<std::string, std::string> {
        auto sp = s.find(' ');
        if (sp == std::string_view::npos) return {std::string(s), {}};
        return {std::string(s.substr(0, sp)), std::string(trimLeft(s.substr(sp + 1)))};
    };
    auto toLowerCopy = [](std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    };

    if (lower.starts_with("help") || lower.starts_with("?")) {
        Logger::info(
            "Console commands:\n"
            "   help  or  ?               shows this message\n"
            "   kick <player>             removes a player from the server\n"
            "   ban <player>              bans a player from the server\n"
            "   pardon <player>           pardons a banned player\n"
            "   ban-ip <ip>               bans an IP address\n"
            "   pardon-ip <ip>            pardons a banned IP address\n"
            "   op <player>               turns a player into an op\n"
            "   deop <player>             removes op status\n"
            "   tp <player1> <player2>    teleports player1 to player2\n"
            "   give <player> <id> [num]  gives a player a resource\n"
            "   summon <mob> [count] [player] spawns debug mobs near player\n"
            "   tell <player> <message>   sends a private message\n"
            "   stop                      gracefully stops the server\n"
            "   save-all                  forces a server-wide level save\n"
            "   list                      lists all connected players\n"
            "   say <message>             broadcasts a message");
    } else if (lower.starts_with("list")) {
        Logger::info("Connected players: {}", configManager->getPlayerList());
    } else if (lower.starts_with("stop")) {
        Logger::info("Stopping the server..");
        running_ = false;
    } else if (lower.starts_with("save-all")) {
        Logger::info("Forcing save..");
        if (worldMngr) worldMngr->saveWorld();
        if (configManager) configManager->savePlayerStates();
        Logger::info("Save complete.");
    } else if (lower.starts_with("op ")) {
        auto name = argOf("op ");
        configManager->opPlayer(name);
        Logger::info("Opping {}", name);
        configManager->sendChatToPlayer(name, "\u00a7eYou are now op!");
    } else if (lower.starts_with("deop ")) {
        auto name = argOf("deop ");
        configManager->deopPlayer(name);
        Logger::info("De-opping {}", name);
        configManager->sendChatToPlayer(name, "\u00a7eYou are no longer op!");
    } else if (lower.starts_with("ban-ip ")) {
        auto ip = argOf("ban-ip ");
        configManager->banIP(ip);
        Logger::info("Banning ip {}", ip);
    } else if (lower.starts_with("pardon-ip ")) {
        auto ip = argOf("pardon-ip ");
        configManager->unbanIP(ip);
        Logger::info("Pardoning ip {}", ip);
    } else if (lower.starts_with("ban ")) {
        auto name = argOf("ban ");
        configManager->banPlayer(name);
        Logger::info("Banning {}", name);
        if (auto* player = configManager->getPlayerEntity(name); player && player->netHandler)
            player->netHandler->kick("Banned by admin");
    } else if (lower.starts_with("pardon ")) {
        auto name = argOf("pardon ");
        configManager->unbanPlayer(name);
        Logger::info("Pardoning {}", name);
    } else if (lower.starts_with("kick ")) {
        auto name = argOf("kick ");
        if (auto* player = configManager->getPlayerEntity(name); player && player->netHandler) {
            player->netHandler->kick("Kicked by admin");
            Logger::info("Kicking {}", player->username);
        } else {
            Logger::info("Can't find user {}. No kick.", name);
        }
    } else if (lower.starts_with("tp ")) {
        auto [p1, p2] = splitTwo(trimLeft(sv.substr(3)));
        auto* player1 = configManager->getPlayerEntity(p1);
        auto* player2 = configManager->getPlayerEntity(p2);
        if (!player1)       Logger::info("Can't find user {}. No tp.", p1);
        else if (!player2)  Logger::info("Can't find user {}. No tp.", p2);
        else {
            player1->netHandler->teleport(player2->posX, player2->posY, player2->posZ,
                                          player2->rotationYaw, player2->rotationPitch);
            Logger::info("Teleporting {} to {}.", p1, p2);
        }
    } else if (lower.starts_with("summon ")) {
        if (!worldMngr || !configManager) {
            Logger::info("World is not ready.");
            return;
        }

        auto rest = std::string(trimLeft(sv.substr(7)));
        auto [entityNameRaw, tail] = splitTwo(rest);
        if (entityNameRaw.empty()) {
            Logger::info("Usage: summon <mob> [count] [player]");
            return;
        }

        int count = 1;
        std::string targetName;
        if (!tail.empty()) {
            auto [arg1, arg2] = splitTwo(tail);
            if (!arg1.empty()) {
                bool isNumber = std::ranges::all_of(arg1, [](unsigned char c) { return std::isdigit(c) != 0; });
                if (isNumber) {
                    count = std::max(1, std::min(64, std::stoi(arg1)));
                    targetName = arg2;
                } else {
                    targetName = arg1;
                }
            }
        }

        EntityPlayerMP* anchor = nullptr;
        if (!targetName.empty()) {
            anchor = configManager->getPlayerEntity(targetName);
            if (!anchor) {
                Logger::info("Can't find user {}. No summon.", targetName);
                return;
            }
        } else if (!configManager->playerEntities.empty()) {
            anchor = configManager->playerEntities.front();
        }

        if (!anchor) {
            Logger::info("No online players to anchor summon.");
            return;
        }

        const std::string entityName = toLowerCopy(entityNameRaw);
        auto createEntity = [&]() -> std::unique_ptr<Entity> {
            if (entityName == "pig") return std::make_unique<EntityPig>(worldMngr.get());
            if (entityName == "sheep") return std::make_unique<EntitySheep>(worldMngr.get());
            if (entityName == "cow") return std::make_unique<EntityCow>(worldMngr.get());
            if (entityName == "chicken") return std::make_unique<EntityChicken>(worldMngr.get());
            if (entityName == "zombie") return std::make_unique<EntityZombie>(worldMngr.get());
            if (entityName == "skeleton") return std::make_unique<EntitySkeleton>(worldMngr.get());
            if (entityName == "spider") return std::make_unique<EntitySpider>(worldMngr.get());
            if (entityName == "creeper") return std::make_unique<EntityCreeper>(worldMngr.get());
            return nullptr;
        };

        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            auto entity = createEntity();
            if (!entity) {
                Logger::info("Unknown mob {}. Try pig/sheep/cow/chicken/zombie/skeleton/spider/creeper.", entityNameRaw);
                return;
            }

            const double angle = (static_cast<double>(i) / std::max(1, count)) * 6.283185307179586;
            const double radius = 2.0 + (i % 3);
            const double spawnX = anchor->posX + std::cos(angle) * radius;
            const double spawnZ = anchor->posZ + std::sin(angle) * radius;
            const int groundY = worldMngr->getHeightValue(static_cast<int>(std::floor(spawnX)),
                                                          static_cast<int>(std::floor(spawnZ)));
            const double spawnY = std::max(anchor->posY, static_cast<double>(groundY));

            entity->setPositionAndRotation(
                spawnX,
                spawnY,
                spawnZ,
                static_cast<float>(std::rand() % 360),
                0.0f);
            worldMngr->spawnEntityInWorld(std::move(entity));
            ++spawned;
        }

        Logger::info("Spawned {} {} near {}.", spawned, entityNameRaw, anchor->username);
    } else if (lower.starts_with("say ")) {
        auto msg = argOf("say ");
        Logger::info("[Server] {}", msg);
        configManager->broadcastPacket(std::make_unique<Packet3Chat>("\u00a7d[Server] " + msg));
    } else if (lower.starts_with("tell ")) {
        auto [target, msg] = splitTwo(trimLeft(sv.substr(5)));
        Logger::info("[CONSOLE->{}] {}", target, msg);
        if (!configManager->sendPacketToPlayer(target,
                std::make_unique<Packet3Chat>("\u00a77CONSOLE whispers " + msg)))
            Logger::info("There's no player by that name online.");
    } else {
        Logger::info("Unknown console command. Type \"help\" for help.");
    }
}
