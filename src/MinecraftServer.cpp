#include "MinecraftServer.h"
#include "core/MathHelper.h"
#include "network/Packet.h"
#include "network/packets/AllPackets.h"
#include "entity/EntityPlayerMP.h"
#include "world/World.h"
#include "block/Block.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>

MinecraftServer::MinecraftServer() {
    MathHelper::init();
    Packet::registerPackets();
}

MinecraftServer::~MinecraftServer() = default;

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
    bool isHellWorld = propertyManager->getBooleanProperty("hellworld", false);
    worldDimension_ = isHellWorld ? -1 : 0;

    // Generate world seed
    std::random_device rd;
    std::mt19937_64 gen(rd());
    worldSeed_ = gen();

    std::cout << "[INFO] Preparing level \"" << levelName << "\"" << std::endl;

    // Initialize block properties
    Block::initBlocks();

    // Initialize world
    worldMngr = std::make_unique<World>(this, levelName);

    std::cout << "[INFO] Preparing start region" << std::endl;
    // Spawn position is already set by World constructor, but we ensure it here
    spawnX_ = worldMngr->spawnX;
    spawnY_ = worldMngr->spawnY;
    spawnZ_ = worldMngr->spawnZ;

    std::cout << "[INFO] Done! For help, type \"help\" or \"?\"" << std::endl;
    return true;
}

void MinecraftServer::run() {
    if (!initialize()) {
        std::cerr << "[SEVERE] Failed to initialize server" << std::endl;
        return;
    }

    auto lastTime = std::chrono::steady_clock::now();
    int64_t lag = 0;

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();

        if (elapsed > 2000) {
            std::cerr << "[WARNING] Can't keep up! Did the system time change, or is the server overloaded?" << std::endl;
            elapsed = 2000;
        }

        if (elapsed < 0) {
            std::cerr << "[WARNING] Time ran backwards! Did the system time change?" << std::endl;
            elapsed = 0;
        }

        lag += elapsed;
        lastTime = now;

        while (lag >= 50) {
            lag -= 50;
            serverTick();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Shutdown
    std::cout << "[INFO] Stopping server" << std::endl;
    if (configManager) {
        configManager->savePlayerStates();
    }
    // TODO: save world

    std::cout << "[INFO] Server stopped." << std::endl;
}

void MinecraftServer::stop() {
    running_ = false;
}

void MinecraftServer::serverTick() {
    ++tickCounter_;

    // Increment world time
    worldTime_++;

    // Send time update every 20 ticks (1 second)
    if (tickCounter_ % 20 == 0) {
        configManager->broadcastPacket(std::make_unique<Packet4UpdateTime>(worldTime_));
    }

    // TODO: world tick
    // TODO: entity updates

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
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.starts_with("help") || lower.starts_with("?")) {
        std::cout << "Console commands:" << std::endl;
        std::cout << "   help  or  ?               shows this message" << std::endl;
        std::cout << "   kick <player>             removes a player from the server" << std::endl;
        std::cout << "   ban <player>              bans a player from the server" << std::endl;
        std::cout << "   pardon <player>           pardons a banned player" << std::endl;
        std::cout << "   ban-ip <ip>               bans an IP address" << std::endl;
        std::cout << "   pardon-ip <ip>            pardons a banned IP address" << std::endl;
        std::cout << "   op <player>               turns a player into an op" << std::endl;
        std::cout << "   deop <player>             removes op status" << std::endl;
        std::cout << "   tp <player1> <player2>    teleports player1 to player2" << std::endl;
        std::cout << "   give <player> <id> [num]  gives a player a resource" << std::endl;
        std::cout << "   tell <player> <message>   sends a private message" << std::endl;
        std::cout << "   stop                      gracefully stops the server" << std::endl;
        std::cout << "   save-all                  forces a server-wide level save" << std::endl;
        std::cout << "   list                      lists all connected players" << std::endl;
        std::cout << "   say <message>             broadcasts a message" << std::endl;
    } else if (lower.starts_with("list")) {
        std::cout << "Connected players: " << configManager->getPlayerList() << std::endl;
    } else if (lower.starts_with("stop")) {
        std::cout << "CONSOLE: Stopping the server.." << std::endl;
        running_ = false;
    } else if (lower.starts_with("save-all")) {
        std::cout << "CONSOLE: Forcing save.." << std::endl;
        // TODO: save world
        std::cout << "CONSOLE: Save complete." << std::endl;
    } else if (lower.starts_with("op ")) {
        std::string name = cmd.substr(3);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        configManager->opPlayer(name);
        std::cout << "CONSOLE: Opping " << name << std::endl;
        configManager->sendChatToPlayer(name, "\u00a7eYou are now op!");
    } else if (lower.starts_with("deop ")) {
        std::string name = cmd.substr(5);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        configManager->deopPlayer(name);
        std::cout << "CONSOLE: De-opping " << name << std::endl;
        configManager->sendChatToPlayer(name, "\u00a7eYou are no longer op!");
    } else if (lower.starts_with("ban-ip ")) {
        std::string ip = cmd.substr(7);
        while (!ip.empty() && ip.front() == ' ') ip.erase(ip.begin());
        configManager->banIP(ip);
        std::cout << "CONSOLE: Banning ip " << ip << std::endl;
    } else if (lower.starts_with("pardon-ip ")) {
        std::string ip = cmd.substr(10);
        while (!ip.empty() && ip.front() == ' ') ip.erase(ip.begin());
        configManager->unbanIP(ip);
        std::cout << "CONSOLE: Pardoning ip " << ip << std::endl;
    } else if (lower.starts_with("ban ")) {
        std::string name = cmd.substr(4);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        configManager->banPlayer(name);
        std::cout << "CONSOLE: Banning " << name << std::endl;
        auto* player = configManager->getPlayerEntity(name);
        if (player && player->netHandler) {
            player->netHandler->kick("Banned by admin");
        }
    } else if (lower.starts_with("pardon ")) {
        std::string name = cmd.substr(7);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        configManager->unbanPlayer(name);
        std::cout << "CONSOLE: Pardoning " << name << std::endl;
    } else if (lower.starts_with("kick ")) {
        std::string name = cmd.substr(5);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        auto* player = configManager->getPlayerEntity(name);
        if (player && player->netHandler) {
            player->netHandler->kick("Kicked by admin");
            std::cout << "CONSOLE: Kicking " << player->username << std::endl;
        } else {
            std::cout << "Can't find user " << name << ". No kick." << std::endl;
        }
    } else if (lower.starts_with("tp ")) {
        // Parse: tp <player1> <player2>
        auto parts = cmd.substr(3);
        while (!parts.empty() && parts.front() == ' ') parts.erase(parts.begin());
        auto spaceIdx = parts.find(' ');
        if (spaceIdx != std::string::npos) {
            std::string p1 = parts.substr(0, spaceIdx);
            std::string p2 = parts.substr(spaceIdx + 1);
            while (!p2.empty() && p2.front() == ' ') p2.erase(p2.begin());

            auto* player1 = configManager->getPlayerEntity(p1);
            auto* player2 = configManager->getPlayerEntity(p2);
            if (!player1) {
                std::cout << "Can't find user " << p1 << ". No tp." << std::endl;
            } else if (!player2) {
                std::cout << "Can't find user " << p2 << ". No tp." << std::endl;
            } else {
                player1->netHandler->teleport(player2->posX, player2->posY, player2->posZ,
                                               player2->rotationYaw, player2->rotationPitch);
                std::cout << "CONSOLE: Teleporting " << p1 << " to " << p2 << "." << std::endl;
            }
        } else {
            std::cout << "Syntax error, please provide a source and a target." << std::endl;
        }
    } else if (lower.starts_with("say ")) {
        std::string msg = cmd.substr(4);
        while (!msg.empty() && msg.front() == ' ') msg.erase(msg.begin());
        std::cout << "[Server] " << msg << std::endl;
        configManager->broadcastPacket(std::make_unique<Packet3Chat>("\u00a7d[Server] " + msg));
    } else if (lower.starts_with("tell ")) {
        auto parts = cmd.substr(5);
        while (!parts.empty() && parts.front() == ' ') parts.erase(parts.begin());
        auto spaceIdx = parts.find(' ');
        if (spaceIdx != std::string::npos) {
            std::string target = parts.substr(0, spaceIdx);
            std::string msg = parts.substr(spaceIdx + 1);
            while (!msg.empty() && msg.front() == ' ') msg.erase(msg.begin());
            std::cout << "[CONSOLE->" << target << "] " << msg << std::endl;
            if (!configManager->sendPacketToPlayer(target,
                    std::make_unique<Packet3Chat>("\u00a77CONSOLE whispers " + msg))) {
                std::cout << "There's no player by that name online." << std::endl;
            }
        }
    } else {
        std::cout << "Unknown console command. Type \"help\" for help." << std::endl;
    }
}
