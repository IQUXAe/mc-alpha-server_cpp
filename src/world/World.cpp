#include "World.h"
#include "../block/Block.h"
#include "../entity/Entity.h"
#include "../core/AxisAlignedBB.h"
#include "../MinecraftServer.h"
#include "core/NBT.h"
#include <zlib.h>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <ctime>

World::World(MinecraftServer* server, const std::string& savePath)
    : mcServer(server), worldPath_(savePath) {
    randomSeed = time(nullptr); // Basic seed
    worldTime = 0;

    // Create world directory
    std::filesystem::create_directories(worldPath_);

    // Initialize LevelDB
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, worldPath_ + "/db", &db_);
    if (!status.ok()) {
        std::cerr << "[ERROR] Failed to open LevelDB: " << status.ToString() << std::endl;
        db_ = nullptr;
    } else {
        std::cout << "[INFO] LevelDB world storage initialized at " << worldPath_ << "/db" << std::endl;
    }

    // Initialize WorldChunkManager (biome system)
    worldChunkManager = std::make_unique<WorldChunkManager>(randomSeed);

    // Initialize exactly same as alpha
    chunkProvider = std::make_unique<ChunkProviderGenerate>(this, randomSeed);

    // Initialize background saving
    saveThread_ = std::thread(&World::saveWorker, this);

    std::cout << "[INFO] World initialized with seed " << randomSeed << std::endl;

    findSafeSpawnPoint();
}

World::~World() {
    stopSaving_ = true;
    saveCondition_.notify_all();
    if (saveThread_.joinable()) {
        saveThread_.join();
    }
    if (db_) {
        delete db_;
        db_ = nullptr;
    }
}

void World::tick() {
    worldTime++;

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
                // Queue for saving before unloading
                std::vector<uint8_t> data = compressChunkData(it->second.get());
                {
                    std::lock_guard<std::mutex> lock(saveMutex_);
                    saveQueue_.push({key, data});
                }
                saveCondition_.notify_one();

                chunks_.erase(it);
            }
        }
    }
}

Chunk* World::getChunk(int chunkX, int chunkZ, bool generate) {
    auto key = getChunkKey(chunkX, chunkZ);
    auto it = chunks_.find(key);
    if (it != chunks_.end()) return it->second.get();

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
            chunks_[key] = std::move(chunk);
            return ptr;
        }
    }

    if (!generate) return nullptr;

    // Use ChunkProviderGenerate instead of inline procedural generator
    Chunk* ptr = chunkProvider->provideChunk(chunkX, chunkZ);
    chunks_[key] = std::unique_ptr<Chunk>(ptr);

    // Instead of forcing THIS chunk to populate immediately,
    // we evaluate if any of the surrounding chunks are now ready to populate.
    auto checkPopulate = [&](int px, int pz) {
        auto itP = chunks_.find(getChunkKey(px, pz));
        if (itP == chunks_.end()) return;
        Chunk* c = itP->second.get();
        if (c && !c->isTerrainPopulated) {
            if (chunkExists(px + 1, pz) && chunkExists(px, pz + 1) && chunkExists(px + 1, pz + 1)) {
                c->isTerrainPopulated = true;
                chunkProvider->populate(px, pz);
                
                // Recalculate lighting for the entire 2x2 area AFTER trees/ores are placed
                c->generateSkylightMap();
                if (Chunk* c1 = getChunk(px + 1, pz, false)) c1->generateSkylightMap();
                if (Chunk* c2 = getChunk(px, pz + 1, false)) c2->generateSkylightMap();
                if (Chunk* c3 = getChunk(px + 1, pz + 1, false)) c3->generateSkylightMap();
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

    markBlockNeedsUpdate(x, y, z);
    notifyBlocksOfNeighborChange(x, y, z, blockId);
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
    
    // Key format same as in NetServerHandler
    int64_t key = ((int64_t)(static_cast<uint32_t>(chunkX))) | (((int64_t)(static_cast<uint32_t>(chunkZ))) << 32);

    Packet53BlockChange pkt(x, static_cast<int8_t>(y), z, 
                            static_cast<int8_t>(getBlockId(x, y, z)), 
                            static_cast<int8_t>(getBlockMetadata(x, y, z)));

    // Send only to players who have this chunk loaded
    for (auto* player : mcServer->configManager->playerEntities) {
        if (player->netHandler) {
            if (player->netHandler->hasChunkLoaded(key)) {
                player->netHandler->sendPacket(std::make_unique<Packet53BlockChange>(pkt));
            }
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
    bool found = false;
    int searchChunkX = 0;
    int searchChunkZ = 0;

    std::cout << "[INFO] Searching for safe spawn point..." << std::endl;

    while (!found) {
        // Load/generate chunk to check
        Chunk* chunk = getChunk(searchChunkX, searchChunkZ, true);
        
        for (int x = 0; x < 16 && !found; ++x) {
            for (int z = 0; z < 16 && !found; ++z) {
                // Get highest point in these coordinates
                int y = chunk->getHeightValue(x, z); 
                
                // Check block directly UNDER feet (y - 1)
                uint8_t groundBlock = chunk->getBlockID(x, y - 1, z);
                
                // Search for grass (2) or sand (12)
                if (groundBlock == 2 || groundBlock == 12) {
                    spawnX = (searchChunkX * 16) + x;
                    spawnZ = (searchChunkZ * 16) + z;
                    spawnY = y; // Set player coordinates
                    found = true;
                }
            }
        }

        // If chunk is ocean or rock, spiral out randomly
        if (!found) {
            searchChunkX += (std::rand() % 3) - 1;
            searchChunkZ += (std::rand() % 3) - 1;
        }
    }
    std::cout << "[INFO] Spawn found: X=" << spawnX << " Y=" << spawnY << " Z=" << spawnZ << std::endl;
}

void World::saveWorker() {
    while (!stopSaving_ || !saveQueue_.empty()) {
        std::unique_lock<std::mutex> lock(saveMutex_);
        saveCondition_.wait_for(lock, std::chrono::seconds(1), [this] {
            return !saveQueue_.empty() || stopSaving_;
        });

        while (!saveQueue_.empty()) {
            SaveTask task = std::move(saveQueue_.front());
            saveQueue_.pop();
            lock.unlock();

            // Perform LevelDB I/O outside of main lock
            if (db_) {
                int32_t cx = static_cast<int>(static_cast<int32_t>(task.key >> 32));
                int32_t cz = static_cast<int>(static_cast<int32_t>(task.key & 0xFFFFFFFF));

                leveldb::Slice keySlice(reinterpret_cast<const char*>(&task.key), sizeof(uint64_t));
                leveldb::Slice valSlice(reinterpret_cast<const char*>(task.data.data()), task.data.size());

                leveldb::Status status = db_->Put(leveldb::WriteOptions(), keySlice, valSlice);
                if (!status.ok()) {
                    std::cerr << "[ERROR] LevelDB save failed for chunk " << cx << "," << cz 
                              << ": " << status.ToString() << std::endl;
                }
            }
            
            lock.lock();
        }
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
    root.writeRoot(buf, ""); // Outer tag name is usually empty in modern versions or specific in old

    // Compress with zlib (GZIP)
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // Use GZIP format (windowBits = 15 + 16)
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> outBuf(buf.data.size() + 1024);
    strm.next_in = buf.data.data();
    strm.avail_in = static_cast<uInt>(buf.data.size());
    strm.next_out = outBuf.data();
    strm.avail_out = static_cast<uInt>(outBuf.size());

    deflate(&strm, Z_FINISH);
    outBuf.resize(strm.total_out);
    deflateEnd(&strm);

    return outBuf;
}

void World::decompressChunkData(Chunk* chunk, const std::vector<uint8_t>& data) {
    // Decompress GZIP
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;

    if (inflateInit2(&strm, 15 + 16) != Z_OK) return;

    strm.next_in = const_cast<uint8_t*>(data.data());
    strm.avail_in = static_cast<uInt>(data.size());

    std::vector<uint8_t> outBuf(128 * 1024); // Start with 128KB
    strm.next_out = outBuf.data();
    strm.avail_out = static_cast<uInt>(outBuf.size());

    int ret = inflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
        inflateEnd(&strm);
        return;
    }
    outBuf.resize(strm.total_out);
    inflateEnd(&strm);

    ByteBuffer buf(outBuf);
    auto root = NBTCompound::readRoot(buf);
    if (!root) return;

    auto level = root->getCompound("Level");
    if (!level) return;

    chunk->isTerrainPopulated = level->getByte("TerrainPopulated") != 0;
    chunk->blocks = level->getByteArray("Blocks");
    chunk->data.data = level->getByteArray("Data");
    chunk->skylight.data = level->getByteArray("SkyLight");
    chunk->blocklight.data = level->getByteArray("BlockLight");
    chunk->heightMap = level->getByteArray("HeightMap");
}
