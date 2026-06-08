/*
 * Server compatible with Minecraft Alpha 1.2.6 written on C++
 * Copyright (C) 2026  IQUXAe
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "Chunk.h"
#include "World.h"
#include "TileEntity.h"
#include "../core/NBT.h"
#include "../entity/EntityItem.h"
#include "../entity/EntityAnimals.h"
#include "../entity/EntityMobs.h"
#include "../entity/EntityBoat.h"
#include "../../rust/alpha_bridge/alpha_bridge.h"
#include <string>
#include <filesystem>
#include <iostream>
#include <vector>
#include <memory>
#include <unordered_set>

// ChunkLoader handles saving and loading chunks to/from disk via Rust Bridge FFI
class ChunkLoader {
public:
    ChunkLoader(const std::string& worldDir, bool createIfMissing) 
        : worldDirectory_(worldDir), createDirectories_(createIfMissing) {
    }

    // Save chunk to disk with TileEntities and Entities
    void saveChunk(World* world, Chunk* chunk) {
        if (!chunk) return;

        try {
            AlphaChunkData chunkData;
            chunkData.x_pos = chunk->xPosition;
            chunkData.z_pos = chunk->zPosition;
            chunkData.last_update = world->worldTime;

            // Prepare blocks
            std::vector<uint8_t> blocksArray(chunk->blocks.begin(), chunk->blocks.end());
            chunkData.blocks = blocksArray.data();
            chunkData.blocks_len = blocksArray.size();

            // Prepare metadata and lights
            chunkData.data = chunk->data.data.data();
            chunkData.data_len = chunk->data.data.size();

            chunkData.sky_light = chunk->skylight.data.data();
            chunkData.sky_light_len = chunk->skylight.data.size();

            chunkData.block_light = chunk->blocklight.data.data();
            chunkData.block_light_len = chunk->blocklight.data.size();

            chunkData.height_map = chunk->heightMap.data();
            chunkData.height_map_len = chunk->heightMap.size();

            chunkData.terrain_populated = chunk->isTerrainPopulated;

            // Prepare TileEntities
            std::vector<NbtCompound*> teRustPtrs;
            std::vector<std::shared_ptr<NBTCompound>> teCppCompounds;
            
            for (const auto& [key, te] : chunk->getTileEntities()) {
                auto teCompound = std::make_shared<NBTCompound>();
                te->writeToNBT(*teCompound);
                teCppCompounds.push_back(teCompound);
                
                NbtCompound* r_comp = cpp_to_rust_compound(*teCompound);
                teRustPtrs.push_back(r_comp);
            }

            chunkData.tile_entities = teRustPtrs.data();
            chunkData.tile_entities_count = teRustPtrs.size();

            // Helper lists for Rust FFI cleanups
            std::vector<NbtCompound*> itemRustPtrs;
            std::vector<std::shared_ptr<NBTCompound>> itemCppCompounds;
            std::vector<NbtCompound*> animalRustPtrs;
            std::vector<std::shared_ptr<NBTCompound>> animalCppCompounds;
            std::vector<NbtCompound*> monsterRustPtrs;
            std::vector<std::shared_ptr<NBTCompound>> monsterCppCompounds;
            std::vector<NbtCompound*> boatRustPtrs;
            std::vector<std::shared_ptr<NBTCompound>> boatCppCompounds;

            // 1. Items
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
                itemCppCompounds.push_back(tag);
                itemRustPtrs.push_back(cpp_to_rust_compound(*tag));
            }
            for (const auto& e : world->entities_) {
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
                itemCppCompounds.push_back(tag);
                itemRustPtrs.push_back(cpp_to_rust_compound(*tag));
            }
            chunkData.items = itemRustPtrs.data();
            chunkData.items_count = itemRustPtrs.size();

            // Helper to key entities by PosX/Y/Z for deduplication (same as World.cpp)
            auto makeEntityKey = [](const std::string& id, double x, double y, double z) {
                const int32_t fx = static_cast<int32_t>(std::floor(x * 32.0));
                const int32_t fy = static_cast<int32_t>(std::floor(y * 32.0));
                const int32_t fz = static_cast<int32_t>(std::floor(z * 32.0));
                return id + "#" + std::to_string(fx) + ":" + std::to_string(fy) + ":" + std::to_string(fz);
            };

            // 2. Animals
            std::unordered_set<std::string> seenAnimals;
            for (const auto& animalData : chunk->pendingAnimals) {
                ByteBuffer buf(animalData.nbtData);
                auto root = NBTCompound::readRoot(buf);
                if (root) {
                    const std::string id = root->getString("id");
                    const std::string key = makeEntityKey(id, root->getDouble("PosX"), root->getDouble("PosY"), root->getDouble("PosZ"));
                    if (seenAnimals.insert(key).second) {
                        animalCppCompounds.push_back(root);
                        animalRustPtrs.push_back(cpp_to_rust_compound(*root));
                    }
                }
            }
            for (const auto& e : world->entities_) {
                auto* animal = dynamic_cast<EntityAnimals*>(e.get());
                if (!animal || animal->isDead) continue;
                const int cx = static_cast<int>(std::floor(animal->posX)) >> 4;
                const int cz = static_cast<int>(std::floor(animal->posZ)) >> 4;
                if (cx != chunk->xPosition || cz != chunk->zPosition) continue;
                const std::string key = makeEntityKey(animal->getEntityStringId(), animal->posX, animal->posY, animal->posZ);
                if (!seenAnimals.insert(key).second) continue;
                auto tag = std::make_shared<NBTCompound>();
                animal->writeToNBT(*tag);
                animalCppCompounds.push_back(tag);
                animalRustPtrs.push_back(cpp_to_rust_compound(*tag));
            }
            chunkData.animals = animalRustPtrs.data();
            chunkData.animals_count = animalRustPtrs.size();

            // 3. Monsters
            std::unordered_set<std::string> seenMonsters;
            for (const auto& mobData : chunk->pendingMonsters) {
                ByteBuffer buf(mobData.nbtData);
                auto root = NBTCompound::readRoot(buf);
                if (root) {
                    const std::string id = root->getString("id");
                    const std::string key = makeEntityKey(id, root->getDouble("PosX"), root->getDouble("PosY"), root->getDouble("PosZ"));
                    if (seenMonsters.insert(key).second) {
                        monsterCppCompounds.push_back(root);
                        monsterRustPtrs.push_back(cpp_to_rust_compound(*root));
                    }
                }
            }
            for (const auto& e : world->entities_) {
                auto* mob = dynamic_cast<EntityMob*>(e.get());
                if (!mob || mob->isDead) continue;
                const int cx = static_cast<int>(std::floor(mob->posX)) >> 4;
                const int cz = static_cast<int>(std::floor(mob->posZ)) >> 4;
                if (cx != chunk->xPosition || cz != chunk->zPosition) continue;
                const std::string key = makeEntityKey(mob->getEntityStringId(), mob->posX, mob->posY, mob->posZ);
                if (!seenMonsters.insert(key).second) continue;
                auto tag = std::make_shared<NBTCompound>();
                mob->writeToNBT(*tag);
                monsterCppCompounds.push_back(tag);
                monsterRustPtrs.push_back(cpp_to_rust_compound(*tag));
            }
            chunkData.monsters = monsterRustPtrs.data();
            chunkData.monsters_count = monsterRustPtrs.size();

            // 4. Boats
            auto makeBoatKey = [](double x, double y, double z) {
                const int32_t fx = static_cast<int32_t>(std::floor(x * 32.0));
                const int32_t fy = static_cast<int32_t>(std::floor(y * 32.0));
                const int32_t fz = static_cast<int32_t>(std::floor(z * 32.0));
                return std::to_string(fx) + ":" + std::to_string(fy) + ":" + std::to_string(fz);
            };
            std::unordered_set<std::string> seenBoats;
            for (const auto& boatData : chunk->pendingBoats) {
                ByteBuffer buf(boatData.nbtData);
                auto root = NBTCompound::readRoot(buf);
                if (!root) continue;
                const std::string key = makeBoatKey(root->getDouble("PosX"), root->getDouble("PosY"), root->getDouble("PosZ"));
                if (!seenBoats.insert(key).second) continue;
                boatCppCompounds.push_back(root);
                boatRustPtrs.push_back(cpp_to_rust_compound(*root));
            }
            for (const auto& e : world->entities_) {
                auto* boat = dynamic_cast<EntityBoat*>(e.get());
                if (!boat || boat->isDead) continue;
                const int cx = static_cast<int>(std::floor(boat->posX)) >> 4;
                const int cz = static_cast<int>(std::floor(boat->posZ)) >> 4;
                if (cx != chunk->xPosition || cz != chunk->zPosition) continue;
                const std::string key = makeBoatKey(boat->posX, boat->posY, boat->posZ);
                if (!seenBoats.insert(key).second) continue;
                auto tag = std::make_shared<NBTCompound>();
                boat->writeToNBT(*tag);
                boatCppCompounds.push_back(tag);
                boatRustPtrs.push_back(cpp_to_rust_compound(*tag));
            }
            chunkData.boats = boatRustPtrs.data();
            chunkData.boats_count = boatRustPtrs.size();

            // Call FFI save
            bool success = alpha_chunk_loader_save(
                worldDirectory_.c_str(),
                createDirectories_,
                &chunkData
            );

            // Free temporary Rust compounds allocated for serialization
            auto freeRustPtrs = [](std::vector<NbtCompound*>& ptrs) {
                for (auto* ptr : ptrs) {
                    if (ptr) alpha_nbt_compound_free(ptr);
                }
            };
            freeRustPtrs(teRustPtrs);
            freeRustPtrs(itemRustPtrs);
            freeRustPtrs(animalRustPtrs);
            freeRustPtrs(monsterRustPtrs);
            freeRustPtrs(boatRustPtrs);

            if (success) {
                chunk->isModified = false;
                std::cout << "[ChunkLoader] Saved chunk (" << chunk->xPosition << ", " 
                          << chunk->zPosition << ") with " 
                          << chunk->getTileEntities().size() << " TileEntities" << std::endl;
            } else {
                std::cerr << "[ChunkLoader] Rust alpha_chunk_loader_save returned false" << std::endl;
            }

        } catch (const std::exception& e) {
            std::cerr << "[ChunkLoader] Error saving chunk: " << e.what() << std::endl;
        }
    }

    // Load chunk from disk with TileEntities and Entities
    Chunk* loadChunk(World* world, int chunkX, int chunkZ) {
        try {
            AlphaChunkData* data = alpha_chunk_loader_load(
                worldDirectory_.c_str(),
                chunkX,
                chunkZ
            );

            if (!data) {
                return nullptr;
            }

            Chunk* chunk = new Chunk(world, data->x_pos, data->z_pos);

            // Copy chunk data arrays from AlphaChunkData
            chunk->blocks.assign(data->blocks, data->blocks + data->blocks_len);
            chunk->data.data.assign(data->data, data->data + data->data_len);
            chunk->skylight.data.assign(data->sky_light, data->sky_light + data->sky_light_len);
            chunk->blocklight.data.assign(data->block_light, data->block_light + data->block_light_len);
            chunk->heightMap.assign(data->height_map, data->height_map + data->height_map_len);
            chunk->isTerrainPopulated = data->terrain_populated;

            // Validate data arrays (same as C++ fallback code)
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
            if (data->tile_entities && data->tile_entities_count > 0) {
                for (size_t i = 0; i < data->tile_entities_count; ++i) {
                    NbtCompound* r_comp = data->tile_entities[i];
                    if (r_comp) {
                        auto cpp_comp = rust_to_cpp_compound(r_comp);
                        if (cpp_comp) {
                            auto tileEntity = TileEntity::createFromNBT(*cpp_comp);
                            if (tileEntity) {
                                tileEntity->worldObj = world;
                                chunk->addTileEntity(tileEntity.release());
                            }
                        }
                    }
                }
            }

            // Load EntityItems
            chunk->pendingItems.clear();
            if (data->items && data->items_count > 0) {
                for (size_t i = 0; i < data->items_count; ++i) {
                    NbtCompound* r_comp = data->items[i];
                    if (r_comp) {
                        auto cpp_comp = rust_to_cpp_compound(r_comp);
                        if (cpp_comp) {
                            ChunkEntityData ed;
                            ed.itemID      = cpp_comp->getInt("id");
                            ed.count       = cpp_comp->getInt("count");
                            ed.metadata    = cpp_comp->getInt("meta");
                            ed.age         = cpp_comp->getInt("age");
                            ed.pickupDelay = cpp_comp->getInt("delay");
                            ed.posX        = cpp_comp->getDouble("x");
                            ed.posY        = cpp_comp->getDouble("y");
                            ed.posZ        = cpp_comp->getDouble("z");
                            if (ed.age < 6000)
                                chunk->pendingItems.push_back(ed);
                        }
                    }
                }
            }

            // Load Animals
            chunk->pendingAnimals.clear();
            if (data->animals && data->animals_count > 0) {
                for (size_t i = 0; i < data->animals_count; ++i) {
                    NbtCompound* r_comp = data->animals[i];
                    if (r_comp) {
                        auto cpp_comp = rust_to_cpp_compound(r_comp);
                        if (cpp_comp) {
                            ByteBuffer buf;
                            cpp_comp->writeRoot(buf, cpp_comp->getString("id"));
                            chunk->pendingAnimals.push_back({
                                .entityId = cpp_comp->getString("id"),
                                .nbtData = std::move(buf.data),
                                .posX = cpp_comp->getDouble("PosX"),
                                .posY = cpp_comp->getDouble("PosY"),
                                .posZ = cpp_comp->getDouble("PosZ"),
                            });
                        }
                    }
                }
            }

            // Load Monsters
            chunk->pendingMonsters.clear();
            if (data->monsters && data->monsters_count > 0) {
                for (size_t i = 0; i < data->monsters_count; ++i) {
                    NbtCompound* r_comp = data->monsters[i];
                    if (r_comp) {
                        auto cpp_comp = rust_to_cpp_compound(r_comp);
                        if (cpp_comp) {
                            ByteBuffer buf;
                            cpp_comp->writeRoot(buf, cpp_comp->getString("id"));
                            chunk->pendingMonsters.push_back({
                                .entityId = cpp_comp->getString("id"),
                                .nbtData = std::move(buf.data),
                                .posX = cpp_comp->getDouble("PosX"),
                                .posY = cpp_comp->getDouble("PosY"),
                                .posZ = cpp_comp->getDouble("PosZ"),
                            });
                        }
                    }
                }
            }

            // Load Boats
            chunk->pendingBoats.clear();
            if (data->boats && data->boats_count > 0) {
                for (size_t i = 0; i < data->boats_count; ++i) {
                    NbtCompound* r_comp = data->boats[i];
                    if (r_comp) {
                        auto cpp_comp = rust_to_cpp_compound(r_comp);
                        if (cpp_comp) {
                            ByteBuffer buf;
                            cpp_comp->writeRoot(buf, "Boat");
                            chunk->pendingBoats.push_back({
                                .nbtData = std::move(buf.data),
                                .posX = cpp_comp->getDouble("PosX"),
                                .posY = cpp_comp->getDouble("PosY"),
                                .posZ = cpp_comp->getDouble("PosZ"),
                            });
                        }
                    }
                }
            }

            // Safely free all heap allocated arrays in Rust
            alpha_chunk_data_free(data);

            std::cout << "[ChunkLoader] Loaded chunk (" << chunkX << ", " 
                      << chunkZ << ") with " 
                      << chunk->getTileEntities().size() << " TileEntities and "
                      << chunk->pendingItems.size() + chunk->pendingAnimals.size() + chunk->pendingMonsters.size() + chunk->pendingBoats.size()
                      << " entities" << std::endl;

            return chunk;

        } catch (const std::exception& e) {
            std::cerr << "[ChunkLoader] Error loading chunk: " << e.what() << std::endl;
            return nullptr;
        }
    }

public:
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

private:
    std::string worldDirectory_;
    bool createDirectories_;
};
