#pragma once

#include "Chunk.h"
#include "World.h"
#include "TileEntityFurnace.h"
#include "TileEntityChest.h"
#include "TileEntitySign.h"
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

class ChunkLoader {
public:
    ChunkLoader(const std::string& worldDir, bool createIfMissing) 
        : worldDirectory_(worldDir), createDirectories_(createIfMissing) {
    }

    void saveChunk(World* world, Chunk* chunk) {
        if (!chunk) return;

        try {
            AlphaChunkData chunkData{};
            chunkData.x_pos = chunk->xPosition;
            chunkData.z_pos = chunk->zPosition;
            chunkData.last_update = world->worldTime;

            chunkData.blocks = chunk->blocks;
            chunkData.blocks_len = CHUNK_VOLUME;
            chunkData.blocks_capacity = 0;

            chunkData.data = chunk->data.data_ptr;
            chunkData.data_len = CHUNK_VOLUME / 2;
            chunkData.data_capacity = 0;

            chunkData.sky_light = chunk->skylight.data_ptr;
            chunkData.sky_light_len = CHUNK_VOLUME / 2;
            chunkData.sky_light_capacity = 0;

            chunkData.block_light = chunk->blocklight.data_ptr;
            chunkData.block_light_len = CHUNK_VOLUME / 2;
            chunkData.block_light_capacity = 0;

            chunkData.height_map = chunk->heightMap;
            chunkData.height_map_len = CHUNK_AREA;
            chunkData.height_map_capacity = 0;

            chunkData.terrain_populated = chunk->isTerrainPopulated;

            std::vector<FfiTileEntityFurnaceData> furnaces;
            std::vector<FfiTileEntityChestData> chests;
            std::vector<FfiTileEntitySignData> signs;

            for (const auto& [key, te] : chunk->getTileEntities()) {
                if (auto* f = dynamic_cast<TileEntityFurnace*>(te)) {
                    FfiTileEntityFurnaceData data{};
                    data.x = f->xCoord; data.y = f->yCoord; data.z = f->zCoord;
                    data.state = f->state_;
                    furnaces.push_back(data);
                } else if (auto* c = dynamic_cast<TileEntityChest*>(te)) {
                    FfiTileEntityChestData data{};
                    data.x = c->xCoord; data.y = c->yCoord; data.z = c->zCoord;
                    data.state = c->state_;
                    chests.push_back(data);
                } else if (auto* s = dynamic_cast<TileEntitySign*>(te)) {
                    FfiTileEntitySignData data{};
                    data.x = s->xCoord; data.y = s->yCoord; data.z = s->zCoord;
                    data.state = s->state_;
                    signs.push_back(data);
                }
            }

            chunkData.furnaces = furnaces.data(); chunkData.furnaces_count = furnaces.size();
            chunkData.chests = chests.data(); chunkData.chests_count = chests.size();
            chunkData.signs = signs.data(); chunkData.signs_count = signs.size();

            std::vector<FfiEntityItemData> items;
            std::vector<FfiEntityAnimalData> animals;
            std::vector<FfiEntityMonsterData> monsters;
            std::vector<FfiEntityBoatData> boats;
            std::vector<std::string> stringCache;

            for (const auto& ed : chunk->pendingItems) {
                items.push_back({ed.itemID, ed.count, ed.metadata, ed.age, ed.pickupDelay, ed.posX, ed.posY, ed.posZ});
            }
            for (const auto& e : world->entities_) {
                auto* item = dynamic_cast<EntityItem*>(e.get());
                if (!item || item->isDead) continue;
                int cx = static_cast<int>(std::floor(item->posX)) >> 4;
                int cz = static_cast<int>(std::floor(item->posZ)) >> 4;
                if (cx != chunk->xPosition || cz != chunk->zPosition) continue;
                items.push_back({item->itemID, item->count, item->metadata, item->age, item->pickupDelay, item->posX, item->posY, item->posZ});
            }
            chunkData.items = items.data(); chunkData.items_count = items.size();

            auto makeEntityKey = [](const std::string& id, double x, double y, double z) {
                const int32_t fx = static_cast<int32_t>(std::floor(x * 32.0));
                const int32_t fy = static_cast<int32_t>(std::floor(y * 32.0));
                const int32_t fz = static_cast<int32_t>(std::floor(z * 32.0));
                return id + "#" + std::to_string(fx) + ":" + std::to_string(fy) + ":" + std::to_string(fz);
            };

            std::unordered_set<std::string> seenAnimals;
            for (const auto& ad : chunk->pendingAnimals) {
                const std::string key = makeEntityKey(ad.id, ad.posX, ad.posY, ad.posZ);
                if (seenAnimals.insert(key).second) {
                    stringCache.push_back(ad.id);
                    animals.push_back({
                        stringCache.back().c_str(), ad.posX, ad.posY, ad.posZ,
                        ad.motionX, ad.motionY, ad.motionZ,
                        ad.rotationYaw, ad.rotationPitch,
                        ad.health, ad.maxHealth,
                        ad.saddled, ad.sheared, ad.eggLayTime
                    });
                }
            }
            for (const auto& e : world->entities_) {
                auto* animal = dynamic_cast<EntityAnimals*>(e.get());
                if (!animal || animal->isDead) continue;
                int cx = static_cast<int>(std::floor(animal->posX)) >> 4;
                int cz = static_cast<int>(std::floor(animal->posZ)) >> 4;
                if (cx != chunk->xPosition || cz != chunk->zPosition) continue;
                
                stringCache.push_back(animal->getEntityStringId());
                const std::string key = makeEntityKey(stringCache.back(), animal->posX, animal->posY, animal->posZ);
                if (!seenAnimals.insert(key).second) continue;

                bool saddled = false; bool sheared = false; int eggLayTime = 0;
                if (auto* pig = dynamic_cast<EntityPig*>(animal)) saddled = pig->saddled;
                if (auto* sheep = dynamic_cast<EntitySheep*>(animal)) sheared = sheep->sheared;
                if (auto* chicken = dynamic_cast<EntityChicken*>(animal)) eggLayTime = chicken->eggLayTime;

                animals.push_back({
                    stringCache.back().c_str(), animal->posX, animal->posY, animal->posZ,
                    animal->motionX, animal->motionY, animal->motionZ,
                    animal->rotationYaw, animal->rotationPitch,
                    animal->health, animal->maxHealth,
                    saddled, sheared, eggLayTime
                });
            }
            chunkData.animals = animals.data(); chunkData.animals_count = animals.size();

            std::unordered_set<std::string> seenMonsters;
            for (const auto& md : chunk->pendingMonsters) {
                const std::string key = makeEntityKey(md.id, md.posX, md.posY, md.posZ);
                if (seenMonsters.insert(key).second) {
                    stringCache.push_back(md.id);
                    monsters.push_back({
                        stringCache.back().c_str(), md.posX, md.posY, md.posZ,
                        md.motionX, md.motionY, md.motionZ,
                        md.rotationYaw, md.rotationPitch,
                        md.health, md.maxHealth
                    });
                }
            }
            for (const auto& e : world->entities_) {
                auto* mob = dynamic_cast<EntityMob*>(e.get());
                if (!mob || mob->isDead) continue;
                int cx = static_cast<int>(std::floor(mob->posX)) >> 4;
                int cz = static_cast<int>(std::floor(mob->posZ)) >> 4;
                if (cx != chunk->xPosition || cz != chunk->zPosition) continue;

                stringCache.push_back(mob->getEntityStringId());
                const std::string key = makeEntityKey(stringCache.back(), mob->posX, mob->posY, mob->posZ);
                if (!seenMonsters.insert(key).second) continue;

                monsters.push_back({
                    stringCache.back().c_str(), mob->posX, mob->posY, mob->posZ,
                    mob->motionX, mob->motionY, mob->motionZ,
                    mob->rotationYaw, mob->rotationPitch,
                    mob->health, mob->maxHealth
                });
            }
            chunkData.monsters = monsters.data(); chunkData.monsters_count = monsters.size();

            std::unordered_set<std::string> seenBoats;
            for (const auto& bd : chunk->pendingBoats) {
                const std::string key = makeEntityKey("Boat", bd.posX, bd.posY, bd.posZ);
                if (seenBoats.insert(key).second) {
                    boats.push_back({
                        bd.posX, bd.posY, bd.posZ,
                        bd.motionX, bd.motionY, bd.motionZ,
                        bd.rotationYaw, bd.rotationPitch,
                        bd.timeSinceHit, bd.damageTaken, bd.forwardDirection
                    });
                }
            }
            for (const auto& e : world->entities_) {
                auto* boat = dynamic_cast<EntityBoat*>(e.get());
                if (!boat || boat->isDead) continue;
                int cx = static_cast<int>(std::floor(boat->posX)) >> 4;
                int cz = static_cast<int>(std::floor(boat->posZ)) >> 4;
                if (cx != chunk->xPosition || cz != chunk->zPosition) continue;

                const std::string key = makeEntityKey("Boat", boat->posX, boat->posY, boat->posZ);
                if (!seenBoats.insert(key).second) continue;

                boats.push_back({
                    boat->posX, boat->posY, boat->posZ,
                    boat->motionX, boat->motionY, boat->motionZ,
                    boat->rotationYaw, boat->rotationPitch,
                    boat->timeSinceHit, boat->damageTaken, boat->forwardDirection
                });
            }
            chunkData.boats = boats.data(); chunkData.boats_count = boats.size();

            bool success = alpha_chunk_loader_save(worldDirectory_.c_str(), createDirectories_, &chunkData);

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

    Chunk* loadChunk(World* world, int chunkX, int chunkZ) {
        try {
            AlphaChunkData* data = alpha_chunk_loader_load(worldDirectory_.c_str(), chunkX, chunkZ);

            if (!data) {
                return nullptr;
            }

            Chunk* chunk = new Chunk(
                world, 
                data->x_pos, 
                data->z_pos,
                data->blocks,
                data->data,
                data->sky_light,
                data->block_light,
                data->height_map
            );
            chunk->isTerrainPopulated = data->terrain_populated;

            if (data->furnaces && data->furnaces_count > 0) {
                for (size_t i = 0; i < data->furnaces_count; ++i) {
                    auto* f = new TileEntityFurnace();
                    f->worldObj = world; f->xCoord = data->furnaces[i].x; f->yCoord = data->furnaces[i].y; f->zCoord = data->furnaces[i].z;
                    f->state_ = data->furnaces[i].state;
                    chunk->addTileEntity(f);
                }
            }
            if (data->chests && data->chests_count > 0) {
                for (size_t i = 0; i < data->chests_count; ++i) {
                    auto* c = new TileEntityChest();
                    c->worldObj = world; c->xCoord = data->chests[i].x; c->yCoord = data->chests[i].y; c->zCoord = data->chests[i].z;
                    c->state_ = data->chests[i].state;
                    chunk->addTileEntity(c);
                }
            }
            if (data->signs && data->signs_count > 0) {
                for (size_t i = 0; i < data->signs_count; ++i) {
                    auto* s = new TileEntitySign();
                    s->worldObj = world; s->xCoord = data->signs[i].x; s->yCoord = data->signs[i].y; s->zCoord = data->signs[i].z;
                    s->state_ = data->signs[i].state;
                    chunk->addTileEntity(s);
                }
            }

            chunk->pendingItems.clear();
            if (data->items && data->items_count > 0) {
                for (size_t i = 0; i < data->items_count; ++i) {
                    ChunkEntityData ed;
                    ed.itemID = data->items[i].item_id;
                    ed.count = data->items[i].count;
                    ed.metadata = data->items[i].meta;
                    ed.age = data->items[i].age;
                    ed.pickupDelay = data->items[i].delay;
                    ed.posX = data->items[i].x; ed.posY = data->items[i].y; ed.posZ = data->items[i].z;
                    if (ed.age < 6000) chunk->pendingItems.push_back(ed);
                }
            }

            chunk->pendingAnimals.clear();
            if (data->animals && data->animals_count > 0) {
                for (size_t i = 0; i < data->animals_count; ++i) {
                    const auto& a = data->animals[i];
                    ChunkAnimalData ad;
                    ad.id = a.id ? a.id : "";
                    ad.posX = a.x; ad.posY = a.y; ad.posZ = a.z;
                    ad.motionX = a.motion_x; ad.motionY = a.motion_y; ad.motionZ = a.motion_z;
                    ad.rotationYaw = a.rotation_yaw; ad.rotationPitch = a.rotation_pitch;
                    ad.health = a.health; ad.maxHealth = a.max_health;
                    ad.saddled = a.saddled; ad.sheared = a.sheared; ad.eggLayTime = a.egg_lay_time;
                    chunk->pendingAnimals.push_back(ad);
                }
            }

            chunk->pendingMonsters.clear();
            if (data->monsters && data->monsters_count > 0) {
                for (size_t i = 0; i < data->monsters_count; ++i) {
                    const auto& m = data->monsters[i];
                    ChunkAnimalData md;
                    md.id = m.id ? m.id : "";
                    md.posX = m.x; md.posY = m.y; md.posZ = m.z;
                    md.motionX = m.motion_x; md.motionY = m.motion_y; md.motionZ = m.motion_z;
                    md.rotationYaw = m.rotation_yaw; md.rotationPitch = m.rotation_pitch;
                    md.health = m.health; md.maxHealth = m.max_health;
                    chunk->pendingMonsters.push_back(md);
                }
            }

            chunk->pendingBoats.clear();
            if (data->boats && data->boats_count > 0) {
                for (size_t i = 0; i < data->boats_count; ++i) {
                    const auto& b = data->boats[i];
                    ChunkBoatData bd;
                    bd.posX = b.x; bd.posY = b.y; bd.posZ = b.z;
                    bd.motionX = b.motion_x; bd.motionY = b.motion_y; bd.motionZ = b.motion_z;
                    bd.rotationYaw = b.rotation_yaw; bd.rotationPitch = b.rotation_pitch;
                    bd.timeSinceHit = b.time_since_hit; bd.damageTaken = b.damage_taken; bd.forwardDirection = b.forward_direction;
                    chunk->pendingBoats.push_back(bd);
                }
            }

            alpha_chunk_data_free_except_arrays(data);

            std::cout << "[ChunkLoader] Loaded chunk (" << chunkX << ", " << chunkZ << ")" << std::endl;
            return chunk;

        } catch (const std::exception& e) {
            std::cerr << "[ChunkLoader] Error loading chunk: " << e.what() << std::endl;
            return nullptr;
        }
    }

    std::filesystem::path getChunkFile(int chunkX, int chunkZ) {
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
