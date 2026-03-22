#include "ServerConfigurationManager.h"
#include "../MinecraftServer.h"
#include "../network/NetLoginHandler.h"
#include "../entity/EntityTracker.h"
#include "../world/World.h"

#include <fstream>
#include <iostream>
#include <algorithm>

ServerConfigurationManager::ServerConfigurationManager(MinecraftServer* server)
    : mcServer_(server) {
    maxPlayers_ = server->propertyManager->getIntProperty("max-players", 20);

    loadList(bannedPlayersFile_, bannedPlayers_);
    loadList(bannedIPsFile_, bannedIPs_);
    loadList(opsFile_, ops_);

    // Save to normalize files
    saveList(bannedPlayersFile_, bannedPlayers_);
    saveList(bannedIPsFile_, bannedIPs_);
    saveList(opsFile_, ops_);
}

EntityPlayerMP* ServerConfigurationManager::login(NetLoginHandler* handler, const std::string& username, const std::string& password) {
    std::string lowerName = toLower(username);

    if (bannedPlayers_.count(lowerName)) {
        handler->kickUser("You are banned from this server!");
        return nullptr;
    }

    std::string ip = handler->netManager->getRemoteAddress();
    // Extract IP from format "/ip:port"
    auto slashPos = ip.find('/');
    if (slashPos != std::string::npos) ip = ip.substr(slashPos + 1);
    auto colonPos = ip.find(':');
    if (colonPos != std::string::npos) ip = ip.substr(0, colonPos);

    if (bannedIPs_.count(ip)) {
        handler->kickUser("Your IP address is banned from this server!");
        return nullptr;
    }

    if (static_cast<int>(playerEntities.size()) >= maxPlayers_) {
        handler->kickUser("The server is full!");
        return nullptr;
    }

    // Kick existing player with same name
    for (auto* player : playerEntities) {
        if (toLower(player->username) == lowerName) {
            player->netHandler->kick("You logged in from another location");
        }
    }

    auto* player = new EntityPlayerMP(mcServer_, mcServer_->worldMngr.get(), username);
    // Set spawn position
    player->setPosition(mcServer_->worldMngr->spawnX + 0.5, mcServer_->worldMngr->spawnY, mcServer_->worldMngr->spawnZ + 0.5);
    return player;
}

void ServerConfigurationManager::playerLoggedIn(EntityPlayerMP* player) {
    playerEntities.push_back(player);

    // Try to load player data from NBT file
    std::string saveDir = "player_states";
    std::string saveFile = saveDir + "/" + toLower(player->username) + ".dat";
    
    if (player->loadFromFile(saveFile)) {
        std::cout << "[INFO] Loaded player data for " << player->username << std::endl;
    } else {
        // Fallback: try old .pos format
        std::string oldFile = saveDir + "/" + toLower(player->username) + ".pos";
        std::ifstream file(oldFile);
        if (file.is_open()) {
            double x, y, z;
            float yaw, pitch;
            if (file >> x >> y >> z >> yaw >> pitch) {
                player->setPositionAndRotation(x, y, z, yaw, pitch);
                std::cout << "[INFO] Loaded player position (legacy) for " << player->username << std::endl;
            }
        }
    }

    // Register with entity tracker (sends spawn to nearby players)
    if (mcServer_->entityTracker) {
        mcServer_->entityTracker->addEntity(player);
    }
}

void ServerConfigurationManager::playerLoggedOut(EntityPlayerMP* player) {
    // Remove from entity tracker first (sends Packet29 to other players)
    if (mcServer_->entityTracker) {
        mcServer_->entityTracker->removeEntity(player);
    }

    std::string saveDir = "player_states";
    std::system(("mkdir -p " + saveDir).c_str());
    
    // Save to NBT format
    std::string saveFile = saveDir + "/" + toLower(player->username) + ".dat";
    if (player->saveToFile(saveFile)) {
        std::cout << "[INFO] Saved player data for " << player->username << std::endl;
    } else {
        std::cerr << "[WARNING] Failed to save player data for " << player->username << std::endl;
    }

    playerEntities.erase(
        std::remove(playerEntities.begin(), playerEntities.end(), player),
        playerEntities.end());
}

void ServerConfigurationManager::broadcastPacket(std::unique_ptr<Packet> pkt) {
    for (auto* player : playerEntities) {
        if (player->netHandler) {
            player->netHandler->sendPacket(pkt->clone());
        }
    }
}

void ServerConfigurationManager::broadcastChatMessage(const std::string& msg) {
    broadcastPacket(std::make_unique<Packet3Chat>(msg));
}

bool ServerConfigurationManager::sendPacketToPlayer(const std::string& username, std::unique_ptr<Packet> pkt) {
    auto* player = getPlayerEntity(username);
    if (player && player->netHandler) {
        player->netHandler->sendPacket(std::move(pkt));
        return true;
    }
    return false;
}

void ServerConfigurationManager::sendChatToPlayer(const std::string& username, const std::string& msg) {
    sendPacketToPlayer(username, std::make_unique<Packet3Chat>(msg));
}

std::string ServerConfigurationManager::getPlayerList() const {
    std::string result;
    for (size_t i = 0; i < playerEntities.size(); ++i) {
        if (i > 0) result += ", ";
        result += playerEntities[i]->username;
    }
    return result;
}

EntityPlayerMP* ServerConfigurationManager::getPlayerEntity(const std::string& username) const {
    std::string lowerName = toLower(username);
    for (auto* player : playerEntities) {
        if (toLower(player->username) == lowerName) return player;
    }
    return nullptr;
}

void ServerConfigurationManager::banPlayer(const std::string& name) {
    bannedPlayers_.insert(toLower(name));
    saveList(bannedPlayersFile_, bannedPlayers_);
}

void ServerConfigurationManager::unbanPlayer(const std::string& name) {
    bannedPlayers_.erase(toLower(name));
    saveList(bannedPlayersFile_, bannedPlayers_);
}

void ServerConfigurationManager::banIP(const std::string& ip) {
    bannedIPs_.insert(toLower(ip));
    saveList(bannedIPsFile_, bannedIPs_);
}

void ServerConfigurationManager::unbanIP(const std::string& ip) {
    bannedIPs_.erase(toLower(ip));
    saveList(bannedIPsFile_, bannedIPs_);
}

void ServerConfigurationManager::opPlayer(const std::string& name) {
    ops_.insert(toLower(name));
    saveList(opsFile_, ops_);
}

void ServerConfigurationManager::deopPlayer(const std::string& name) {
    ops_.erase(toLower(name));
    saveList(opsFile_, ops_);
}

bool ServerConfigurationManager::isOp(const std::string& name) const {
    return ops_.count(toLower(name)) > 0;
}

void ServerConfigurationManager::syncHeldItems() {
    for (auto* player : playerEntities) {
        if (player->netHandler) {
            player->savedHeldItemId = player->netHandler->getHeldItemId();
        }
    }
}

void ServerConfigurationManager::savePlayerStates() {
    std::string saveDir = "player_states";
    std::system(("mkdir -p " + saveDir).c_str());

    for (auto* player : playerEntities) {
        std::string saveFile = saveDir + "/" + toLower(player->username) + ".dat";
        if (!player->saveToFile(saveFile)) {
            std::cerr << "[WARNING] Failed to save player data for " << player->username << std::endl;
        }
    }
}

void ServerConfigurationManager::loadList(const std::string& filename, std::set<std::string>& list) {
    try {
        std::ifstream file(filename);
        std::string line;
        while (std::getline(file, line)) {
            // Trim
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
                line.pop_back();
            if (!line.empty()) {
                list.insert(toLower(line));
            }
        }
    } catch (...) {
        // File doesn't exist, that's fine
    }
}

void ServerConfigurationManager::saveList(const std::string& filename, const std::set<std::string>& list) {
    try {
        std::ofstream file(filename);
        for (const auto& entry : list) {
            file << entry << "\n";
        }
    } catch (...) {
        std::cerr << "[WARNING] Failed to save " << filename << std::endl;
    }
}

std::string ServerConfigurationManager::toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}
