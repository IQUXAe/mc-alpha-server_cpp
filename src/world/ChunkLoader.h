#pragma once

#include "Chunk.h"
#include "World.h"
#include "TileEntity.h"
#include "../core/NBT.h"
#include <string>
#include <fstream>
#include <filesystem>
#include <zlib.h>
#include <iostream>

// ChunkLoader handles saving and loading chunks to/from disk
// Matches Java's ChunkLoader implementation for Alpha 1.2.6
class ChunkLoader {
public:
    ChunkLoader(const std::string& worldDir, bool createIfMissing) 
        : worldDirectory_(worldDir), createDirectories_(createIfMissing) {
    }

    // Save chunk to disk with TileEntities
    void saveChunk(World* world, Chunk* chunk) {
        if (!chunk) return;

        std::filesystem::path chunkFile = getChunkFile(chunk->xPosition, chunk->zPosition);
        
        // Create parent directories if needed
        if (createDirectories_) {
            std::filesystem::create_directories(chunkFile.parent_path());
        }

        try {
            // Create NBT structure matching Java format
            NBTCompound root;
            NBTCompound level;
            
            writeChunkToNBT(chunk, world, level);
            
            // Write to temporary file first
            std::filesystem::path tempFile = worldDirectory_ + "/tmp_chunk.dat";
            
            // Serialize NBT to buffer
            ByteBuffer buf;
            root.tags["Level"] = std::make_shared<NBTCompound>(level);
            root.writeRoot(buf, "");
            
            // Compress with GZip
            std::vector<uint8_t> compressed = compressGZip(buf.data);
            
            // Write to file
            std::ofstream out(tempFile, std::ios::binary);
            out.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            out.close();
            
            // Replace old file
            if (std::filesystem::exists(chunkFile)) {
                std::filesystem::remove(chunkFile);
            }
            std::filesystem::rename(tempFile, chunkFile);
            
            std::cout << "[ChunkLoader] Saved chunk (" << chunk->xPosition << ", " 
                      << chunk->zPosition << ") with " 
                      << chunk->getTileEntities().size() << " TileEntities" << std::endl;
                      
        } catch (const std::exception& e) {
            std::cerr << "[ChunkLoader] Error saving chunk: " << e.what() << std::endl;
        }
    }

    // Load chunk from disk with TileEntities
    Chunk* loadChunk(World* world, int chunkX, int chunkZ) {
        std::filesystem::path chunkFile = getChunkFile(chunkX, chunkZ);
        
        if (!std::filesystem::exists(chunkFile)) {
            return nullptr;
        }

        try {
            // Read file
            std::ifstream in(chunkFile, std::ios::binary);
            std::vector<uint8_t> compressed((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
            in.close();
            
            // Decompress GZip
            std::vector<uint8_t> decompressed = decompressGZip(compressed);
            
            // Parse NBT
            ByteBuffer buf;
            buf.data = decompressed;
            buf.readPos = 0;
            
            auto root = NBTCompound::readRoot(buf);
            if (!root) return nullptr;
            
            auto levelTag = root->tags.find("Level");
            if (levelTag == root->tags.end()) return nullptr;
            
            auto level = std::dynamic_pointer_cast<NBTCompound>(levelTag->second);
            if (!level) return nullptr;
            
            // Create chunk from NBT
            Chunk* chunk = readChunkFromNBT(world, *level);
            
            if (chunk) {
                std::cout << "[ChunkLoader] Loaded chunk (" << chunkX << ", " 
                          << chunkZ << ") with " 
                          << chunk->getTileEntities().size() << " TileEntities" << std::endl;
            }
            
            return chunk;
            
        } catch (const std::exception& e) {
            std::cerr << "[ChunkLoader] Error loading chunk: " << e.what() << std::endl;
            return nullptr;
        }
    }

private:
    std::string worldDirectory_;
    bool createDirectories_;

    // Get file path for chunk (matching Java format)
    std::filesystem::path getChunkFile(int chunkX, int chunkZ) {
        // Convert to base-36 like Java
        auto toBase36 = [](int num) -> std::string {
            const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
            if (num == 0) return "0";
            
            std::string result;
            bool negative = num < 0;
            if (negative) num = -num;
            
            while (num > 0) {
                result = digits[num % 36] + result;
                num /= 36;
            }
            
            return negative ? "-" + result : result;
        };
        
        std::string fileName = "c." + toBase36(chunkX) + "." + toBase36(chunkZ) + ".dat";
        std::string dir1 = toBase36(chunkX & 63);
        std::string dir2 = toBase36(chunkZ & 63);
        
        return std::filesystem::path(worldDirectory_) / dir1 / dir2 / fileName;
    }

    // Write chunk data to NBT (matching Java format exactly)
    void writeChunkToNBT(Chunk* chunk, World* world, NBTCompound& nbt) {
        nbt.setInt("xPos", chunk->xPosition);
        nbt.setInt("zPos", chunk->zPosition);
        nbt.setLong("LastUpdate", world->worldTime);
        
        // Convert blocks vector to array
        std::vector<uint8_t> blocksArray(chunk->blocks.begin(), chunk->blocks.end());
        nbt.setByteArray("Blocks", blocksArray);
        nbt.setByteArray("Data", chunk->data.data);
        nbt.setByteArray("SkyLight", chunk->skylight.data);
        nbt.setByteArray("BlockLight", chunk->blocklight.data);
        nbt.setByteArray("HeightMap", chunk->heightMap);
        nbt.setBoolean("TerrainPopulated", chunk->isTerrainPopulated);
        
        // Save TileEntities
        std::vector<std::shared_ptr<NBTTag>> tileEntitiesList;
        for (const auto& [key, te] : chunk->getTileEntities()) {
            auto teCompound = std::make_shared<NBTCompound>();
            te->writeToNBT(*teCompound);
            tileEntitiesList.push_back(teCompound);
        }
        
        if (!tileEntitiesList.empty()) {
            auto listTag = std::make_shared<NBTList>();
            listTag->tags = tileEntitiesList;
            listTag->tagType = NBTTagType::TAG_Compound;
            nbt.tags["TileEntities"] = listTag;
        }
    }

    // Read chunk data from NBT (matching Java format exactly)
    Chunk* readChunkFromNBT(World* world, const NBTCompound& nbt) {
        int xPos = nbt.getInt("xPos");
        int zPos = nbt.getInt("zPos");
        
        Chunk* chunk = new Chunk(world, xPos, zPos);
        
        // Load blocks and data
        chunk->blocks = nbt.getByteArray("Blocks");
        chunk->data.data = nbt.getByteArray("Data");
        chunk->skylight.data = nbt.getByteArray("SkyLight");
        chunk->blocklight.data = nbt.getByteArray("BlockLight");
        chunk->heightMap = nbt.getByteArray("HeightMap");
        chunk->isTerrainPopulated = nbt.tags.find("TerrainPopulated") != nbt.tags.end() 
            ? std::dynamic_pointer_cast<NBTByte>(nbt.tags.at("TerrainPopulated"))->value != 0
            : false;
        
        // Validate data arrays
        if (chunk->data.data.size() != CHUNK_VOLUME / 2) {
            chunk->data.data.resize(CHUNK_VOLUME / 2, 0);
        }
        if (chunk->skylight.data.size() != CHUNK_VOLUME / 2) {
            chunk->skylight.data.resize(CHUNK_VOLUME / 2, 0);
            chunk->generateSkylightMap();
        }
        if (chunk->blocklight.data.size() != CHUNK_VOLUME / 2) {
            chunk->blocklight.data.resize(CHUNK_VOLUME / 2, 0);
        }
        if (chunk->heightMap.size() != CHUNK_AREA) {
            chunk->heightMap.resize(CHUNK_AREA, 0);
            chunk->generateHeightMap();
        }
        
        // Load TileEntities
        auto tileEntitiesTag = nbt.tags.find("TileEntities");
        if (tileEntitiesTag != nbt.tags.end()) {
            auto listTag = std::dynamic_pointer_cast<NBTList>(tileEntitiesTag->second);
            if (listTag) {
                for (const auto& tag : listTag->tags) {
                    auto teCompound = std::dynamic_pointer_cast<NBTCompound>(tag);
                    if (teCompound) {
                        auto tileEntity = TileEntity::createFromNBT(*teCompound);
                        if (tileEntity) {
                            tileEntity->worldObj = world;
                            chunk->addTileEntity(tileEntity.release());
                        }
                    }
                }
            }
        }
        
        return chunk;
    }

    // Compress data with GZip
    std::vector<uint8_t> compressGZip(const std::vector<uint8_t>& data) {
        z_stream stream{};
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        
        // Use gzip format (windowBits = 15 + 16)
        if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 
                        15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("Failed to initialize deflate");
        }
        
        stream.avail_in = static_cast<uInt>(data.size());
        stream.next_in = const_cast<Bytef*>(data.data());
        
        std::vector<uint8_t> compressed;
        compressed.resize(deflateBound(&stream, data.size()));
        
        stream.avail_out = static_cast<uInt>(compressed.size());
        stream.next_out = compressed.data();
        
        deflate(&stream, Z_FINISH);
        compressed.resize(stream.total_out);
        
        deflateEnd(&stream);
        return compressed;
    }

    // Decompress GZip data
    std::vector<uint8_t> decompressGZip(const std::vector<uint8_t>& compressed) {
        z_stream stream{};
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        
        // Use gzip format (windowBits = 15 + 16)
        if (inflateInit2(&stream, 15 + 16) != Z_OK) {
            throw std::runtime_error("Failed to initialize inflate");
        }
        
        stream.avail_in = static_cast<uInt>(compressed.size());
        stream.next_in = const_cast<Bytef*>(compressed.data());
        
        std::vector<uint8_t> decompressed;
        decompressed.resize(1024 * 1024); // 1MB initial buffer
        
        stream.avail_out = static_cast<uInt>(decompressed.size());
        stream.next_out = decompressed.data();
        
        int ret = inflate(&stream, Z_FINISH);
        if (ret != Z_STREAM_END) {
            inflateEnd(&stream);
            throw std::runtime_error("Failed to decompress data");
        }
        
        decompressed.resize(stream.total_out);
        inflateEnd(&stream);
        return decompressed;
    }
};
