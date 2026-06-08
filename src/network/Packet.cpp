#include "Packet.h"
#include "packets/AllPackets.h"
#include "alpha_bridge.h"

#include <sys/socket.h>
#include <unistd.h>
#include <bit>
#include <stdexcept>
#include <typeinfo>

std::unordered_map<int, std::function<std::unique_ptr<Packet>()>> Packet::idToFactory;
std::unordered_map<std::string, int> Packet::classToId;

void Packet::registerPackets() {
    auto reg = []<typename T>(int id) {
        idToFactory[id] = [] { return std::make_unique<T>(); };
        classToId[typeid(T).name()] = id;
    };
    reg.operator()<Packet0KeepAlive>(0);
    reg.operator()<Packet1Login>(1);
    reg.operator()<Packet2Handshake>(2);
    reg.operator()<Packet3Chat>(3);
    reg.operator()<Packet4UpdateTime>(4);
    reg.operator()<Packet5PlayerInventory>(5);
    reg.operator()<Packet6SpawnPosition>(6);
    reg.operator()<Packet7UseEntity>(7);
    reg.operator()<Packet8UpdateHealth>(8);
    reg.operator()<Packet9Respawn>(9);
    reg.operator()<Packet10Flying>(10);
    reg.operator()<Packet11PlayerPosition>(11);
    reg.operator()<Packet12PlayerLook>(12);
    reg.operator()<Packet13PlayerLookMove>(13);
    reg.operator()<Packet14BlockDig>(14);
    reg.operator()<Packet15Place>(15);
    reg.operator()<Packet16BlockItemSwitch>(16);
    reg.operator()<Packet17AddToInventory>(17);
    reg.operator()<Packet18ArmAnimation>(18);
    reg.operator()<Packet20NamedEntitySpawn>(20);
    reg.operator()<Packet21PickupSpawn>(21);
    reg.operator()<Packet22Collect>(22);
    reg.operator()<Packet23VehicleSpawn>(23);
    reg.operator()<Packet24MobSpawn>(24);
    reg.operator()<Packet28EntityVelocity>(28);
    reg.operator()<Packet29DestroyEntity>(29);
    reg.operator()<Packet30Entity>(30);
    reg.operator()<Packet31RelEntityMove>(31);
    reg.operator()<Packet32EntityLook>(32);
    reg.operator()<Packet33RelEntityMoveLook>(33);
    reg.operator()<Packet34EntityTeleport>(34);
    reg.operator()<Packet38EntityStatus>(38);
    reg.operator()<Packet39AttachEntity>(39);
    reg.operator()<Packet50PreChunk>(50);
    reg.operator()<Packet51MapChunk>(51);
    reg.operator()<Packet52MultiBlockChange>(52);
    reg.operator()<Packet53BlockChange>(53);
    reg.operator()<Packet59ComplexEntity>(59);
    reg.operator()<Packet60Explosion>(60);
    reg.operator()<Packet255KickDisconnect>(255);
}

int Packet::getPacketId() const {
    auto it = classToId.find(typeid(*this).name());
    if (it == classToId.end())
        throw std::runtime_error("Unknown packet class: " + std::string(typeid(*this).name()));
    return it->second;
}

std::unique_ptr<Packet> Packet::createPacket(int id) {
    auto it = idToFactory.find(id);
    return it != idToFactory.end() ? it->second() : nullptr;
}

std::unique_ptr<Packet> Packet::createFromFfi(const RustPacket* ffiPacket) {
    if (!ffiPacket) return nullptr;

    switch (ffiPacket->packet_id) {
        case 0: {
            return std::make_unique<Packet0KeepAlive>();
        }
        case 1: {
            auto p = std::make_unique<Packet1Login>();
            p->protocolVersion = ffiPacket->data.login.protocol_version;
            if (ffiPacket->data.login.username) p->username = ffiPacket->data.login.username;
            if (ffiPacket->data.login.password) p->password = ffiPacket->data.login.password;
            p->mapSeed = ffiPacket->data.login.map_seed;
            p->dimension = ffiPacket->data.login.dimension;
            return p;
        }
        case 2: {
            auto p = std::make_unique<Packet2Handshake>();
            if (ffiPacket->data.handshake.username) p->username = ffiPacket->data.handshake.username;
            return p;
        }
        case 3: {
            auto p = std::make_unique<Packet3Chat>();
            if (ffiPacket->data.chat.message) p->message = ffiPacket->data.chat.message;
            return p;
        }
        case 5: {
            auto p = std::make_unique<Packet5PlayerInventory>();
            p->type = ffiPacket->data.inventory.type;
            p->itemCount = ffiPacket->data.inventory.item_count;
            p->slots.resize(p->itemCount);
            for (int i = 0; i < p->itemCount; ++i) {
                p->slots[i].itemId = ffiPacket->data.inventory.slots[i].item_id;
                p->slots[i].count = ffiPacket->data.inventory.slots[i].count;
                p->slots[i].damage = ffiPacket->data.inventory.slots[i].damage;
            }
            return p;
        }
        case 7: {
            auto p = std::make_unique<Packet7UseEntity>();
            p->playerEntityId = ffiPacket->data.use_entity.player_entity_id;
            p->targetEntityId = ffiPacket->data.use_entity.target_entity_id;
            p->isLeftClick = ffiPacket->data.use_entity.is_left_click;
            return p;
        }
        case 9: {
            return std::make_unique<Packet9Respawn>();
        }
        case 10: {
            auto p = std::make_unique<Packet10Flying>();
            p->onGround = ffiPacket->data.flying.on_ground;
            return p;
        }
        case 11: {
            auto p = std::make_unique<Packet11PlayerPosition>();
            p->x = ffiPacket->data.position.x;
            p->y = ffiPacket->data.position.y;
            p->stance = ffiPacket->data.position.stance;
            p->z = ffiPacket->data.position.z;
            p->onGround = ffiPacket->data.position.on_ground;
            return p;
        }
        case 12: {
            auto p = std::make_unique<Packet12PlayerLook>();
            p->yaw = ffiPacket->data.look.yaw;
            p->pitch = ffiPacket->data.look.pitch;
            p->onGround = ffiPacket->data.look.on_ground;
            return p;
        }
        case 13: {
            auto p = std::make_unique<Packet13PlayerLookMove>();
            p->x = ffiPacket->data.look_move.x;
            p->y = ffiPacket->data.look_move.y;
            p->stance = ffiPacket->data.look_move.stance;
            p->z = ffiPacket->data.look_move.z;
            p->yaw = ffiPacket->data.look_move.yaw;
            p->pitch = ffiPacket->data.look_move.pitch;
            p->onGround = ffiPacket->data.look_move.on_ground;
            return p;
        }
        case 14: {
            auto p = std::make_unique<Packet14BlockDig>();
            p->status = ffiPacket->data.block_dig.status;
            p->x = ffiPacket->data.block_dig.x;
            p->y = ffiPacket->data.block_dig.y;
            p->z = ffiPacket->data.block_dig.z;
            p->face = ffiPacket->data.block_dig.face;
            return p;
        }
        case 15: {
            auto p = std::make_unique<Packet15Place>();
            p->itemId = ffiPacket->data.place.item_id;
            p->x = ffiPacket->data.place.x;
            p->y = ffiPacket->data.place.y;
            p->z = ffiPacket->data.place.z;
            p->direction = ffiPacket->data.place.direction;
            return p;
        }
        case 16: {
            auto p = std::make_unique<Packet16BlockItemSwitch>();
            p->entityId = ffiPacket->data.item_switch.entity_id;
            p->itemId = ffiPacket->data.item_switch.item_id;
            return p;
        }
        case 18: {
            auto p = std::make_unique<Packet18ArmAnimation>();
            p->entityId = ffiPacket->data.arm_anim.entity_id;
            p->animate = ffiPacket->data.arm_anim.animate;
            return p;
        }
        case 21: {
            auto p = std::make_unique<Packet21PickupSpawn>();
            p->entityId = ffiPacket->data.pickup_spawn.entity_id;
            p->itemId = ffiPacket->data.pickup_spawn.item_id;
            p->count = ffiPacket->data.pickup_spawn.count;
            p->x = ffiPacket->data.pickup_spawn.x;
            p->y = ffiPacket->data.pickup_spawn.y;
            p->z = ffiPacket->data.pickup_spawn.z;
            p->rotation = ffiPacket->data.pickup_spawn.rotation;
            p->pitch = ffiPacket->data.pickup_spawn.pitch;
            p->roll = ffiPacket->data.pickup_spawn.roll;
            return p;
        }
        case 59: {
            auto p = std::make_unique<Packet59ComplexEntity>();
            p->x = ffiPacket->data.complex_entity.x;
            p->y = ffiPacket->data.complex_entity.y;
            p->z = ffiPacket->data.complex_entity.z;
            if (ffiPacket->data.complex_entity.nbt_data && ffiPacket->data.complex_entity.nbt_len > 0) {
                p->nbtData.assign(ffiPacket->data.complex_entity.nbt_data,
                                  ffiPacket->data.complex_entity.nbt_data + ffiPacket->data.complex_entity.nbt_len);
            }
            return p;
        }
        case 255: {
            auto p = std::make_unique<Packet255KickDisconnect>();
            if (ffiPacket->data.kick.reason) p->reason = ffiPacket->data.kick.reason;
            return p;
        }
        default:
            return nullptr;
    }
}

