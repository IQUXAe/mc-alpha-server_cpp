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

static bool readFully(int fd, uint8_t* buf, size_t len) {
    for (size_t total = 0; total < len; ) {
        ssize_t n = ::recv(fd, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

static bool writeFully(int fd, const uint8_t* buf, size_t len) {
    for (size_t total = 0; total < len; ) {
        ssize_t n = ::send(fd, buf + total, len - total, MSG_NOSIGNAL);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

std::unique_ptr<Packet> Packet::readPacket(int fd) {
    uint8_t packetId;
    if (!readFully(fd, &packetId, 1)) return nullptr;

    auto pkt = createPacket(static_cast<int>(packetId));
    if (!pkt) throw std::runtime_error("Bad packet id " + std::to_string(packetId));

    struct SocketReader : ByteBuffer {
        int fd_;
        explicit SocketReader(int fd) : fd_(fd) {}

        int8_t  readByte()  override { uint8_t b; if (!readFully(fd_, &b, 1)) throw std::runtime_error("Connection lost"); return static_cast<int8_t>(b); }
        uint8_t readUByte() override { uint8_t b; if (!readFully(fd_, &b, 1)) throw std::runtime_error("Connection lost"); return b; }

        int16_t readShort() override {
            uint8_t buf[2]; if (!readFully(fd_, buf, 2)) throw std::runtime_error("Connection lost");
            return static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) | buf[1]);
        }
        int32_t readInt() override {
            uint8_t buf[4]; if (!readFully(fd_, buf, 4)) throw std::runtime_error("Connection lost");
            return static_cast<int32_t>((static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16)
                                      | (static_cast<uint32_t>(buf[2]) << 8)  |  static_cast<uint32_t>(buf[3]));
        }
        int64_t readLong() override {
            uint8_t buf[8]; if (!readFully(fd_, buf, 8)) throw std::runtime_error("Connection lost");
            uint64_t v = 0;
            for (auto b : buf) v = (v << 8) | b;
            return static_cast<int64_t>(v);
        }
        float  readFloat()  override { return std::bit_cast<float>(static_cast<uint32_t>(readInt())); }
        double readDouble() override { return std::bit_cast<double>(static_cast<uint64_t>(readLong())); }
        bool   readBool()   override { return readByte() != 0; }

        std::string readUTF() override {
            int16_t len = readShort();
            if (len < 0) throw std::runtime_error("Negative UTF length");
            std::string s(static_cast<size_t>(len), '\0');
            if (len > 0 && !readFully(fd_, reinterpret_cast<uint8_t*>(s.data()), static_cast<size_t>(len)))
                throw std::runtime_error("Connection lost");
            return s;
        }
        void readBytes(uint8_t* buf, size_t len) override {
            if (!readFully(fd_, buf, len)) throw std::runtime_error("Connection lost");
        }
        std::vector<uint8_t> readBytes(size_t len) override {
            std::vector<uint8_t> result(len);
            if (!readFully(fd_, result.data(), len)) throw std::runtime_error("Connection lost");
            return result;
        }
    };

    SocketReader reader(fd);
    pkt->readPacketData(reader);
    return pkt;
}

void Packet::writePacket(Packet& pkt, int fd) {
    ByteBuffer buf;
    buf.writeUByte(static_cast<uint8_t>(pkt.getPacketId()));
    pkt.writePacketData(buf);
    if (!writeFully(fd, buf.data.data(), buf.data.size()))
        throw std::runtime_error("Failed to write packet");
}
