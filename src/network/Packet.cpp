#include "Packet.h"
#include "packets/AllPackets.h"

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

