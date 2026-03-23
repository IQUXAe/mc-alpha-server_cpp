#include "MinecraftServer.h"
#include "core/MathHelper.h"
#include "core/Logger.h"
#include "network/Packet.h"
#include "network/packets/AllPackets.h"
#include "entity/EntityPlayerMP.h"
#include "world/World.h"
#include "block/Block.h"
#include "core/Item.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>
#include <ranges>
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
    std::cout << "[INFO] Starting minecraft server version 0.2.8 (Alpha 1.2.6)" << std::endl;

    // Load properties
    std::cout << "[INFO] Loading properties" << std::endl;
    propertyManager = std::make_unique<PropertyManager>("server.properties");

    std::string bindAddress = propertyManager->getStringProperty("server-ip", "");
    onlineMode = propertyManager->getBooleanProperty("online-mode", true);
    spawnAnimals = propertyManager->getBooleanProperty("spawn-animals", true);
    pvpEnabled = propertyManager->getBooleanProperty("pvp", true);
    viewDistance = propertyManager->getIntProperty("view-distance", 10);
    if (viewDistance < 3) viewDistance = 3;
    if (viewDistance > 15) viewDistance = 15;
    autoSaveInterval = propertyManager->getIntProperty("auto-save-interval", 6000);
    saveModifiedChunksOnly = propertyManager->getBooleanProperty("save-modified-chunks-only", false);
    int port = propertyManager->getIntProperty("server-port", 25565);

    std::string displayAddr = bindAddress.empty() ? "*" : bindAddress;
    std::cout << "[INFO] Starting Minecraft server on " << displayAddr << ":" << port << std::endl;

    if (!onlineMode) {
        std::cout << "[WARNING] **** SERVER IS RUNNING IN OFFLINE/INSECURE MODE!" << std::endl;
        std::cout << "[WARNING] The server will make no attempt to authenticate usernames. Beware." << std::endl;
        std::cout << "[WARNING] While this makes the game possible to play without internet access, "
                  << "it also opens up the ability for hackers to connect with any username they choose." << std::endl;
        std::cout << "[WARNING] To change this, set \"online-mode\" to \"true\" in the server.properties file." << std::endl;
    }

    // Create network listener
    try {
        networkListenThread = std::make_unique<NetworkListenThread>(this, bindAddress, port);
    } catch (const std::exception& e) {
        std::cerr << "[WARNING] **** FAILED TO BIND TO PORT!" << std::endl;
        std::cerr << "[WARNING] The exception was: " << e.what() << std::endl;
        std::cerr << "[WARNING] Perhaps a server is already running on that port?" << std::endl;
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

    std::cout << "[INFO] Preparing start region" << std::endl;

    // Initialize block and item properties
    Block::initBlocks();
    Item::initItems();

    // Initialize world
    Logger::info("Preparing level \"{}\" with seed config: {}", levelName, levelSeed);
    worldMngr = std::make_unique<World>(this, "world/" + levelName, worldSeed_);
    worldSeed_ = worldMngr->randomSeed;

    // Player saves go inside the world folder, mirroring Java's PlayerNBTManager
    playerSaveDir = "world/" + levelName + "/players";
    std::filesystem::create_directories(playerSaveDir);

    Logger::info("Actual world seed: {}", worldSeed_);
    Logger::info("Spawn position: {}, {}, {}", spawnX_, spawnY_, spawnZ_);

    // Initialize entity tracker
    entityTracker = std::make_unique<EntityTracker>(this);

    std::cout << "[INFO] Preparing start region" << std::endl;
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

    std::cout << "[INFO] Server stopped.\n";
}

void MinecraftServer::stop() {
    running_ = false;
}

void MinecraftServer::serverTick() {
    ++tickCounter_;

    // Increment world time
    worldTime_++;

    // Send time update every 20 ticks (1 second)
    if (tickCounter_ % ServerConstants::TICKS_PER_SECOND == 0) {
        configManager->broadcastPacket(std::make_unique<Packet4UpdateTime>(worldTime_));
    }

    if (worldMngr) {
        worldMngr->tick();
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

    if (lower.starts_with("help") || lower.starts_with("?")) {
        std::cout <<
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
            "   tell <player> <message>   sends a private message\n"
            "   stop                      gracefully stops the server\n"
            "   save-all                  forces a server-wide level save\n"
            "   list                      lists all connected players\n"
            "   say <message>             broadcasts a message\n";
    } else if (lower.starts_with("list")) {
        std::cout << "Connected players: " << configManager->getPlayerList() << '\n';
    } else if (lower.starts_with("stop")) {
        std::cout << "CONSOLE: Stopping the server..\n";
        running_ = false;
    } else if (lower.starts_with("save-all")) {
        std::cout << "CONSOLE: Forcing save..\n";
        if (worldMngr) worldMngr->saveWorld();
        if (configManager) configManager->savePlayerStates();
        std::cout << "CONSOLE: Save complete.\n";
    } else if (lower.starts_with("op ")) {
        auto name = argOf("op ");
        configManager->opPlayer(name);
        std::cout << "CONSOLE: Opping " << name << '\n';
        configManager->sendChatToPlayer(name, "\u00a7eYou are now op!");
    } else if (lower.starts_with("deop ")) {
        auto name = argOf("deop ");
        configManager->deopPlayer(name);
        std::cout << "CONSOLE: De-opping " << name << '\n';
        configManager->sendChatToPlayer(name, "\u00a7eYou are no longer op!");
    } else if (lower.starts_with("ban-ip ")) {
        auto ip = argOf("ban-ip ");
        configManager->banIP(ip);
        std::cout << "CONSOLE: Banning ip " << ip << '\n';
    } else if (lower.starts_with("pardon-ip ")) {
        auto ip = argOf("pardon-ip ");
        configManager->unbanIP(ip);
        std::cout << "CONSOLE: Pardoning ip " << ip << '\n';
    } else if (lower.starts_with("ban ")) {
        auto name = argOf("ban ");
        configManager->banPlayer(name);
        std::cout << "CONSOLE: Banning " << name << '\n';
        if (auto* player = configManager->getPlayerEntity(name); player && player->netHandler)
            player->netHandler->kick("Banned by admin");
    } else if (lower.starts_with("pardon ")) {
        auto name = argOf("pardon ");
        configManager->unbanPlayer(name);
        std::cout << "CONSOLE: Pardoning " << name << '\n';
    } else if (lower.starts_with("kick ")) {
        auto name = argOf("kick ");
        if (auto* player = configManager->getPlayerEntity(name); player && player->netHandler) {
            player->netHandler->kick("Kicked by admin");
            std::cout << "CONSOLE: Kicking " << player->username << '\n';
        } else {
            std::cout << "Can't find user " << name << ". No kick.\n";
        }
    } else if (lower.starts_with("tp ")) {
        auto [p1, p2] = splitTwo(trimLeft(sv.substr(3)));
        auto* player1 = configManager->getPlayerEntity(p1);
        auto* player2 = configManager->getPlayerEntity(p2);
        if (!player1)       std::cout << "Can't find user " << p1 << ". No tp.\n";
        else if (!player2)  std::cout << "Can't find user " << p2 << ". No tp.\n";
        else {
            player1->netHandler->teleport(player2->posX, player2->posY, player2->posZ,
                                          player2->rotationYaw, player2->rotationPitch);
            std::cout << "CONSOLE: Teleporting " << p1 << " to " << p2 << ".\n";
        }
    } else if (lower.starts_with("say ")) {
        auto msg = argOf("say ");
        std::cout << "[Server] " << msg << '\n';
        configManager->broadcastPacket(std::make_unique<Packet3Chat>("\u00a7d[Server] " + msg));
    } else if (lower.starts_with("tell ")) {
        auto [target, msg] = splitTwo(trimLeft(sv.substr(5)));
        std::cout << "[CONSOLE->" << target << "] " << msg << '\n';
        if (!configManager->sendPacketToPlayer(target,
                std::make_unique<Packet3Chat>("\u00a77CONSOLE whispers " + msg)))
            std::cout << "There's no player by that name online.\n";
    } else {
        std::cout << "Unknown console command. Type \"help\" for help.\n";
    }
}
