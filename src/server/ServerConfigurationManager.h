#pragma once

#include "../forward.h"
#include "../entity/EntityPlayerMP.h"
#include "../network/packets/AllPackets.h"

#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <memory>
#include <iostream>
#include <algorithm>

class MinecraftServer;
class NetLoginHandler;

class ServerConfigurationManager {
public:
    std::vector<EntityPlayerMP*> playerEntities;

    explicit ServerConfigurationManager(MinecraftServer* server);

    // Login: create a new player entity or reject
    EntityPlayerMP* login(NetLoginHandler* handler, const std::string& username, const std::string& password);

    void playerLoggedIn(EntityPlayerMP* player);
    void playerLoggedOut(EntityPlayerMP* player);

    // Broadcast a packet to all players
    void broadcastPacket(std::unique_ptr<Packet> pkt);

    // Send a chat message to all players
    void broadcastChatMessage(const std::string& msg);

    // Send to specific player
    bool sendPacketToPlayer(const std::string& username, std::unique_ptr<Packet> pkt);
    void sendChatToPlayer(const std::string& username, const std::string& msg);

    // Player list
    std::string getPlayerList() const;

    EntityPlayerMP* getPlayerEntity(const std::string& username) const;

    // Ban/Op management
    void banPlayer(const std::string& name);
    void unbanPlayer(const std::string& name);
    void banIP(const std::string& ip);
    void unbanIP(const std::string& ip);
    void opPlayer(const std::string& name);
    void deopPlayer(const std::string& name);
    bool isOp(const std::string& name) const;

    void savePlayerStates();
    void syncHeldItems(); // sync heldItem_ -> savedHeldItemId before save

private:
    MinecraftServer* mcServer_;
    int maxPlayers_ = 20;

    std::set<std::string> bannedPlayers_;
    std::set<std::string> bannedIPs_;
    std::set<std::string> ops_;

    std::string bannedPlayersFile_ = "banned-players.txt";
    std::string bannedIPsFile_ = "banned-ips.txt";
    std::string opsFile_ = "ops.txt";

    void loadList(const std::string& filename, std::set<std::string>& list);
    void saveList(const std::string& filename, const std::set<std::string>& list);

    static std::string toLower(const std::string& s);
};
