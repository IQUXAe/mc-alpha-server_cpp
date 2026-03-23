#include "World.h"
#include "../block/Block.h"
#include "../entity/Entity.h"
#include "../entity/EntityItem.h"
#include "../entity/EntityPlayerMP.h"
#include "../entity/EntityTracker.h"
#include "../network/packets/AllPackets.h"
#include "../core/AxisAlignedBB.h"
#include "../MinecraftServer.h"
#include "core/NBT.h"
#include "core/Logger.h"
#include <zlib.h>
#include <zstd.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <ctime>

// Magic prefix to distinguish zstd chunks from legacy gzip chunks
static constexpr uint32_t ZSTD_MAGIC = 0xFD2FB528; // native zstd frame magic

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
    rand.seed(randomSeed);

    saveThread_ = std::jthread([this](std::stop_token st) { saveWorker(st); });
    
    // Load spawn and entities
    if (!hasLevelDat) {
        findSafeSpawnPoint();
        saveLevelDat();
    }
    loadEntities();
}

void World::saveWorld() {
    saveLevelDat();
    saveEntities();

    int savedCount = 0;
    for (const auto& [key, chunk] : chunks_) {
        if (mcServer && mcServer->saveModifiedChunksOnly && !chunk->isModified) continue;
        auto data = compressChunkData(chunk.get());
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

    delete db_;
    db_ = nullptr;
}

void World::tick() {
    worldTime++;

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
    for (auto itE = entities_.begin(); itE != entities_.end(); ) {
        (*itE)->tick();
        
        // Item pickup logic
        auto* item = dynamic_cast<EntityItem*>(itE->get());
        if (item && !item->isDead && item->pickupDelay <= 0) {
            if (mcServer && mcServer->configManager) {
                for (auto* player : mcServer->configManager->playerEntities) {
                    double dx = player->posX - item->posX;
                    double dy = player->posY - item->posY;
                    double dz = player->posZ - item->posZ;
                    
                    // Java: boundingBox.expands(1.0, 0.0, 1.0) — expand player bbox by 1 on X/Z, 0 on Y
                    double ex = (player->width / 2.0) + 1.0;
                    double ey = player->height;
                    double ez = (player->width / 2.0) + 1.0;
                    bool inRange = std::abs(dx) < ex && std::abs(dy) >= 0.0 && std::abs(dy) < ey && std::abs(dz) < ez;
                    if (inRange) {
                        ItemStack stack(item->itemID, item->count, item->metadata);
                        int initialCount = item->count;
                        player->inventory.addItemStackToInventory(&stack);
                        int addedCount = initialCount - stack.stackSize;

                        if (addedCount > 0) {
                            mcServer->configManager->broadcastPacket(std::make_unique<Packet22Collect>(item->entityId, player->entityId));

                            if (player->netHandler) {
                                // Packet17: shows pickup animation
                                player->netHandler->sendPacket(std::make_unique<Packet17AddToInventory>(
                                    static_cast<int16_t>(item->itemID),
                                    static_cast<int8_t>(addedCount),
                                    static_cast<int16_t>(item->metadata)));
                                // Packet5: syncs actual inventory state to client
                                player->netHandler->sendInventory();
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

        if ((*itE)->isDead) {
            if (mcServer && mcServer->entityTracker) {
                mcServer->entityTracker->removeEntity(itE->get());
            }
            itE = entities_.erase(itE);
        } else {
            ++itE;
        }
    }

    // Unload chunks far from all players periodically
    if (worldTime % 100 == 0) {
        int r = 0;
        if (mcServer) {
            r = mcServer->viewDistance + 2; // Generation radius mapping
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
                bool shouldSave = true;
                if (mcServer && mcServer->saveModifiedChunksOnly && !it->second->isModified) {
                    shouldSave = false;
                }
                
                if (shouldSave) {
                    // Queue for saving before unloading
                    std::vector<uint8_t> data = compressChunkData(it->second.get());
                    {
                        std::lock_guard<std::mutex> lock(saveMutex_);
                        saveQueue_.push({key, data});
                    }
                    saveCondition_.notify_one();
                }

                chunks_.erase(it);
            }
        }
    }

    if (mcServer && mcServer->autoSaveInterval > 0 && worldTime % mcServer->autoSaveInterval == 0) {
        int savedCount = 0;
        for (const auto& [key, chunk] : chunks_) {
            if (mcServer->saveModifiedChunksOnly && !chunk->isModified) continue;
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

Chunk* World::getChunk(int chunkX, int chunkZ, bool generate) {
    auto key = getChunkKey(chunkX, chunkZ);
    
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
            return ptr;
        }
    }

    if (!generate) return nullptr;

    // Use ChunkProviderGenerate instead of inline procedural generator
    Chunk* ptr = chunkProvider->provideChunk(chunkX, chunkZ);
    {
        std::lock_guard<std::mutex> lock(chunksMutex_);
        chunks_[key] = std::unique_ptr<Chunk>(ptr);
    }

    // Instead of forcing THIS chunk to populate immediately,
    // we evaluate if any of the surrounding chunks are now ready to populate.
    auto checkPopulate = [&](int px, int pz) {
        auto itP = chunks_.find(getChunkKey(px, pz));
        if (itP == chunks_.end()) return;
        Chunk* c = itP->second.get();
        if (c && !c->isTerrainPopulated) {
            if (chunkExists(px + 1, pz) && chunkExists(px, pz + 1) && chunkExists(px + 1, pz + 1)) {
                c->isTerrainPopulated = true;
                this->isPopulating = true;
                chunkProvider->populate(px, pz);
                
                // Recalculate lighting for the entire 2x2 area AFTER trees/ores are placed
                c->generateSkylightMap();
                if (Chunk* c1 = getChunk(px + 1, pz, false)) c1->generateSkylightMap();
                if (Chunk* c2 = getChunk(px, pz + 1, false)) c2->generateSkylightMap();
                if (Chunk* c3 = getChunk(px + 1, pz + 1, false)) c3->generateSkylightMap();
                this->isPopulating = false;
            }
        }
    };

    checkPopulate(chunkX, chunkZ);
    checkPopulate(chunkX - 1, chunkZ);
    checkPopulate(chunkX, chunkZ - 1);
    checkPopulate(chunkX - 1, chunkZ - 1);

    return ptr;
}

Chunk* World::getChunkFromBlockCoords(int x, int z, bool generate) {
    return getChunk(x >> 4, z >> 4, generate);
}

bool World::chunkExists(int chunkX, int chunkZ) const {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    return chunks_.find(getChunkKey(chunkX, chunkZ)) != chunks_.end();
}

uint8_t World::getBlockId(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 0;

    Chunk* chunk = getChunkFromBlockCoords(x, z);
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
    if (!setBlock(x, y, z, blockId)) return false;

    // Call onBlockAdded BEFORE notifying client so metadata is set correctly
    if (blockId > 0 && Block::blocksList[blockId])
        Block::blocksList[blockId]->onBlockAdded(this, x, y, z);

    // Send block update with final metadata (after onBlockAdded may have changed it)
    markBlockNeedsUpdate(x, y, z);
    notifyBlocksOfNeighborChange(x, y, z, blockId);
    return true;
}

bool World::setBlockWithNotifyNoClientUpdate(int x, int y, int z, uint8_t blockId) {
    if (!setBlock(x, y, z, blockId)) return false;
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

    markBlockNeedsUpdate(x, y, z);
    notifyBlocksOfNeighborChange(x, y, z, blockId);
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

void World::findSafeSpawnPoint() {
    Logger::info("Searching for safe spawn point...");
    for (int sx = 0, sz = 0; ; sx += (std::rand() % 3) - 1, sz += (std::rand() % 3) - 1) {
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
            lock.unlock();

            if (db_) {
                leveldb::Slice keySlice(reinterpret_cast<const char*>(&task.key), sizeof(uint64_t));
                leveldb::Slice valSlice(reinterpret_cast<const char*>(task.data.data()), task.data.size());
                auto status = db_->Put(leveldb::WriteOptions(), keySlice, valSlice);
                if (!status.ok())
                    Logger::severe("LevelDB save failed: {}", status.ToString());
            }

            lock.lock();
        }

        // Exit only when queue is empty AND stop was requested
        if (stopSaving_.load() && saveQueue_.empty()) break;
    }
}

std::vector<uint8_t> World::compressChunkData(Chunk* chunk) {
    NBTCompound level;
    level.setInt("xPos", chunk->xPosition);
    level.setInt("zPos", chunk->zPosition);
    level.setByte("TerrainPopulated", chunk->isTerrainPopulated ? 1 : 0);
    level.setByteArray("Blocks", chunk->blocks);
    level.setByteArray("Data", chunk->data.data);
    level.setByteArray("SkyLight", chunk->skylight.data);
    level.setByteArray("BlockLight", chunk->blocklight.data);
    level.setByteArray("HeightMap", chunk->heightMap);

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

void World::decompressChunkData(Chunk* chunk, const std::vector<uint8_t>& data) {
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

    if (chunk->heightMap.empty() || chunk->heightMap.size() < 256) {
        chunk->heightMap.assign(256, 0);
        chunk->generateHeightMap();
        chunk->generateSkylightMap();
    }
}

void World::spawnEntityInWorld(std::unique_ptr<Entity> entity) {
    if (!entity) return;
    entity->worldObj = this;
    Entity* ptr = entity.get();
    entities_.push_back(std::move(entity));
    // EntityTracker handles sending spawn packets to players
    if (mcServer && mcServer->entityTracker) {
        mcServer->entityTracker->addEntity(ptr);
    }
}

void World::removeEntity(Entity* entity) {
    if (!entity) return;
    entity->isDead = true;
}

void World::saveEntities() {
    std::ofstream file(worldPath_ + "/entities.dat", std::ios::binary);
    if (!file) return;

    uint32_t count = 0;
    for (const auto& e : entities_) {
        if (dynamic_cast<EntityItem*>(e.get()) && !e->isDead) count++;
    }

    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& e : entities_) {
        auto* item = dynamic_cast<EntityItem*>(e.get());
        if (item && !item->isDead) {
            file.write(reinterpret_cast<const char*>(&item->itemID), sizeof(item->itemID));
            file.write(reinterpret_cast<const char*>(&item->count), sizeof(item->count));
            file.write(reinterpret_cast<const char*>(&item->metadata), sizeof(item->metadata));
            file.write(reinterpret_cast<const char*>(&item->posX), sizeof(item->posX));
            file.write(reinterpret_cast<const char*>(&item->posY), sizeof(item->posY));
            file.write(reinterpret_cast<const char*>(&item->posZ), sizeof(item->posZ));
        }
    }
    file.flush();
    file.close();
}

void World::loadEntities() {
    std::ifstream file(worldPath_ + "/entities.dat", std::ios::binary);
    if (!file) return;

    uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    for (uint32_t i = 0; i < count; ++i) {
        int id, cnt, meta;
        double x, y, z;
        file.read(reinterpret_cast<char*>(&id), sizeof(id));
        file.read(reinterpret_cast<char*>(&cnt), sizeof(cnt));
        file.read(reinterpret_cast<char*>(&meta), sizeof(meta));
        file.read(reinterpret_cast<char*>(&x), sizeof(x));
        file.read(reinterpret_cast<char*>(&y), sizeof(y));
        file.read(reinterpret_cast<char*>(&z), sizeof(z));

        auto item = std::make_unique<EntityItem>(id, cnt, meta);
        item->setPosition(x, y, z);
        item->worldObj = this;
        entities_.push_back(std::move(item));
    }
    if (count > 0) Logger::info("Loaded {} entities.", count);
}
