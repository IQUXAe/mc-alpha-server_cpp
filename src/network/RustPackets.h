#pragma once

#include "alpha_bridge.h"
#include <string>
#include <algorithm>
#include <cstdint>

namespace RustPackets {

inline RustPacket keepAlive() {
    RustPacket p{};
    p.packet_id = 0;
    return p;
}

inline RustPacket login(int32_t entityId, const char* username, const char* password, int64_t seed, int8_t dimension) {
    RustPacket p{};
    p.packet_id = 1;
    p.data.login.protocol_version = entityId;
    p.data.login.username = username;
    p.data.login.password = password;
    p.data.login.map_seed = seed;
    p.data.login.dimension = dimension;
    return p;
}

inline RustPacket login(int32_t entityId, const std::string& username, const std::string& password, int64_t seed, int8_t dimension) {
    return login(entityId, username.c_str(), password.c_str(), seed, dimension);
}

inline RustPacket handshake(const char* username) {
    RustPacket p{};
    p.packet_id = 2;
    p.data.handshake.username = username;
    return p;
}

inline RustPacket handshake(const std::string& username) {
    return handshake(username.c_str());
}

inline RustPacket chat(const char* message) {
    RustPacket p{};
    p.packet_id = 3;
    p.data.chat.message = message;
    return p;
}

inline RustPacket chat(const std::string& message) {
    return chat(message.c_str());
}

inline RustPacket updateTime(int64_t time) {
    RustPacket p{};
    p.packet_id = 4;
    p.data.update_time.time = time;
    return p;
}

inline RustPacket playerInventory(int32_t type, int16_t count, const FfiSlotData* slots) {
    RustPacket p{};
    p.packet_id = 5;
    p.data.inventory.type = type;
    p.data.inventory.item_count = count;
    p.data.inventory.slots = slots;
    return p;
}

inline RustPacket spawnPosition(int x, int y, int z) {
    RustPacket p{};
    p.packet_id = 6;
    p.data.spawn_position.x = x;
    p.data.spawn_position.y = y;
    p.data.spawn_position.z = z;
    return p;
}

inline RustPacket useEntity(int playerEntityId, int targetEntityId, bool isLeftClick) {
    RustPacket p{};
    p.packet_id = 7;
    p.data.use_entity.player_entity_id = playerEntityId;
    p.data.use_entity.target_entity_id = targetEntityId;
    p.data.use_entity.is_left_click = isLeftClick;
    return p;
}

inline RustPacket updateHealth(int16_t health) {
    RustPacket p{};
    p.packet_id = 8;
    p.data.update_health.health = health;
    return p;
}

inline RustPacket respawn() {
    RustPacket p{};
    p.packet_id = 9;
    return p;
}

inline RustPacket flying(bool onGround) {
    RustPacket p{};
    p.packet_id = 10;
    p.data.flying.on_ground = onGround;
    return p;
}

inline RustPacket playerPosition(double x, double y, double stance, double z, bool onGround) {
    RustPacket p{};
    p.packet_id = 11;
    p.data.position.x = x;
    p.data.position.y = y;
    p.data.position.stance = stance;
    p.data.position.z = z;
    p.data.position.on_ground = onGround;
    return p;
}

inline RustPacket playerLook(float yaw, float pitch, bool onGround) {
    RustPacket p{};
    p.packet_id = 12;
    p.data.look.yaw = yaw;
    p.data.look.pitch = pitch;
    p.data.look.on_ground = onGround;
    return p;
}

inline RustPacket playerLookMove(double x, double y, double stance, double z, float yaw, float pitch, bool onGround) {
    RustPacket p{};
    p.packet_id = 13;
    p.data.look_move.x = x;
    p.data.look_move.y = y;
    p.data.look_move.stance = stance;
    p.data.look_move.z = z;
    p.data.look_move.yaw = yaw;
    p.data.look_move.pitch = pitch;
    p.data.look_move.on_ground = onGround;
    return p;
}

inline RustPacket blockDig(int8_t status, int x, int8_t y, int z, int8_t face) {
    RustPacket p{};
    p.packet_id = 14;
    p.data.block_dig.status = status;
    p.data.block_dig.x = x;
    p.data.block_dig.y = y;
    p.data.block_dig.z = z;
    p.data.block_dig.face = face;
    return p;
}

inline RustPacket place(int16_t itemId, int x, int8_t y, int z, int8_t direction) {
    RustPacket p{};
    p.packet_id = 15;
    p.data.place.item_id = itemId;
    p.data.place.x = x;
    p.data.place.y = y;
    p.data.place.z = z;
    p.data.place.direction = direction;
    return p;
}

inline RustPacket blockItemSwitch(int entityId, int itemId) {
    RustPacket p{};
    p.packet_id = 16;
    p.data.item_switch.entity_id = entityId;
    p.data.item_switch.item_id = static_cast<int16_t>(itemId);
    return p;
}

inline RustPacket addToInventory(int itemId, int count, int damage) {
    RustPacket p{};
    p.packet_id = 17;
    p.data.add_to_inventory.item_id = static_cast<int16_t>(itemId);
    p.data.add_to_inventory.count = static_cast<int8_t>(count);
    p.data.add_to_inventory.damage = static_cast<int16_t>(damage);
    return p;
}

inline RustPacket armAnimation(int entityId, int animate) {
    RustPacket p{};
    p.packet_id = 18;
    p.data.arm_anim.entity_id = entityId;
    p.data.arm_anim.animate = static_cast<int8_t>(animate);
    return p;
}

inline RustPacket namedEntitySpawn(int entityId, const char* name, int x, int y, int z, int8_t yaw, int8_t pitch, int currentItem) {
    RustPacket p{};
    p.packet_id = 20;
    p.data.named_entity_spawn.entity_id = entityId;
    p.data.named_entity_spawn.name = name;
    p.data.named_entity_spawn.x = x;
    p.data.named_entity_spawn.y = y;
    p.data.named_entity_spawn.z = z;
    p.data.named_entity_spawn.rotation = yaw;
    p.data.named_entity_spawn.pitch = pitch;
    p.data.named_entity_spawn.current_item = static_cast<int16_t>(currentItem);
    return p;
}

inline RustPacket namedEntitySpawn(int entityId, const std::string& name, int x, int y, int z, int8_t yaw, int8_t pitch, int currentItem) {
    return namedEntitySpawn(entityId, name.c_str(), x, y, z, yaw, pitch, currentItem);
}

inline RustPacket pickupSpawn(int entityId, int itemId, int count, int x, int y, int z, int8_t rot, int8_t pitch, int8_t roll) {
    RustPacket p{};
    p.packet_id = 21;
    p.data.pickup_spawn.entity_id = entityId;
    p.data.pickup_spawn.item_id = static_cast<int16_t>(itemId);
    p.data.pickup_spawn.count = static_cast<int8_t>(count);
    p.data.pickup_spawn.x = x;
    p.data.pickup_spawn.y = y;
    p.data.pickup_spawn.z = z;
    p.data.pickup_spawn.rotation = rot;
    p.data.pickup_spawn.pitch = pitch;
    p.data.pickup_spawn.roll = roll;
    return p;
}

inline RustPacket collect(int collectedId, int collectorId) {
    RustPacket p{};
    p.packet_id = 22;
    p.data.collect.collected_entity_id = collectedId;
    p.data.collect.collector_entity_id = collectorId;
    return p;
}

inline RustPacket vehicleSpawn(int entityId, int8_t type, int x, int y, int z) {
    RustPacket p{};
    p.packet_id = 23;
    p.data.vehicle_spawn.entity_id = entityId;
    p.data.vehicle_spawn.vehicle_type = type;
    p.data.vehicle_spawn.x = x;
    p.data.vehicle_spawn.y = y;
    p.data.vehicle_spawn.z = z;
    return p;
}

inline RustPacket mobSpawn(int entityId, uint8_t type, int x, int y, int z, int8_t yaw, int8_t pitch) {
    RustPacket p{};
    p.packet_id = 24;
    p.data.mob_spawn.entity_id = entityId;
    p.data.mob_spawn.mob_type = type;
    p.data.mob_spawn.x = x;
    p.data.mob_spawn.y = y;
    p.data.mob_spawn.z = z;
    p.data.mob_spawn.yaw = yaw;
    p.data.mob_spawn.pitch = pitch;
    return p;
}

inline RustPacket velocity(int entityId, double vx, double vy, double vz) {
    RustPacket p{};
    p.packet_id = 28;
    p.data.entity_velocity.entity_id = entityId;
    p.data.entity_velocity.motion_x = static_cast<int16_t>(std::clamp(vx, -3.9, 3.9) * 8000.0);
    p.data.entity_velocity.motion_y = static_cast<int16_t>(std::clamp(vy, -3.9, 3.9) * 8000.0);
    p.data.entity_velocity.motion_z = static_cast<int16_t>(std::clamp(vz, -3.9, 3.9) * 8000.0);
    return p;
}

inline RustPacket destroyEntity(int entityId) {
    RustPacket p{};
    p.packet_id = 29;
    p.data.destroy_entity.entity_id = entityId;
    return p;
}

inline RustPacket entity(int entityId) {
    RustPacket p{};
    p.packet_id = 30;
    p.data.entity.entity_id = entityId;
    return p;
}

inline RustPacket relEntityMove(int entityId, int8_t dx, int8_t dy, int8_t dz) {
    RustPacket p{};
    p.packet_id = 31;
    p.data.rel_entity_move.entity_id = entityId;
    p.data.rel_entity_move.dx = dx;
    p.data.rel_entity_move.dy = dy;
    p.data.rel_entity_move.dz = dz;
    return p;
}

inline RustPacket entityLook(int entityId, int8_t yaw, int8_t pitch) {
    RustPacket p{};
    p.packet_id = 32;
    p.data.entity_look.entity_id = entityId;
    p.data.entity_look.yaw = yaw;
    p.data.entity_look.pitch = pitch;
    return p;
}

inline RustPacket relEntityMoveLook(int entityId, int8_t dx, int8_t dy, int8_t dz, int8_t yaw, int8_t pitch) {
    RustPacket p{};
    p.packet_id = 33;
    p.data.rel_entity_move_look.entity_id = entityId;
    p.data.rel_entity_move_look.dx = dx;
    p.data.rel_entity_move_look.dy = dy;
    p.data.rel_entity_move_look.dz = dz;
    p.data.rel_entity_move_look.yaw = yaw;
    p.data.rel_entity_move_look.pitch = pitch;
    return p;
}

inline RustPacket entityTeleport(int entityId, int x, int y, int z, int8_t yaw, int8_t pitch) {
    RustPacket p{};
    p.packet_id = 34;
    p.data.entity_teleport.entity_id = entityId;
    p.data.entity_teleport.x = x;
    p.data.entity_teleport.y = y;
    p.data.entity_teleport.z = z;
    p.data.entity_teleport.yaw = yaw;
    p.data.entity_teleport.pitch = pitch;
    return p;
}

inline RustPacket entityStatus(int entityId, int8_t status) {
    RustPacket p{};
    p.packet_id = 38;
    p.data.entity_status.entity_id = entityId;
    p.data.entity_status.status = status;
    return p;
}

inline RustPacket attachEntity(int entityId, int vehicleId) {
    RustPacket p{};
    p.packet_id = 39;
    p.data.attach_entity.entity_id = entityId;
    p.data.attach_entity.vehicle_id = vehicleId;
    return p;
}

inline RustPacket preChunk(int x, int z, bool mode) {
    RustPacket p{};
    p.packet_id = 50;
    p.data.pre_chunk.x = x;
    p.data.pre_chunk.z = z;
    p.data.pre_chunk.mode = mode;
    return p;
}

inline RustPacket blockChange(int x, int y, int z, uint8_t type, uint8_t meta) {
    RustPacket p{};
    p.packet_id = 53;
    p.data.block_change.x = x;
    p.data.block_change.y = static_cast<int8_t>(y);
    p.data.block_change.z = z;
    p.data.block_change.block_type = type;
    p.data.block_change.metadata = meta;
    return p;
}

inline RustPacket complexEntity(int x, int16_t y, int z, const uint8_t* nbtData, size_t nbtLen) {
    RustPacket p{};
    p.packet_id = 59;
    p.data.complex_entity.x = x;
    p.data.complex_entity.y = y;
    p.data.complex_entity.z = z;
    p.data.complex_entity.nbt_data = nbtData;
    p.data.complex_entity.nbt_len = nbtLen;
    return p;
}

inline RustPacket kickDisconnect(const char* reason) {
    RustPacket p{};
    p.packet_id = 255;
    p.data.kick.reason = reason;
    return p;
}

inline RustPacket kickDisconnect(const std::string& reason) {
    return kickDisconnect(reason.c_str());
}

} // namespace RustPackets
