1. **Add Chunk-to-Player Map**
   - In `src/server/ServerConfigurationManager.h`, add:
     ```cpp
     private:
         std::unordered_map<int64_t, std::vector<EntityPlayerMP*>> playersByChunk_;
     public:
         void markChunkLoaded(EntityPlayerMP* player, int64_t chunkKey);
         void markChunkUnloaded(EntityPlayerMP* player, int64_t chunkKey);
         const std::vector<EntityPlayerMP*>& getPlayersInChunk(int64_t chunkKey) const;
     ```
   - Implement these methods in `src/server/ServerConfigurationManager.cpp`
     - `markChunkLoaded` uses `push_back`.
     - `markChunkUnloaded` uses `std::erase(playersByChunk_[chunkKey], player)`. If the vector becomes empty, remove the key from `playersByChunk_`.
     - Return an empty vector from `getPlayersInChunk` if the key doesn't exist.

2. **Update Player Logout**
   - In `ServerConfigurationManager::playerLoggedOut`, ensure the player is removed from all chunks they had loaded (by iterating `player->netHandler->sentChunks_` and calling `markChunkUnloaded`).

3. **Hook into Chunk Load/Unload events**
   - In `src/network/NetServerHandler.cpp`, wherever `sentChunks_.insert(key)` is called, also call `mcServer_->configManager->markChunkLoaded(player_, key)`.
   - Wherever `sentChunks_.erase(key)` is called, also call `mcServer_->configManager->markChunkUnloaded(player_, key)`.

4. **Optimize Spatial Loops**
   - **`World::markBlockNeedsUpdate`** (`src/world/World.cpp:1461`):
     Change loop over `mcServer->configManager->playerEntities` to:
     ```cpp
     auto key = NetServerHandler::chunkKey(chunkX, chunkZ);
     for (auto* player : mcServer->configManager->getPlayersInChunk(key)) {
         if (player->netHandler) { // hasChunkLoaded is guaranteed by the map
             player->netHandler->sendPacket(pkt->clone());
         }
     }
     ```
   - **`ServerConfigurationManager::sendTileEntityToNearbyPlayers`** (`src/server/ServerConfigurationManager.cpp:139`):
     Change loop over `playerEntities` to:
     ```cpp
     auto key = NetServerHandler::chunkKey(chunkX, chunkZ);
     for (auto* player : getPlayersInChunk(key)) {
         auto* handler = static_cast<NetServerHandler*>(player->netHandler);
         if (!handler) continue;
         // ...
     }
     ```

5. **Pre-commit and Test**
   - Build server, run benchmark (if any), write pre-commit step for correctness.
