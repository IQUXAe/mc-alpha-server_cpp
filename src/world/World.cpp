#include "World.h"
#include "../block/Block.h"
#include "../entity/Entity.h"
#include "../entity/EntityAnimals.h"
#include "../entity/EntityMobs.h"
#include "../entity/EntityItem.h"
#include "../entity/EntityPlayerMP.h"
#include "../entity/EntityTracker.h"
#include "../network/packets/AllPackets.h"
#include "../core/AxisAlignedBB.h"
#include "../MinecraftServer.h"
#include "TileEntity.h"
#include "TileEntityChest.h"
#include "TileEntityFurnace.h"
#include "core/NBT.h"
#include "core/Logger.h"
#include <zlib.h>
#include <zstd.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <ctime>
#include <unordered_set>

// Magic prefix to distinguish zstd chunks from legacy gzip chunks
static constexpr uint32_t ZSTD_MAGIC = 0xFD2FB528; // native zstd frame magic

namespace {

std::unique_ptr<EntityAnimals> createAnimalEntity(World* world, const std::string& entityId) {
    if (entityId == "Pig") {
        return std::make_unique<EntityPig>(world);
    }
    if (entityId == "Sheep") {
        return std::make_unique<EntitySheep>(world);
    }
    if (entityId == "Cow") {
        return std::make_unique<EntityCow>(world);
    }
    if (entityId == "Chicken") {
        return std::make_unique<EntityChicken>(world);
    }
    return nullptr;
}

std::unique_ptr<EntityMob> createHostileEntity(World* world, const std::string& entityId) {
    if (entityId == "Zombie") {
        return std::make_unique<EntityZombie>(world);
    }
    if (entityId == "Skeleton") {
        return std::make_unique<EntitySkeleton>(world);
    }
    if (entityId == "Spider") {
        return std::make_unique<EntitySpider>(world);
    }
    if (entityId == "Creeper") {
        return std::make_unique<EntityCreeper>(world);
    }
    return nullptr;
}

void restorePendingAnimals(World* world, Chunk* chunk, std::vector<std::unique_ptr<Entity>>& entities,
                           EntityTracker* entityTracker) {
    if (!world || !chunk) {
        return;
    }

    for (const auto& animalData : chunk->pendingAnimals) {
        ByteBuffer buf(animalData.nbtData);
        auto root = NBTCompound::readRoot(buf);
        if (!root) {
            continue;
        }

        std::string entityId = animalData.entityId;
        if (entityId.empty()) {
            entityId = root->getString("id");
        }
        auto animal = createAnimalEntity(world, entityId);
        if (!animal) {
            continue;
        }

        animal->readFromNBT(*root);
        animal->worldObj = world;
        Entity* ptr = animal.get();
        entities.push_back(std::move(animal));
        if (entityTracker) {
            entityTracker->addEntity(ptr);
        }
    }

    chunk->pendingAnimals.clear();
}

void restorePendingMonsters(World* world, Chunk* chunk, std::vector<std::unique_ptr<Entity>>& entities,
                            EntityTracker* entityTracker) {
    if (!world || !chunk) {
        return;
    }

    for (const auto& mobData : chunk->pendingMonsters) {
        ByteBuffer buf(mobData.nbtData);
        auto root = NBTCompound::readRoot(buf);
        if (!root) {
            continue;
        }

        std::string entityId = mobData.entityId;
        if (entityId.empty()) {
            entityId = root->getString("id");
        }
        auto mob = createHostileEntity(world, entityId);
        if (!mob) {
            continue;
        }

        mob->readFromNBT(*root);
        mob->worldObj = world;
        Entity* ptr = mob.get();
        entities.push_back(std::move(mob));
        if (entityTracker) {
            entityTracker->addEntity(ptr);
        }
    }

    chunk->pendingMonsters.clear();
}

} // namespace

World::World(MinecraftServer* server, const std::string& savePath, int64_t seed)
    : mcServer(server), worldPath_(savePath) {
    // Create world directory
    std::filesystem::create_directories(worldPath_);

    // Initialize LevelDB
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, worldPath_ + "/db", &db_);
    if (!status.ok()) {
        Logger::severe("Failed to open LevelDB: {}", status.ToString());
        db_ = nullptr;
    } else {
        Logger::info("LevelDB world storage initialized at {}/db", worldPath_);
    }

    // Load level.dat or initialize new world data
    bool hasLevelDat = loadLevelDat();
    if (!hasLevelDat) {
        // Use provided seed, or generate random if seed is 0
        randomSeed = (seed != 0) ? seed : static_cast<int64_t>(time(nullptr));
        worldTime = 0;
        Logger::info("No level.dat found, generated new seed: {}", randomSeed);
    } else {
        Logger::info("Loaded level.dat, seed: {}, time: {}", randomSeed, worldTime);
    }

    // IMPORTANT: Initialize WorldChunkManager AFTER randomSeed is set!
    // In Java, WorldChunkManager is created in WorldProvider.func_4090_a() after World.randomSeed is loaded
    worldChunkManager = std::make_unique<WorldChunkManager>(randomSeed);

    // Initialize exactly same as alpha
    chunkProvider = std::make_unique<ChunkProviderGenerate>(this, randomSeed);
    asyncChunkManager_ = std::make_unique<WorldChunkManager>(randomSeed);
    asyncChunkProvider_ = std::make_unique<ChunkProviderGenerate>(this, randomSeed, asyncChunkManager_.get());
    rand.seed(randomSeed);

    saveThread_ = std::jthread([this](std::stop_token st) { saveWorker(st); });
    chunkBuildThread_ = std::jthread([this](std::stop_token st) { chunkWorker(st); });
    
    if (!hasLevelDat) {
        findSafeSpawnPoint();
        saveLevelDat();
    }

    prewarmSpawnArea();
}

void World::saveWorld(bool flushToDisk) {
    saveLevelDat();

    std::vector<Chunk*> loadedChunks;
    {
        std::lock_guard lock(chunksMutex_);
        loadedChunks.reserve(chunks_.size());
        for (const auto& [key, chunk] : chunks_) {
            if (chunk) {
                loadedChunks.push_back(chunk.get());
            }
        }
    }

    if (flushToDisk && db_) {
        waitForPendingSaves();

        int savedCount = 0;
        for (Chunk* chunk : loadedChunks) {
            saveChunkImmediate(chunk);
            ++savedCount;
        }
        Logger::info("Saved level.dat and flushed {} loaded chunks to disk.", savedCount);
        return;
    }

    int savedCount = 0;
    for (Chunk* chunk : loadedChunks) {
        const uint64_t key = getChunkKey(chunk->xPosition, chunk->zPosition);
        auto data = compressChunkData(chunk);
        {
            std::lock_guard lock(saveMutex_);
            saveQueue_.push({key, std::move(data)});
        }
        chunk->isModified = false;
        ++savedCount;
    }
    if (savedCount > 0) saveCondition_.notify_one();
    Logger::info("Saved level.dat and queued {} chunks.", savedCount);
}

void World::prewarmSpawnArea() {
    const int spawnChunkX = spawnX >> 4;
    const int spawnChunkZ = spawnZ >> 4;
    constexpr int syncRadius = 1;
    const int asyncRadius = std::clamp(mcServer ? mcServer->getViewDistance() + 1 : 4, 3, 6);

    Logger::info("Prewarming spawn area around chunk ({}, {})", spawnChunkX, spawnChunkZ);

    // Build the inner 3x3 synchronously so the first player does not wait for a cold start.
    for (int dx = -syncRadius; dx <= syncRadius; ++dx) {
        for (int dz = -syncRadius; dz <= syncRadius; ++dz) {
            getChunk(spawnChunkX + dx, spawnChunkZ + dz, true);
        }
    }

    for (int dx = -syncRadius; dx <= syncRadius; ++dx) {
        for (int dz = -syncRadius; dz <= syncRadius; ++dz) {
            ensureChunkPopulated(spawnChunkX + dx, spawnChunkZ + dz);
        }
    }

    // Queue the outer ring asynchronously so the area around spawn keeps warming up
    // while the main server loop starts accepting players.
    for (int dx = -asyncRadius; dx <= asyncRadius; ++dx) {
        for (int dz = -asyncRadius; dz <= asyncRadius; ++dz) {
            if (std::max(std::abs(dx), std::abs(dz)) <= syncRadius) {
                continue;
            }
            requestChunkAsync(spawnChunkX + dx, spawnChunkZ + dz, 0);
        }
    }
}

bool World::loadLevelDat() {
    std::ifstream file(worldPath_ + "/level.dat", std::ios::binary);
    if (!file.is_open()) return false;
    
    std::vector<uint8_t> compressed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    if (inflateInit2(&strm, 15 + 16) != Z_OK) return false;
    strm.next_in = compressed.data();
    strm.avail_in = compressed.size();
    
    std::vector<uint8_t> uncompressed(1024 * 128); // 128KB should be plenty for level.dat
    strm.next_out = uncompressed.data();
    strm.avail_out = uncompressed.size();
    
    int ret = inflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
        inflateEnd(&strm);
        return false;
    }
    uncompressed.resize(strm.total_out);
    inflateEnd(&strm);

    ByteBuffer buf(uncompressed);
    auto root = NBTCompound::readRoot(buf);
    if (!root) return false;
    
    auto data = root->getCompound("Data");
    if (!data) return false;
    
    randomSeed = data->getLong("RandomSeed");
    spawnX = data->getInt("SpawnX");
    spawnY = data->getInt("SpawnY");
    spawnZ = data->getInt("SpawnZ");
    worldTime = data->getLong("Time");
    
    return true;
}

void World::saveLevelDat() {
    NBTCompound data;
    data.setLong("RandomSeed", randomSeed);
    data.setInt("SpawnX", spawnX);
    data.setInt("SpawnY", spawnY);
    data.setInt("SpawnZ", spawnZ);
    data.setLong("Time", worldTime);
    data.setLong("SizeOnDisk", 0);
    data.setInt("version", 19132);
    data.setString("LevelName", "world");
    
    NBTCompound root;
    root.setCompound("Data", std::make_shared<NBTCompound>(data));
    
    ByteBuffer buf;
    root.writeRoot(buf, "");
    
    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return;
    
    std::vector<uint8_t> compressed(buf.data.size() + 1024);
    strm.next_in = buf.data.data();
    strm.avail_in = buf.data.size();
    strm.next_out = compressed.data();
    strm.avail_out = compressed.size();
    
    deflate(&strm, Z_FINISH);
    compressed.resize(strm.total_out);
    deflateEnd(&strm);
    
    std::ofstream file(worldPath_ + "/level.dat", std::ios::binary);
    if (file) {
        file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    }
}

World::~World() {
    // saveWorld() is already called from MinecraftServer::run() — no double save.
    // Stop saveThread_ explicitly BEFORE deleting db_, otherwise saveWorker
    // could access db_ after it has been deleted.
    stopSaving_ = true;
    saveCondition_.notify_all();
    saveThread_.request_stop();
    saveThread_.join(); // wait for all pending writes to finish

    chunkBuildCondition_.notify_all();
    chunkBuildThread_.request_stop();
    chunkBuildThread_.join();

    delete db_;
    db_ = nullptr;
}

void World::tick() {
    drainPreparedChunks();
    worldTime++;

    if (mcServer && mcServer->isSpawnMonsters()) {
        spawnHostileMobs();
    }

    if (mcServer && mcServer->isSpawnAnimals()) {
        spawnPassiveMobs();
    }

    // Update tile entities — copy pointers first to avoid deadlock,
    // since updateEntity() may call setTileEntity/removeTileEntity which lock tileEntitiesMutex_
    std::vector<TileEntity*> toUpdate;
    {
        std::lock_guard lock(tileEntitiesMutex_);
        toUpdate.reserve(tileEntities_.size());
        for (auto& [key, te] : tileEntities_) {
            if (te) toUpdate.push_back(te.get());
        }
    }
    for (TileEntity* te : toUpdate) {
        te->updateEntity();
    }

    // Process scheduled block updates
    int maxTicks = 1000;
    auto it = scheduledTicks.begin();
    while (it != scheduledTicks.end() && maxTicks-- > 0) {
        if (it->scheduledTime > worldTime) {
            break; // Since it's a set sorted by scheduledTime, subsequent entries are also in the future
        }
        
        NextTickListEntry entry = *it;
        scheduledTicks.erase(it);
        
        uint8_t currentBlock = getBlockId(entry.x, entry.y, entry.z);
        if (currentBlock == entry.blockId && currentBlock > 0) {
            if (Block::blocksList[currentBlock]) {
                Block::blocksList[currentBlock]->updateTick(this, entry.x, entry.y, entry.z);
            }
        }
        
        it = scheduledTicks.begin(); // Re-fetch begin as set might have been modified during updateTick
    }

    // Tick entities
    // Snapshot size before loop: entities spawned during tick (e.g. via getChunk->push_back)
    // will have indices >= snapshot and are processed next tick, avoiding iterator invalidation.
    const size_t entityCount = entities_.size();
    for (size_t ei = 0; ei < entityCount; ++ei) {
        if (!entities_[ei] || entities_[ei]->isDead) continue;
        if (Chunk* chunk = getChunkFromBlockCoords(static_cast<int>(std::floor(entities_[ei]->posX)),
                                                   static_cast<int>(std::floor(entities_[ei]->posZ)), false)) {
            chunk->isModified = true;
        }
        entities_[ei]->tick();
        
        // Item pickup logic
        auto* item = dynamic_cast<EntityItem*>(entities_[ei].get());
        if (item && !item->isDead && item->pickupDelay <= 0) {
            if (mcServer && mcServer->configManager) {
                for (auto* player : mcServer->configManager->playerEntities) {
                    // Java: item.boundingBox intersects player.boundingBox.expand(1.0, 0.0, 1.0)
                    // Player bbox: [posX±w/2, posY..posY+h, posZ±w/2]
                    // Expanded by (1,0,1): [posX±(w/2+1), posY..posY+h, posZ±(w/2+1)]
                    // Item bbox: [posX±0.125, posY..posY+0.25, posZ±0.125]
                    double ex = (player->width / 2.0) + 1.0 + 0.125;
                    double ez = (player->width / 2.0) + 1.0 + 0.125;
                    double minY = player->posY - 0.25;          // item height = 0.25
                    double maxY = player->posY + player->height;

                    bool inRange = std::abs(player->posX - item->posX) < ex
                                && item->posY < maxY && item->posY + 0.25 > minY
                                && std::abs(player->posZ - item->posZ) < ez;
                    if (inRange) {
                        ItemStack stack(item->itemID, item->count, item->metadata);
                        int initialCount = item->count;
                        player->inventory.addItemStackToInventory(&stack);
                        int addedCount = initialCount - stack.stackSize;

                        if (addedCount > 0) {
                            mcServer->configManager->broadcastPacket(std::make_unique<Packet22Collect>(item->entityId, player->entityId));

                            if (player->netHandler) {
                                // Packet17: shows pickup animation and tells client to add item
                                player->netHandler->sendPacket(std::make_unique<Packet17AddToInventory>(
                                    static_cast<int16_t>(item->itemID),
                                    static_cast<int8_t>(addedCount),
                                    static_cast<int16_t>(item->metadata)));
                            }

                            if (stack.stackSize <= 0) {
                                item->isDead = true;
                            } else {
                                item->count = stack.stackSize;
                            }
                        }

                        if (item->isDead) break;
                    }
                }
            }
        }

        if (entities_[ei]->isDead) {
            if (mcServer && mcServer->entityTracker) {
                mcServer->entityTracker->removeEntity(entities_[ei].get());
            }
        }
    }

    // Remove dead entities only after unregistering them from EntityTracker.
    // Some entities can already be dead before they reach the main tick loop,
    // so relying on the per-entity path above is not sufficient.
    auto* entityTracker = (mcServer && mcServer->entityTracker) ? mcServer->entityTracker.get() : nullptr;
    entities_.erase(
        std::remove_if(entities_.begin(), entities_.end(),
            [entityTracker](const std::unique_ptr<Entity>& e) {
                if (!e) {
                    return true;
                }
                if (!e->isDead) {
                    return false;
                }
                if (entityTracker) {
                    entityTracker->removeEntity(e.get());
                }
                return true;
            }),
        entities_.end());

    // Unload chunks far from all players periodically
    if (worldTime % 100 == 0) {
        int r = 0;
        if (mcServer) {
            r = mcServer->getViewDistance() + 2; // generation radius mapping
        }

        std::vector<uint64_t> toUnload;
        for (const auto& [key, chunk] : chunks_) {
            int cx = static_cast<int>(static_cast<int32_t>(key >> 32));
            int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));

            bool keep = false;
            if (mcServer && mcServer->configManager) {
                for (auto* player : mcServer->configManager->playerEntities) {
                    int px = static_cast<int>(std::floor(player->posX)) >> 4;
                    int pz = static_cast<int>(std::floor(player->posZ)) >> 4;
                    
                    if (std::abs(cx - px) <= r && std::abs(cz - pz) <= r) {
                        keep = true;
                        break;
                    }
                }
            } else {
                keep = true; // Don't unload if no server attached yet
            }

            // Spawn chunks protection could be added here (e.g. keep around 0,0)
            if (std::abs(cx) <= 3 && std::abs(cz) <= 3) {
                keep = true;
            }

            if (!keep) {
                toUnload.push_back(key);
            }
        }

        for (uint64_t key : toUnload) {
            auto it = chunks_.find(key);
            if (it != chunks_.end()) {
                Chunk* chunk = it->second.get();
                std::unordered_set<Entity*> entitiesToRemove;

                // Move live EntityItems from this chunk into pendingItems before unloading
                chunk->pendingItems.clear();
                for (auto& e : entities_) {
                    auto* item = dynamic_cast<EntityItem*>(e.get());
                    if (!item || item->isDead) continue;
                    int cx = static_cast<int>(std::floor(item->posX)) >> 4;
                    int cz = static_cast<int>(std::floor(item->posZ)) >> 4;
                    if (cx != chunk->xPosition || cz != chunk->zPosition) continue;
                    chunk->pendingItems.push_back({item->itemID, item->count, item->metadata,
                                                   item->age, item->pickupDelay,
                                                   item->posX, item->posY, item->posZ});
                    entitiesToRemove.insert(item);
                }

                chunk->pendingAnimals.clear();
                chunk->pendingMonsters.clear();
                for (auto& e : entities_) {
                    auto* animal = dynamic_cast<EntityAnimals*>(e.get());
                    if (!animal || animal->isDead) continue;
                    const int cx = static_cast<int>(std::floor(animal->posX)) >> 4;
                    const int cz = static_cast<int>(std::floor(animal->posZ)) >> 4;
                    if (cx != chunk->xPosition || cz != chunk->zPosition) continue;

                    NBTCompound nbt;
                    animal->writeToNBT(nbt);
                    ByteBuffer buf;
                    nbt.writeRoot(buf, animal->getEntityStringId());
                    chunk->pendingAnimals.push_back({
                        .entityId = animal->getEntityStringId(),
                        .nbtData = std::move(buf.data),
                        .posX = animal->posX,
                        .posY = animal->posY,
                        .posZ = animal->posZ,
                    });
                    entitiesToRemove.insert(animal);
                }

                for (auto& e : entities_) {
                    auto* mob = dynamic_cast<EntityMob*>(e.get());
                    if (!mob || mob->isDead) continue;
                    const int cx = static_cast<int>(std::floor(mob->posX)) >> 4;
                    const int cz = static_cast<int>(std::floor(mob->posZ)) >> 4;
                    if (cx != chunk->xPosition || cz != chunk->zPosition) continue;

                    NBTCompound nbt;
                    mob->writeToNBT(nbt);
                    ByteBuffer buf;
                    nbt.writeRoot(buf, mob->getEntityStringId());
                    chunk->pendingMonsters.push_back({
                        .entityId = mob->getEntityStringId(),
                        .nbtData = std::move(buf.data),
                        .posX = mob->posX,
                        .posY = mob->posY,
                        .posZ = mob->posZ,
                    });
                    entitiesToRemove.insert(mob);
                }

                if (!entitiesToRemove.empty()) {
                    if (mcServer && mcServer->entityTracker) {
                        for (Entity* entity : entitiesToRemove) {
                            mcServer->entityTracker->removeEntity(entity);
                        }
                    }

                    entities_.erase(
                        std::remove_if(entities_.begin(), entities_.end(),
                            [&entitiesToRemove](const std::unique_ptr<Entity>& e) {
                                return e && entitiesToRemove.contains(e.get());
                            }),
                        entities_.end());
                }

                bool shouldSave = true;
                if (mcServer && mcServer->isSaveModifiedChunksOnly() && !chunk->isModified
                    && chunk->pendingItems.empty() && chunk->pendingAnimals.empty()
                    && chunk->pendingMonsters.empty()) {
                    shouldSave = false;
                }

                if (shouldSave) {
                    const bool hasPendingEntities =
                        !chunk->pendingItems.empty() ||
                        !chunk->pendingAnimals.empty() ||
                        !chunk->pendingMonsters.empty();

                    // Entity unload data is critical; persist synchronously to avoid
                    // losing mobs/items if the process is restarted right after logout.
                    if (hasPendingEntities && db_) {
                        saveChunkImmediate(chunk);
                    } else {
                        std::vector<uint8_t> data = compressChunkData(chunk);
                        {
                            std::lock_guard<std::mutex> lock(saveMutex_);
                            saveQueue_.push({key, data});
                        }
                        saveCondition_.notify_one();
                    }
                }

                // Remove all TileEntities belonging to this chunk so they stop ticking
                {
                    std::lock_guard lock(tileEntitiesMutex_);
                    for (const auto& [teKey, te] : chunk->getTileEntities()) {
                        if (te) tileEntities_.erase(getTileEntityKey(te->xCoord, te->yCoord, te->zCoord));
                    }
                }

                chunks_.erase(it);
            }
        }
    }

    if (mcServer && mcServer->getAutoSaveInterval() > 0 && worldTime % mcServer->getAutoSaveInterval() == 0) {
        int savedCount = 0;
        for (const auto& [key, chunk] : chunks_) {
            if (mcServer->isSaveModifiedChunksOnly() && !chunk->isModified) continue;
            auto data = compressChunkData(chunk.get());
            {
                std::lock_guard lock(saveMutex_);
                saveQueue_.push({key, std::move(data)});
            }
            chunk->isModified = false;
            ++savedCount;
        }
        if (savedCount > 0) {
            Logger::info("Auto-saved {} chunks.", savedCount);
            saveCondition_.notify_one();
        }
    }
}

void World::scheduleBlockUpdate(int x, int y, int z, int blockId, int delay) {
    NextTickListEntry entry;
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.blockId = blockId;
    entry.scheduledTime = worldTime + delay;
    scheduledTicks.insert(entry);
}

void World::requestChunkAsync(int chunkX, int chunkZ, int priority) {
    const auto key = getChunkKey(chunkX, chunkZ);

    {
        std::lock_guard lock(chunksMutex_);
        if (chunks_.contains(key)) {
            return;
        }
    }

    {
        std::lock_guard lock(chunkBuildMutex_);
        if (chunkBuildInFlight_.contains(key) || preparedChunks_.contains(key)) {
            return;
        }
        chunkBuildInFlight_.insert(key);
        chunkBuildQueue_.push(ChunkBuildRequest{
            .key = key,
            .priority = priority,
            .sequence = chunkBuildSequence_++,
        });
    }

    chunkBuildCondition_.notify_one();
}

void World::chunkWorker(std::stop_token st) {
    while (!st.stop_requested()) {
        uint64_t key = 0;
        {
            std::unique_lock lock(chunkBuildMutex_);
            chunkBuildCondition_.wait(lock, [&] {
                return st.stop_requested() || !chunkBuildQueue_.empty();
            });

            if (st.stop_requested()) {
                return;
            }

            key = chunkBuildQueue_.top().key;
            chunkBuildQueue_.pop();
        }

        const int chunkX = static_cast<int32_t>(key >> 32);
        const int chunkZ = static_cast<int32_t>(key & 0xFFFFFFFFu);

        PreparedChunk prepared;

        bool loadedFromStorage = false;
        if (db_) {
            std::string val;
            leveldb::Slice keySlice(reinterpret_cast<const char*>(&key), sizeof(uint64_t));
            const auto status = db_->Get(leveldb::ReadOptions(), keySlice, &val);
            if (status.ok()) {
                auto chunk = std::make_unique<Chunk>(this, chunkX, chunkZ);
                std::vector<uint8_t> data(val.begin(), val.end());
                decompressChunkData(chunk.get(), data, &prepared.tileEntities);
                prepared.chunk = std::move(chunk);
                loadedFromStorage = true;
            }
        }

        if (!loadedFromStorage) {
            auto chunk = std::make_unique<Chunk>(this, chunkX, chunkZ);
            asyncChunkProvider_->generateChunk(*chunk, false);
            prepared.chunk = std::move(chunk);
        }

        {
            std::lock_guard lock(chunkBuildMutex_);
            if (prepared.chunk) {
                preparedChunks_[key] = std::move(prepared);
                preparedChunkOrder_.push(key);
            }
        }
    }
}

void World::drainPreparedChunks(size_t maxChunks) {
    for (size_t i = 0; i < maxChunks; ++i) {
        uint64_t key = 0;
        bool found = false;
        {
            std::lock_guard lock(chunkBuildMutex_);
            while (!preparedChunkOrder_.empty()) {
                const uint64_t candidate = preparedChunkOrder_.front();
                preparedChunkOrder_.pop();
                if (preparedChunks_.contains(candidate)) {
                    key = candidate;
                    found = true;
                    break;
                }
            }
        }

        if (!found || !commitPreparedChunk(key)) {
            break;
        }
    }
}

bool World::commitPreparedChunk(uint64_t key) {
    PreparedChunk prepared;
    {
        std::lock_guard lock(chunkBuildMutex_);
        auto it = preparedChunks_.find(key);
        if (it == preparedChunks_.end()) {
            return false;
        }
        prepared = std::move(it->second);
        preparedChunks_.erase(it);
        chunkBuildInFlight_.erase(key);
    }

    if (!prepared.chunk) {
        return true;
    }

    {
        std::lock_guard lock(chunksMutex_);
        if (chunks_.contains(key)) {
            return true;
        }
        chunks_[key] = std::move(prepared.chunk);
    }

    Chunk* chunk = nullptr;
    {
        std::lock_guard lock(chunksMutex_);
        chunk = chunks_[key].get();
    }
    if (!chunk) {
        return true;
    }

    if (chunk->heightMap.size() != CHUNK_AREA) {
        chunk->heightMap.assign(CHUNK_AREA, 0);
        chunk->generateHeightMap();
    }
    if (chunk->skylight.data.size() != CHUNK_VOLUME / 2 || chunk->blocklight.data.size() != CHUNK_VOLUME / 2) {
        chunk->skylight.data.resize(CHUNK_VOLUME / 2, 0);
        chunk->blocklight.data.resize(CHUNK_VOLUME / 2, 0);
        chunk->generateSkylightMap();
    }

    {
        std::lock_guard lock(tileEntitiesMutex_);
        for (auto& te : prepared.tileEntities) {
            if (!te) continue;
            te->worldObj = this;
            chunk->addTileEntity(te.get());
            tileEntities_[getTileEntityKey(te->xCoord, te->yCoord, te->zCoord)] = std::move(te);
        }
    }

    for (const auto& ed : chunk->pendingItems) {
        auto item = std::make_unique<EntityItem>(ed.itemID, ed.count, ed.metadata);
        item->setPosition(ed.posX, ed.posY, ed.posZ);
        item->age = ed.age;
        item->pickupDelay = ed.pickupDelay;
        item->worldObj = this;
        entities_.push_back(std::move(item));
    }
    chunk->pendingItems.clear();

    restorePendingAnimals(this, chunk, entities_, mcServer ? mcServer->entityTracker.get() : nullptr);
    restorePendingMonsters(this, chunk, entities_, mcServer ? mcServer->entityTracker.get() : nullptr);

    tryPopulateChunksAround(chunk->xPosition, chunk->zPosition);
    return true;
}

void World::populateChunkIfReady(int chunkX, int chunkZ) {
    commitPreparedChunk(getChunkKey(chunkX, chunkZ));
    commitPreparedChunk(getChunkKey(chunkX + 1, chunkZ));
    commitPreparedChunk(getChunkKey(chunkX, chunkZ + 1));
    commitPreparedChunk(getChunkKey(chunkX + 1, chunkZ + 1));

    Chunk* c = nullptr;
    Chunk* c1 = nullptr;
    Chunk* c2 = nullptr;
    Chunk* c3 = nullptr;
    bool shouldPopulate = false;

    {
        std::lock_guard lock(chunksMutex_);
        auto it = chunks_.find(getChunkKey(chunkX, chunkZ));
        if (it == chunks_.end()) return;

        c = it->second.get();
        if (!c || c->isTerrainPopulated) return;

        auto it1 = chunks_.find(getChunkKey(chunkX + 1, chunkZ));
        auto it2 = chunks_.find(getChunkKey(chunkX, chunkZ + 1));
        auto it3 = chunks_.find(getChunkKey(chunkX + 1, chunkZ + 1));

        if (it1 != chunks_.end() && it2 != chunks_.end() && it3 != chunks_.end()) {
            shouldPopulate = true;
            c1 = it1->second.get();
            c2 = it2->second.get();
            c3 = it3->second.get();
        }
    }

    if (shouldPopulate && c) {
        c->isTerrainPopulated = true;
        isPopulating = true;
        chunkProvider->populate(chunkX, chunkZ);

        c->generateSkylightMap();
        if (c1) c1->generateSkylightMap();
        if (c2) c2->generateSkylightMap();
        if (c3) c3->generateSkylightMap();
        isPopulating = false;
    }
}

void World::tryPopulateChunksAround(int chunkX, int chunkZ) {
    populateChunkIfReady(chunkX, chunkZ);
    populateChunkIfReady(chunkX - 1, chunkZ);
    populateChunkIfReady(chunkX, chunkZ - 1);
    populateChunkIfReady(chunkX - 1, chunkZ - 1);
}

Chunk* World::getChunk(int chunkX, int chunkZ, bool generate) {
    auto key = getChunkKey(chunkX, chunkZ);

    commitPreparedChunk(key);
    
    {
        std::lock_guard<std::mutex> lock(chunksMutex_);
        auto it = chunks_.find(key);
        if (it != chunks_.end()) return it->second.get();
    }

    // Try loading from LevelDB
    if (db_) {
        std::string val;
        leveldb::Slice keySlice(reinterpret_cast<const char*>(&key), sizeof(uint64_t));
        leveldb::Status status = db_->Get(leveldb::ReadOptions(), keySlice, &val);
        if (status.ok()) {
            auto chunk = std::make_unique<Chunk>(this, chunkX, chunkZ);
            std::vector<uint8_t> data(val.begin(), val.end());
            decompressChunkData(chunk.get(), data);
            Chunk* ptr = chunk.get();
            {
                std::lock_guard<std::mutex> lock(chunksMutex_);
                chunks_[key] = std::move(chunk);
            }
            // Spawn EntityItems that were frozen while chunk was unloaded
            for (const auto& ed : ptr->pendingItems) {
                auto item = std::make_unique<EntityItem>(ed.itemID, ed.count, ed.metadata);
                item->setPosition(ed.posX, ed.posY, ed.posZ);
                item->age         = ed.age;
                item->pickupDelay = ed.pickupDelay;
                item->worldObj    = this;
                entities_.push_back(std::move(item));
            }
            ptr->pendingItems.clear();
            restorePendingAnimals(this, ptr, entities_, mcServer ? mcServer->entityTracker.get() : nullptr);
            restorePendingMonsters(this, ptr, entities_, mcServer ? mcServer->entityTracker.get() : nullptr);
            return ptr;
        }
    }

    if (!generate) return nullptr;

    // Create empty chunk and add to map FIRST, so neighbors can find it
    auto chunk = std::make_unique<Chunk>(this, chunkX, chunkZ);
    Chunk* ptr = chunk.get();
    
    {
        std::lock_guard<std::mutex> lock(chunksMutex_);
        chunks_[key] = std::move(chunk);
    }
    
    chunkProvider->generateChunk(*ptr, true);
    tryPopulateChunksAround(chunkX, chunkZ);

    return ptr;
}

Chunk* World::getChunkFromBlockCoords(int x, int z, bool generate) {
    return getChunk(x >> 4, z >> 4, generate);
}

bool World::chunkExists(int chunkX, int chunkZ) const {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    return chunks_.find(getChunkKey(chunkX, chunkZ)) != chunks_.end();
}

void World::ensureChunkPopulated(int chunkX, int chunkZ) {
    populateChunkIfReady(chunkX, chunkZ);
}

uint8_t World::getBlockId(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 0;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return 0;

    return chunk->getBlockID(x & 15, y, z & 15);
}

uint8_t World::getBlockIdNoChunkLoad(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 0;

    Chunk* chunk = getChunkFromBlockCoords(x, z, false);
    if (!chunk) return 0;

    return chunk->getBlockID(x & 15, y, z & 15);
}

uint8_t World::getBlockMetadata(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 0;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return 0;

    return chunk->getBlockMetadata(x & 15, y, z & 15);
}

bool World::setBlock(int x, int y, int z, uint8_t blockId) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return false;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return false;

    bool placed = chunk->setBlockIDWithMetadata(x & 15, y, z & 15, blockId, 0);
    return placed;
}

bool World::setBlockWithNotify(int x, int y, int z, uint8_t blockId) {
    uint8_t oldId = getBlockId(x, y, z);
    if (!setBlock(x, y, z, blockId)) return false;

    // Notify old block it was removed
    if (oldId > 0 && Block::blocksList[oldId])
        Block::blocksList[oldId]->onBlockRemoval(this, x, y, z);

    // Call onBlockAdded BEFORE notifying client so metadata is set correctly
    if (blockId > 0 && Block::blocksList[blockId])
        Block::blocksList[blockId]->onBlockAdded(this, x, y, z);

    // Send block update with final metadata (after onBlockAdded may have changed it)
    markBlockNeedsUpdate(x, y, z);
    notifyBlocksOfNeighborChange(x, y, z, blockId);
    return true;
}

bool World::setBlockWithNotifyNoClientUpdate(int x, int y, int z, uint8_t blockId) {
    uint8_t oldId = getBlockId(x, y, z);
    if (!setBlock(x, y, z, blockId)) return false;
    if (oldId > 0 && Block::blocksList[oldId])
        Block::blocksList[oldId]->onBlockRemoval(this, x, y, z);
    if (blockId > 0 && Block::blocksList[blockId])
        Block::blocksList[blockId]->onBlockAdded(this, x, y, z);
    notifyBlocksOfNeighborChange(x, y, z, blockId);
    return true;
}

bool World::setBlockAndUpdate(int x, int y, int z, uint8_t blockId) {
    if (!setBlock(x, y, z, blockId)) return false;
    markBlockNeedsUpdate(x, y, z);
    return true;
}

bool World::setBlockAndMetadata(int x, int y, int z, uint8_t blockId, uint8_t metadata) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return false;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return false;

    return chunk->setBlockIDWithMetadata(x & 15, y, z & 15, blockId, metadata);
}

bool World::setBlockAndMetadataWithNotify(int x, int y, int z, uint8_t blockId, uint8_t metadata) {
    if (!setBlockAndMetadata(x, y, z, blockId, metadata)) return false;

    if (blockId > 0 && Block::blocksList[blockId])
        Block::blocksList[blockId]->onBlockAdded(this, x, y, z);

    markBlockNeedsUpdate(x, y, z);
    notifyBlocksOfNeighborChange(x, y, z, blockId);
    return true;
}

bool World::setBlockMetadata(int x, int y, int z, uint8_t metadata) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return false;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return false;

    chunk->setBlockMetadata(x & 15, y, z & 15, metadata);
    chunk->isModified = true; // Mark chunk as modified so metadata is saved
    return true;
}

void World::markBlockNeedsUpdate(int x, int y, int z) {
    if (!mcServer || !mcServer->configManager) return;

    int chunkX = x >> 4;
    int chunkZ = z >> 4;
    auto pkt = std::make_unique<Packet53BlockChange>(x, static_cast<int8_t>(y), z,
                            static_cast<int8_t>(getBlockId(x, y, z)),
                            static_cast<int8_t>(getBlockMetadata(x, y, z)));

    // Only send to players who have this chunk loaded
    for (auto* player : mcServer->configManager->playerEntities) {
        if (player->netHandler && player->netHandler->hasChunkLoaded(
                player->netHandler->chunkKey(chunkX, chunkZ))) {
            player->netHandler->sendPacket(pkt->clone());
        }
    }
}

void World::notifyBlocksOfNeighborChange(int x, int y, int z, uint8_t blockId) {
    auto notify = [&](int nx, int ny, int nz) {
        uint8_t nId = getBlockId(nx, ny, nz);
        if (nId > 0 && Block::blocksList[nId]) {
            Block::blocksList[nId]->onNeighborBlockChange(this, nx, ny, nz, blockId);
        }
    };

    notify(x - 1, y, z);
    notify(x + 1, y, z);
    notify(x, y - 1, z);
    notify(x, y + 1, z);
    notify(x, y, z - 1);
    notify(x, y, z + 1);
}

// Get height at world coordinates (top non-air block Y)
int World::getHeightValue(int x, int z) {
    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (!chunk) return 0;
    return chunk->getHeightValue(x & 15, z & 15);
}

int World::getBlockLightValue(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 0;
    Chunk* chunk = getChunkFromBlockCoords(x, z, false);
    if (!chunk) return 0;
    const int localX = x & 15;
    const int localZ = z & 15;
    return std::max(chunk->getSavedLightValue(0, localX, y, localZ),
                    chunk->getSavedLightValue(1, localX, y, localZ));
}

int World::getSavedLightValue(int type, int x, int y, int z) {
    if (y < 0) {
        return 0;
    }
    if (y >= CHUNK_SIZE_Y) {
        return type == 0 ? 15 : 0;
    }

    Chunk* chunk = getChunkFromBlockCoords(x, z, false);
    if (!chunk) {
        return 0;
    }
    return chunk->getSavedLightValue(type, x & 15, y, z & 15);
}

bool World::canBlockSeeSky(int x, int y, int z) {
    if (y < 0) {
        return false;
    }
    if (y >= CHUNK_SIZE_Y) {
        return true;
    }

    Chunk* chunk = getChunkFromBlockCoords(x, z, false);
    if (!chunk) {
        return false;
    }
    return y >= chunk->getHeightValue(x & 15, z & 15);
}

bool World::isDaytime() const {
    const int64_t timeOfDay = worldTime % 24000;
    return timeOfDay >= 0 && timeOfDay < 12000;
}

EntityPlayerMP* World::getClosestPlayer(double x, double y, double z, double maxDistance) const {
    if (!mcServer || !mcServer->configManager) return nullptr;
    EntityPlayerMP* closest = nullptr;
    double bestSq = maxDistance * maxDistance;
    for (auto* player : mcServer->configManager->playerEntities) {
        if (!player) continue;
        const double dx = player->posX - x;
        const double dy = player->posY - y;
        const double dz = player->posZ - z;
        const double distSq = dx * dx + dy * dy + dz * dz;
        if (distSq < bestSq) {
            bestSq = distSq;
            closest = player;
        }
    }
    return closest;
}

int World::countHostileMobs() const {
    int count = 0;
    for (const auto& entity : entities_) {
        if (entity && !entity->isDead && dynamic_cast<EntityMob*>(entity.get())) {
            ++count;
        }
    }
    return count;
}

int World::countPassiveAnimals() const {
    int count = 0;
    for (const auto& entity : entities_) {
        if (entity && !entity->isDead && dynamic_cast<EntityAnimals*>(entity.get())) {
            ++count;
        }
    }
    return count;
}

void World::spawnHostileMobs() {
    if (!mcServer || !mcServer->configManager || mcServer->configManager->playerEntities.empty()) {
        return;
    }

    std::unordered_set<uint64_t> eligibleChunks;
    constexpr int chunkRadius = 8;
    for (auto* player : mcServer->configManager->playerEntities) {
        if (!player) continue;
        const int chunkX = MathHelper::floor_double(player->posX / 16.0);
        const int chunkZ = MathHelper::floor_double(player->posZ / 16.0);
        for (int dx = -chunkRadius; dx <= chunkRadius; ++dx) {
            for (int dz = -chunkRadius; dz <= chunkRadius; ++dz) {
                eligibleChunks.insert(getChunkKey(chunkX + dx, chunkZ + dz));
            }
        }
    }

    if (eligibleChunks.empty()) {
        return;
    }

    const int maxCreatures = 100 * static_cast<int>(eligibleChunks.size()) / 256;
    if (countHostileMobs() > maxCreatures) {
        return;
    }

    for (const uint64_t key : eligibleChunks) {
        if ((std::rand() % 50) != 0) {
            continue;
        }

        const int chunkX = static_cast<int32_t>(key >> 32);
        const int chunkZ = static_cast<int32_t>(key & 0xFFFFFFFFu);
        if (!chunkExists(chunkX, chunkZ)) {
            continue;
        }

        const int baseX = chunkX * 16;
        const int baseZ = chunkZ * 16;
        const int selectedMob = std::rand() % 4; // Java picks mob class once per chunk attempt.
        const int originX = baseX + (std::rand() % 16);
        const int originY = std::rand() % CHUNK_SIZE_Y;
        const int originZ = baseZ + (std::rand() % 16);

        if (isBlockSolidNoChunkLoad(originX, originY, originZ)
            || getBlockMaterialNoChunkLoad(originX, originY, originZ) != &Material::air) {
            continue;
        }

        bool moveToNextChunk = false;
        for (int groupAttempt = 0; groupAttempt < 3 && !moveToNextChunk; ++groupAttempt) {
            int groupCount = 0;
            int x = originX;
            int y = originY;
            int z = originZ;

            for (int packAttempt = 0; packAttempt < 4; ++packAttempt) {
                x += (std::rand() % 6) - (std::rand() % 6);
                z += (std::rand() % 6) - (std::rand() % 6);

                if (!isBlockSolidNoChunkLoad(x, y - 1, z)
                    || isBlockSolidNoChunkLoad(x, y, z)
                    || getBlockMaterialNoChunkLoad(x, y, z)->getIsLiquid()
                    || isBlockSolidNoChunkLoad(x, y + 1, z)) {
                    continue;
                }

                const float fx = static_cast<float>(x) + 0.5f;
                const float fy = static_cast<float>(y);
                const float fz = static_cast<float>(z) + 0.5f;
                if (getClosestPlayer(fx, fy, fz, 24.0) != nullptr) {
                    continue;
                }

                const float dsx = fx - static_cast<float>(spawnX);
                const float dsy = fy - static_cast<float>(spawnY);
                const float dsz = fz - static_cast<float>(spawnZ);
                if (dsx * dsx + dsy * dsy + dsz * dsz < 576.0f) {
                    continue;
                }

                std::unique_ptr<EntityMob> mob;
                switch (selectedMob) {
                    case 0: mob = std::make_unique<EntitySpider>(this); break;
                    case 1: mob = std::make_unique<EntityZombie>(this); break;
                    case 2: mob = std::make_unique<EntitySkeleton>(this); break;
                    default: mob = std::make_unique<EntityCreeper>(this); break;
                }
                if (!mob) {
                    continue;
                }

                mob->setPositionAndRotation(fx, fy, fz, static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 360.0f, 0.0f);
                if (!mob->getCanSpawnHere()) {
                    continue;
                }

                const int maxInChunk = mob->getMaxSpawnedInChunk();
                EntityMob* spawnedMob = mob.get();
                spawnEntityInWorld(std::move(mob));
                ++groupCount;

                // Alpha spider jockey chance.
                if (dynamic_cast<EntitySpider*>(spawnedMob) != nullptr && (std::rand() % 100) == 0) {
                    auto skeleton = std::make_unique<EntitySkeleton>(this);
                    skeleton->setPositionAndRotation(fx, fy, fz, spawnedMob->rotationYaw, 0.0f);
                    EntitySkeleton* skelPtr = skeleton.get();
                    spawnEntityInWorld(std::move(skeleton));
                    skelPtr->mountEntity(spawnedMob);
                }

                if (groupCount >= maxInChunk) {
                    moveToNextChunk = true; // Java: continue label110
                    break;
                }
            }
        }
    }
}

void World::spawnPassiveMobs() {
    if (!mcServer || !mcServer->configManager || mcServer->configManager->playerEntities.empty()) {
        return;
    }

    std::unordered_set<uint64_t> eligibleChunks;
    constexpr int chunkRadius = 8;
    for (auto* player : mcServer->configManager->playerEntities) {
        if (!player) continue;
        const int chunkX = MathHelper::floor_double(player->posX / 16.0);
        const int chunkZ = MathHelper::floor_double(player->posZ / 16.0);
        for (int dx = -chunkRadius; dx <= chunkRadius; ++dx) {
            for (int dz = -chunkRadius; dz <= chunkRadius; ++dz) {
                eligibleChunks.insert(getChunkKey(chunkX + dx, chunkZ + dz));
            }
        }
    }

    if (eligibleChunks.empty()) {
        return;
    }

    const int maxCreatures = 20 * static_cast<int>(eligibleChunks.size()) / 256;
    if (countPassiveAnimals() > maxCreatures) {
        return;
    }

    for (const uint64_t key : eligibleChunks) {
        if ((std::rand() % 50) != 0) {
            continue;
        }

        const int chunkX = static_cast<int32_t>(key >> 32);
        const int chunkZ = static_cast<int32_t>(key & 0xFFFFFFFFu);
        if (!chunkExists(chunkX, chunkZ)) {
            continue;
        }

        const int baseX = chunkX * 16;
        const int baseZ = chunkZ * 16;
        const int selectedAnimal = std::rand() % 4; // Java picks class once per chunk attempt.
        const int originX = baseX + (std::rand() % 16);
        const int originY = std::rand() % CHUNK_SIZE_Y;
        const int originZ = baseZ + (std::rand() % 16);

        if (isBlockSolidNoChunkLoad(originX, originY, originZ)
            || getBlockMaterialNoChunkLoad(originX, originY, originZ) != &Material::air) {
            continue;
        }

        bool moveToNextChunk = false;
        for (int groupAttempt = 0; groupAttempt < 3 && !moveToNextChunk; ++groupAttempt) {
            int groupCount = 0;
            int x = originX;
            int y = originY;
            int z = originZ;

            for (int packAttempt = 0; packAttempt < 4; ++packAttempt) {
                x += (std::rand() % 6) - (std::rand() % 6);
                z += (std::rand() % 6) - (std::rand() % 6);

                if (!isBlockSolidNoChunkLoad(x, y - 1, z)
                    || isBlockSolidNoChunkLoad(x, y, z)
                    || getBlockMaterialNoChunkLoad(x, y, z)->getIsLiquid()
                    || isBlockSolidNoChunkLoad(x, y + 1, z)) {
                    continue;
                }

                const float fx = static_cast<float>(x) + 0.5f;
                const float fy = static_cast<float>(y);
                const float fz = static_cast<float>(z) + 0.5f;
                if (getClosestPlayer(fx, fy, fz, 24.0) != nullptr) {
                    continue;
                }

                const float dsx = fx - static_cast<float>(spawnX);
                const float dsy = fy - static_cast<float>(spawnY);
                const float dsz = fz - static_cast<float>(spawnZ);
                if (dsx * dsx + dsy * dsy + dsz * dsz < 576.0f) {
                    continue;
                }

                std::unique_ptr<EntityAnimals> animal;
                switch (selectedAnimal) {
                    case 0: animal = std::make_unique<EntitySheep>(this); break;
                    case 1: animal = std::make_unique<EntityPig>(this); break;
                    case 2: animal = std::make_unique<EntityChicken>(this); break;
                    default: animal = std::make_unique<EntityCow>(this); break;
                }
                if (!animal) {
                    continue;
                }

                animal->setPositionAndRotation(fx, fy, fz, static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 360.0f, 0.0f);
                if (!animal->getCanSpawnHere()) {
                    continue;
                }

                const int maxInChunk = animal->getMaxSpawnedInChunk();
                spawnEntityInWorld(std::move(animal));
                ++groupCount;
                if (groupCount >= maxInChunk) {
                    moveToNextChunk = true; // Java: continue label110
                    break;
                }
            }
        }
    }
}

// Returns the material of the block at the given position
Material* World::getBlockMaterial(int x, int y, int z) {
    uint8_t id = getBlockId(x, y, z);
    if (id == 0 || !Block::blocksList[id]) return &Material::air;
    return Block::blocksList[id]->blockMaterial;
}

Material* World::getBlockMaterialNoChunkLoad(int x, int y, int z) {
    uint8_t id = getBlockIdNoChunkLoad(x, y, z);
    if (id == 0 || !Block::blocksList[id]) return &Material::air;
    return Block::blocksList[id]->blockMaterial;
}

// Check if block at position is solid (for snow placement etc.)
bool World::isBlockSolid(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return false;
    uint8_t id = getBlockId(x, y, z);
    if (id == 0) return false;
    // Non-solid blocks: air(0), water(8,9), lava(10,11), snow layers(78), flowers(37,38),
    // mushrooms(39,40), reeds(83), fire(51), etc.
    if (id == 8 || id == 9 || id == 10 || id == 11) return false;
    if (id == 78 || id == 37 || id == 38 || id == 39 || id == 40) return false;
    if (id == 83 || id == 51 || id == 6) return false;
    return true;
}

bool World::isBlockSolidNoChunkLoad(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return false;
    uint8_t id = getBlockIdNoChunkLoad(x, y, z);
    if (id == 0) return false;
    if (id == 8 || id == 9 || id == 10 || id == 11) return false;
    if (id == 78 || id == 37 || id == 38 || id == 39 || id == 40) return false;
    if (id == 83 || id == 51 || id == 6) return false;
    return true;
}

void World::getCollidingBoundingBoxes(Entity* entity, const AxisAlignedBB& mask, std::vector<AxisAlignedBB>& result) {
    int minX = std::floor(mask.minX);
    int maxX = std::floor(mask.maxX + 1.0);
    int minY = std::floor(mask.minY);
    int maxY = std::floor(mask.maxY + 1.0);
    int minZ = std::floor(mask.minZ);
    int maxZ = std::floor(mask.maxZ + 1.0);

    for (int x = minX; x < maxX; ++x) {
        for (int y = minY; y < maxY; ++y) {
            for (int z = minZ; z < maxZ; ++z) {
                uint8_t bId = getBlockId(x, y, z);
                if (bId > 0 && Block::blocksList[bId]) {
                    Block::blocksList[bId]->getCollidingBoundingBoxes(this, x, y, z, mask, result);
                }
            }
        }
    }
}

void World::getEntitiesWithinAABBExcludingEntity(Entity* entity, const AxisAlignedBB& mask, std::vector<Entity*>& result) {
    result.clear();
    for (auto& candidate : entities_) {
        if (!candidate || candidate.get() == entity || candidate->isDead) {
            continue;
        }
        if (candidate->boundingBox.intersectsWith(mask)) {
            result.push_back(candidate.get());
        }
    }

    if (!mcServer || !mcServer->configManager) {
        return;
    }

    for (EntityPlayerMP* player : mcServer->configManager->playerEntities) {
        if (!player || player == entity || player->isDead) {
            continue;
        }
        if (player->boundingBox.intersectsWith(mask)) {
            result.push_back(player);
        }
    }
}

bool World::isPlacementVolumeClear(const AxisAlignedBB& mask) {
    std::vector<Entity*> entities;
    getEntitiesWithinAABBExcludingEntity(nullptr, mask, entities);
    for (Entity* entity : entities) {
        if (entity && !entity->isDead && entity->preventsEntitySpawning()) {
            return false;
        }
    }
    return true;
}

std::optional<MovingObjectPosition> World::rayTraceBlocks(const Vec3D& from, const Vec3D& to, bool includeLiquids) {
    Vec3D current = from;
    const Vec3D target = to;

    if (std::isnan(current.xCoord) || std::isnan(current.yCoord) || std::isnan(current.zCoord)
        || std::isnan(target.xCoord) || std::isnan(target.yCoord) || std::isnan(target.zCoord)) {
        return std::nullopt;
    }

    int targetX = MathHelper::floor_double(target.xCoord);
    int targetY = MathHelper::floor_double(target.yCoord);
    int targetZ = MathHelper::floor_double(target.zCoord);
    int currentX = MathHelper::floor_double(current.xCoord);
    int currentY = MathHelper::floor_double(current.yCoord);
    int currentZ = MathHelper::floor_double(current.zCoord);

    for (int steps = 0; steps <= 200; ++steps) {
        if (std::isnan(current.xCoord) || std::isnan(current.yCoord) || std::isnan(current.zCoord)) {
            return std::nullopt;
        }

        if (currentX == targetX && currentY == targetY && currentZ == targetZ) {
            return std::nullopt;
        }

        double nextBoundaryX = 999.0;
        double nextBoundaryY = 999.0;
        double nextBoundaryZ = 999.0;
        if (targetX > currentX) nextBoundaryX = static_cast<double>(currentX) + 1.0;
        if (targetX < currentX) nextBoundaryX = static_cast<double>(currentX);
        if (targetY > currentY) nextBoundaryY = static_cast<double>(currentY) + 1.0;
        if (targetY < currentY) nextBoundaryY = static_cast<double>(currentY);
        if (targetZ > currentZ) nextBoundaryZ = static_cast<double>(currentZ) + 1.0;
        if (targetZ < currentZ) nextBoundaryZ = static_cast<double>(currentZ);

        const double deltaX = target.xCoord - current.xCoord;
        const double deltaY = target.yCoord - current.yCoord;
        const double deltaZ = target.zCoord - current.zCoord;

        double scaleX = 999.0;
        double scaleY = 999.0;
        double scaleZ = 999.0;
        if (nextBoundaryX != 999.0 && std::abs(deltaX) > 1.0E-7) scaleX = (nextBoundaryX - current.xCoord) / deltaX;
        if (nextBoundaryY != 999.0 && std::abs(deltaY) > 1.0E-7) scaleY = (nextBoundaryY - current.yCoord) / deltaY;
        if (nextBoundaryZ != 999.0 && std::abs(deltaZ) > 1.0E-7) scaleZ = (nextBoundaryZ - current.zCoord) / deltaZ;

        int8_t crossedSide = 0;
        if (scaleX < scaleY && scaleX < scaleZ) {
            crossedSide = targetX > currentX ? 4 : 5;
            current.xCoord = nextBoundaryX;
            current.yCoord += deltaY * scaleX;
            current.zCoord += deltaZ * scaleX;
        } else if (scaleY < scaleZ) {
            crossedSide = targetY > currentY ? 0 : 1;
            current.xCoord += deltaX * scaleY;
            current.yCoord = nextBoundaryY;
            current.zCoord += deltaZ * scaleY;
        } else {
            crossedSide = targetZ > currentZ ? 2 : 3;
            current.xCoord += deltaX * scaleZ;
            current.yCoord += deltaY * scaleZ;
            current.zCoord = nextBoundaryZ;
        }

        currentX = MathHelper::floor_double(current.xCoord);
        if (crossedSide == 5) {
            --currentX;
        }
        currentY = MathHelper::floor_double(current.yCoord);
        if (crossedSide == 1) {
            --currentY;
        }
        currentZ = MathHelper::floor_double(current.zCoord);
        if (crossedSide == 3) {
            --currentZ;
        }

        const int blockId = getBlockId(currentX, currentY, currentZ);
        if (blockId <= 0) {
            continue;
        }

        Block* block = Block::blocksList[blockId];
        if (!block) {
            continue;
        }

        const int metadata = getBlockMetadata(currentX, currentY, currentZ);
        const Material* material = getBlockMaterial(currentX, currentY, currentZ);
        const bool hitLiquid = includeLiquids && material && material->getIsLiquid();
        if (!hitLiquid && !block->canCollideCheck(metadata, includeLiquids)) {
            continue;
        }

        AxisAlignedBB hitBox(static_cast<double>(currentX), static_cast<double>(currentY), static_cast<double>(currentZ),
                             static_cast<double>(currentX + 1), static_cast<double>(currentY + 1), static_cast<double>(currentZ + 1));
        if (!hitLiquid) {
            if (auto collisionBox = block->getCollisionBoundingBoxFromPool(this, currentX, currentY, currentZ)) {
                hitBox = *collisionBox;
            }
        }

        auto hit = hitBox.clip(current, target);
        if (hit) {
            hit->blockX = currentX;
            hit->blockY = currentY;
            hit->blockZ = currentZ;
            return hit;
        }
    }

    return std::nullopt;
}

void World::findSafeSpawnPoint() {
    Logger::info("Searching for safe spawn point...");
    std::uniform_int_distribution<int> dist(-1, 1);
    for (int sx = 0, sz = 0; ; sx += dist(rand), sz += dist(rand)) {
        Chunk* chunk = getChunk(sx, sz, true);
        bool found = false;
        for (int x = 0; x < 16 && !found; ++x) {
            for (int z = 0; z < 16 && !found; ++z) {
                int y = chunk->getHeightValue(x, z);
                uint8_t ground = chunk->getBlockID(x, y - 1, z);
                if (ground == 2 || ground == 12) {
                    spawnX = sx * 16 + x;
                    spawnZ = sz * 16 + z;
                    spawnY = y;
                    found = true;
                }
            }
        }
        if (found) break;
    }
    Logger::info("Spawn found: X={} Y={} Z={}", spawnX, spawnY, spawnZ);
}

void World::saveWorker(std::stop_token st) {
    while (true) {
        std::unique_lock lock(saveMutex_);
        saveCondition_.wait_for(lock, std::chrono::milliseconds(200), [&] {
            return !saveQueue_.empty() || stopSaving_.load();
        });

        while (!saveQueue_.empty()) {
            SaveTask task = std::move(saveQueue_.front());
            saveQueue_.pop();
            ++activeSaveTasks_;
            lock.unlock();

            if (db_) {
                leveldb::Slice keySlice(reinterpret_cast<const char*>(&task.key), sizeof(uint64_t));
                leveldb::Slice valSlice(reinterpret_cast<const char*>(task.data.data()), task.data.size());
                auto status = db_->Put(leveldb::WriteOptions(), keySlice, valSlice);
                if (!status.ok())
                    Logger::severe("LevelDB save failed: {}", status.ToString());
            }

            lock.lock();
            if (activeSaveTasks_ > 0) {
                --activeSaveTasks_;
            }
            if (saveQueue_.empty() && activeSaveTasks_ == 0) {
                saveDrainedCondition_.notify_all();
            }
        }

        // Exit only when queue is empty AND stop was requested
        if (stopSaving_.load() && saveQueue_.empty()) break;
    }
}

void World::waitForPendingSaves() {
    std::unique_lock lock(saveMutex_);
    saveDrainedCondition_.wait(lock, [this] {
        return saveQueue_.empty() && activeSaveTasks_ == 0;
    });
}

std::vector<uint8_t> World::compressChunkData(Chunk* chunk) {
    NBTCompound level;
    // Collect EntityItems belonging to this chunk before saving
    level.setInt("xPos", chunk->xPosition);
    level.setInt("zPos", chunk->zPosition);
    level.setByte("TerrainPopulated", chunk->isTerrainPopulated ? 1 : 0);
    level.setByteArray("Blocks", chunk->blocks);
    level.setByteArray("Data", chunk->data.data);
    level.setByteArray("SkyLight", chunk->skylight.data);
    level.setByteArray("BlockLight", chunk->blocklight.data);
    level.setByteArray("HeightMap", chunk->heightMap);

    // Save EntityItems that belong to this chunk
    {
        auto itemList = std::make_shared<NBTList>();
        itemList->tagType = NBTTagType::TAG_Compound;

        // Items already serialized on unload
        for (const auto& ed : chunk->pendingItems) {
            auto tag = std::make_shared<NBTCompound>();
            tag->setInt("id",    ed.itemID);
            tag->setInt("count", ed.count);
            tag->setInt("meta",  ed.metadata);
            tag->setInt("age",   ed.age);
            tag->setInt("delay", ed.pickupDelay);
            tag->setDouble("x",  ed.posX);
            tag->setDouble("y",  ed.posY);
            tag->setDouble("z",  ed.posZ);
            itemList->tags.push_back(tag);
        }

        // Live entities currently in world that belong to this chunk
        for (const auto& e : entities_) {
            auto* item = dynamic_cast<EntityItem*>(e.get());
            if (!item || item->isDead) continue;
            int cx = static_cast<int>(std::floor(item->posX)) >> 4;
            int cz = static_cast<int>(std::floor(item->posZ)) >> 4;
            if (cx != chunk->xPosition || cz != chunk->zPosition) continue;
            auto tag = std::make_shared<NBTCompound>();
            tag->setInt("id",    item->itemID);
            tag->setInt("count", item->count);
            tag->setInt("meta",  item->metadata);
            tag->setInt("age",   item->age);
            tag->setInt("delay", item->pickupDelay);
            tag->setDouble("x",  item->posX);
            tag->setDouble("y",  item->posY);
            tag->setDouble("z",  item->posZ);
            itemList->tags.push_back(tag);
        }

        if (!itemList->tags.empty())
            level.tags["Items"] = itemList;
    }

    {
        auto animalList = std::make_shared<NBTList>();
        animalList->tagType = NBTTagType::TAG_Compound;
        std::unordered_set<std::string> seenAnimals;

        auto makeEntityKey = [](const std::string& id, double x, double y, double z) {
            const int32_t fx = static_cast<int32_t>(std::floor(x * 32.0));
            const int32_t fy = static_cast<int32_t>(std::floor(y * 32.0));
            const int32_t fz = static_cast<int32_t>(std::floor(z * 32.0));
            return id + "#" + std::to_string(fx) + ":" + std::to_string(fy) + ":" + std::to_string(fz);
        };

        for (const auto& animalData : chunk->pendingAnimals) {
            ByteBuffer buf(animalData.nbtData);
            auto root = NBTCompound::readRoot(buf);
            if (root) {
                const std::string id = root->getString("id");
                const std::string key = makeEntityKey(id, root->getDouble("PosX"), root->getDouble("PosY"), root->getDouble("PosZ"));
                if (seenAnimals.insert(key).second) {
                    animalList->tags.push_back(std::make_shared<NBTCompound>(*root));
                }
            }
        }

        for (const auto& e : entities_) {
            auto* animal = dynamic_cast<EntityAnimals*>(e.get());
            if (!animal || animal->isDead) continue;
            const int cx = static_cast<int>(std::floor(animal->posX)) >> 4;
            const int cz = static_cast<int>(std::floor(animal->posZ)) >> 4;
            if (cx != chunk->xPosition || cz != chunk->zPosition) continue;

            const std::string key = makeEntityKey(animal->getEntityStringId(), animal->posX, animal->posY, animal->posZ);
            if (!seenAnimals.insert(key).second) continue;
            auto tag = std::make_shared<NBTCompound>();
            animal->writeToNBT(*tag);
            animalList->tags.push_back(tag);
        }

        if (!animalList->tags.empty()) {
            level.tags["Animals"] = animalList;
        }
    }

    {
        auto monsterList = std::make_shared<NBTList>();
        monsterList->tagType = NBTTagType::TAG_Compound;
        std::unordered_set<std::string> seenMonsters;

        auto makeEntityKey = [](const std::string& id, double x, double y, double z) {
            const int32_t fx = static_cast<int32_t>(std::floor(x * 32.0));
            const int32_t fy = static_cast<int32_t>(std::floor(y * 32.0));
            const int32_t fz = static_cast<int32_t>(std::floor(z * 32.0));
            return id + "#" + std::to_string(fx) + ":" + std::to_string(fy) + ":" + std::to_string(fz);
        };

        for (const auto& mobData : chunk->pendingMonsters) {
            ByteBuffer buf(mobData.nbtData);
            auto root = NBTCompound::readRoot(buf);
            if (root) {
                const std::string id = root->getString("id");
                const std::string key = makeEntityKey(id, root->getDouble("PosX"), root->getDouble("PosY"), root->getDouble("PosZ"));
                if (seenMonsters.insert(key).second) {
                    monsterList->tags.push_back(std::make_shared<NBTCompound>(*root));
                }
            }
        }

        for (const auto& e : entities_) {
            auto* mob = dynamic_cast<EntityMob*>(e.get());
            if (!mob || mob->isDead) continue;
            const int cx = static_cast<int>(std::floor(mob->posX)) >> 4;
            const int cz = static_cast<int>(std::floor(mob->posZ)) >> 4;
            if (cx != chunk->xPosition || cz != chunk->zPosition) continue;

            const std::string key = makeEntityKey(mob->getEntityStringId(), mob->posX, mob->posY, mob->posZ);
            if (!seenMonsters.insert(key).second) continue;
            auto tag = std::make_shared<NBTCompound>();
            mob->writeToNBT(*tag);
            monsterList->tags.push_back(tag);
        }

        if (!monsterList->tags.empty()) {
            level.tags["Monsters"] = monsterList;
        }
    }

    // Save TileEntities
    std::vector<std::shared_ptr<NBTTag>> tileEntityList;
    for (const auto& [key, te] : chunk->getTileEntities()) {
        if (te) {
            auto teCompound = std::make_shared<NBTCompound>();
            te->writeToNBT(*teCompound);
            tileEntityList.push_back(teCompound);
        }
    }
    
    if (!tileEntityList.empty()) {
        auto listTag = std::make_shared<NBTList>();
        listTag->tags = tileEntityList;
        listTag->tagType = NBTTagType::TAG_Compound;
        level.tags["TileEntities"] = listTag;
    }

    NBTCompound root;
    root.setCompound("Level", std::make_shared<NBTCompound>(level));

    ByteBuffer buf;
    root.writeRoot(buf, "");

    // Compress with zstd (faster decompression than gzip, same ratio)
    size_t bound = ZSTD_compressBound(buf.data.size());
    std::vector<uint8_t> out(bound);
    size_t compressed = ZSTD_compress(out.data(), bound,
                                      buf.data.data(), buf.data.size(),
                                      1); // level 1 = fastest
    if (ZSTD_isError(compressed)) {
        Logger::severe("zstd compress failed: {}", ZSTD_getErrorName(compressed));
        return {};
    }
    out.resize(compressed);
    return out;
}

void World::decompressChunkData(Chunk* chunk, const std::vector<uint8_t>& data,
                                std::vector<std::unique_ptr<TileEntity>>* detachedTileEntities) {
    if (data.size() < 4) return;

    std::vector<uint8_t> outBuf;

    // Detect format by magic: zstd frame starts with 0xFD2FB528 (little-endian)
    uint32_t magic;
    std::memcpy(&magic, data.data(), 4);
    if (magic == ZSTD_MAGIC) {
        // zstd
        unsigned long long decompSize = ZSTD_getFrameContentSize(data.data(), data.size());
        if (decompSize == ZSTD_CONTENTSIZE_ERROR || decompSize == ZSTD_CONTENTSIZE_UNKNOWN) {
            decompSize = 256 * 1024; // fallback 256KB
        }
        outBuf.resize(static_cast<size_t>(decompSize));
        size_t result = ZSTD_decompress(outBuf.data(), outBuf.size(),
                                        data.data(), data.size());
        if (ZSTD_isError(result)) {
            Logger::severe("zstd decompress failed: {}", ZSTD_getErrorName(result));
            return;
        }
        outBuf.resize(result);
    } else {
        // Legacy gzip
        z_stream strm{};
        if (inflateInit2(&strm, 15 + 16) != Z_OK) return;
        strm.next_in  = const_cast<uint8_t*>(data.data());
        strm.avail_in = static_cast<uInt>(data.size());
        outBuf.resize(128 * 1024);
        strm.next_out  = outBuf.data();
        strm.avail_out = static_cast<uInt>(outBuf.size());
        int ret = inflate(&strm, Z_FINISH);
        if (ret != Z_STREAM_END && ret != Z_OK) { inflateEnd(&strm); return; }
        outBuf.resize(strm.total_out);
        inflateEnd(&strm);
    }

    ByteBuffer buf(std::move(outBuf));
    auto root = NBTCompound::readRoot(buf);
    if (!root) return;

    auto level = root->getCompound("Level");
    if (!level) return;

    chunk->isTerrainPopulated = level->getByte("TerrainPopulated") != 0;
    chunk->blocks       = level->getByteArray("Blocks");
    chunk->data.data    = level->getByteArray("Data");
    chunk->skylight.data   = level->getByteArray("SkyLight");
    chunk->blocklight.data = level->getByteArray("BlockLight");
    chunk->heightMap    = level->getByteArray("HeightMap");
    chunk->pendingItems.clear();
    chunk->pendingAnimals.clear();
    chunk->pendingMonsters.clear();

    // Load TileEntities
    auto tileEntitiesTag = level->tags.find("TileEntities");
    if (tileEntitiesTag != level->tags.end()) {
        auto listTag = std::dynamic_pointer_cast<NBTList>(tileEntitiesTag->second);
        if (listTag) {
            for (const auto& tag : listTag->tags) {
                auto teCompound = std::dynamic_pointer_cast<NBTCompound>(tag);
                if (teCompound) {
                    auto te = TileEntity::createFromNBT(*teCompound);
                    if (te) {
                        if (detachedTileEntities) {
                            chunk->addTileEntity(te.get());
                            detachedTileEntities->push_back(std::move(te));
                        } else {
                            te->worldObj = this;
                            int x = te->xCoord;
                            int y = te->yCoord;
                            int z = te->zCoord;
                            std::lock_guard lock(tileEntitiesMutex_);
                            auto key = getTileEntityKey(x, y, z);
                            if (!tileEntities_.contains(key)) {
                                chunk->addTileEntity(te.get());
                                tileEntities_[key] = std::move(te);
                            }
                        }
                    }
                }
            }
        }
    }

    // Load EntityItems
    auto itemsTag = level->tags.find("Items");
    if (itemsTag != level->tags.end()) {
        auto listTag = std::dynamic_pointer_cast<NBTList>(itemsTag->second);
        if (listTag) {
            for (const auto& tag : listTag->tags) {
                auto t = std::dynamic_pointer_cast<NBTCompound>(tag);
                if (!t) continue;
                ChunkEntityData ed;
                ed.itemID      = t->getInt("id");
                ed.count       = t->getInt("count");
                ed.metadata    = t->getInt("meta");
                ed.age         = t->getInt("age");
                ed.pickupDelay = t->getInt("delay");
                ed.posX        = t->getDouble("x");
                ed.posY        = t->getDouble("y");
                ed.posZ        = t->getDouble("z");
                if (ed.age < 6000)
                    chunk->pendingItems.push_back(ed);
            }
        }
    }

    auto animalsTag = level->tags.find("Animals");
    if (animalsTag != level->tags.end()) {
        auto listTag = std::dynamic_pointer_cast<NBTList>(animalsTag->second);
        if (listTag) {
            for (const auto& tag : listTag->tags) {
                auto animalTag = std::dynamic_pointer_cast<NBTCompound>(tag);
                if (!animalTag) continue;
                ByteBuffer buf;
                animalTag->writeRoot(buf, animalTag->getString("id"));
                chunk->pendingAnimals.push_back({
                    .entityId = animalTag->getString("id"),
                    .nbtData = std::move(buf.data),
                    .posX = animalTag->getDouble("PosX"),
                    .posY = animalTag->getDouble("PosY"),
                    .posZ = animalTag->getDouble("PosZ"),
                });
            }
        }
    }

    auto monstersTag = level->tags.find("Monsters");
    if (monstersTag != level->tags.end()) {
        auto listTag = std::dynamic_pointer_cast<NBTList>(monstersTag->second);
        if (listTag) {
            for (const auto& tag : listTag->tags) {
                auto mobTag = std::dynamic_pointer_cast<NBTCompound>(tag);
                if (!mobTag) continue;
                ByteBuffer buf;
                mobTag->writeRoot(buf, mobTag->getString("id"));
                chunk->pendingMonsters.push_back({
                    .entityId = mobTag->getString("id"),
                    .nbtData = std::move(buf.data),
                    .posX = mobTag->getDouble("PosX"),
                    .posY = mobTag->getDouble("PosY"),
                    .posZ = mobTag->getDouble("PosZ"),
                });
            }
        }
    }

    if (chunk->heightMap.empty() || chunk->heightMap.size() < 256) {
        chunk->heightMap.assign(256, 0);
        chunk->generateHeightMap();
        chunk->generateSkylightMap();
    }
}

void World::spawnEntityInWorld(std::unique_ptr<Entity> entity) {
    if (!entity) return;
    entity->worldObj = this;
    if (Chunk* chunk = getChunkFromBlockCoords(static_cast<int>(std::floor(entity->posX)),
                                               static_cast<int>(std::floor(entity->posZ)), false)) {
        chunk->isModified = true;
    }
    Entity* ptr = entity.get();
    entities_.push_back(std::move(entity));
    // EntityTracker handles sending spawn packets to players
    if (mcServer && mcServer->entityTracker) {
        mcServer->entityTracker->addEntity(ptr);
    }
}

void World::removeEntity(Entity* entity) {
    if (!entity) return;
    if (Chunk* chunk = getChunkFromBlockCoords(static_cast<int>(std::floor(entity->posX)),
                                               static_cast<int>(std::floor(entity->posZ)), false)) {
        chunk->isModified = true;
    }
    entity->isDead = true;
}

void World::sendEntityStatus(Entity* entity, int8_t status) {
    if (!entity || !mcServer || !mcServer->configManager) {
        return;
    }

    mcServer->configManager->broadcastPacket(
        std::make_unique<Packet38EntityStatus>(entity->entityId, status));
}


TileEntity* World::getTileEntity(int x, int y, int z) {
    std::lock_guard lock(tileEntitiesMutex_);
    auto it = tileEntities_.find(getTileEntityKey(x, y, z));
    return it != tileEntities_.end() ? it->second.get() : nullptr;
}

void World::setTileEntity(int x, int y, int z, std::unique_ptr<TileEntity> tileEntity) {
    if (!tileEntity) return;
    
    tileEntity->worldObj = this;
    tileEntity->xCoord = x;
    tileEntity->yCoord = y;
    tileEntity->zCoord = z;
    
    // Add to chunk for tracking
    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (chunk) {
        chunk->addTileEntity(tileEntity.get());
    }
    
    std::lock_guard lock(tileEntitiesMutex_);
    tileEntities_[getTileEntityKey(x, y, z)] = std::move(tileEntity);
}

void World::removeTileEntity(int x, int y, int z) {
    // Remove from chunk
    Chunk* chunk = getChunkFromBlockCoords(x, z);
    if (chunk) {
        chunk->removeTileEntity(x & 15, y, z & 15);
    }
    
    std::lock_guard lock(tileEntitiesMutex_);
    tileEntities_.erase(getTileEntityKey(x, y, z));
}

void World::markTileEntityChanged(int x, int y, int z, TileEntity* te) {
    Chunk* chunk = getChunkFromBlockCoords(x, z, false);
    if (chunk) {
        chunk->isModified = true;
        // Immediately queue chunk save so TileEntity data (sign text, chest contents)
        // is persisted even if the server crashes or the chunk is unloaded before autosave
        uint64_t key = getChunkKey(chunk->xPosition, chunk->zPosition);
        auto data = compressChunkData(chunk);
        if (!data.empty()) {
            std::lock_guard lock(saveMutex_);
            saveQueue_.push({key, std::move(data)});
        }
        saveCondition_.notify_one();
    }
    if (onTileEntityChanged) {
        onTileEntityChanged(x, y, z, te);
    }
}

void World::saveChunkImmediate(Chunk* chunk) {
    if (!chunk || !db_) return;
    
    uint64_t key = getChunkKey(chunk->xPosition, chunk->zPosition);
    auto data = compressChunkData(chunk);
    
    if (!data.empty()) {
        leveldb::Slice keySlice(reinterpret_cast<const char*>(&key), sizeof(uint64_t));
        leveldb::Slice valSlice(reinterpret_cast<const char*>(data.data()), data.size());
        auto status = db_->Put(leveldb::WriteOptions(), keySlice, valSlice);
        
        if (status.ok()) {
            chunk->isModified = false;
        } else {
            Logger::severe("Failed to immediately save chunk: {}", status.ToString());
        }
    }
}
