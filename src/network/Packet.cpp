#include "Packet.h"
#include "packets/AllPackets.h"

#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <stdexcept>
#include <typeinfo>

std::unordered_map<int, std::function<std::unique_ptr<Packet>()>> Packet::idToFactory;
std::unordered_map<std::string, int> Packet::classToId;

template<typename T>
static void registerPacket(int id) {
    Packet::idToFactory[id] = []() -> std::unique_ptr<Packet> { return std::make_unique<T>(); };
    Packet::classToId[typeid(T).name()] = id;
}

void Packet::registerPackets() {
    registerPacket<Packet0KeepAlive>(0);
    registerPacket<Packet1Login>(1);
    registerPacket<Packet2Handshake>(2);
    registerPacket<Packet3Chat>(3);
    registerPacket<Packet4UpdateTime>(4);
    registerPacket<Packet5PlayerInventory>(5);
    registerPacket<Packet6SpawnPosition>(6);
    registerPacket<Packet7UseEntity>(7);
    registerPacket<Packet8UpdateHealth>(8);
    registerPacket<Packet9Respawn>(9);
    registerPacket<Packet10Flying>(10);
    registerPacket<Packet11PlayerPosition>(11);
    registerPacket<Packet12PlayerLook>(12);
    registerPacket<Packet13PlayerLookMove>(13);
    registerPacket<Packet14BlockDig>(14);
    registerPacket<Packet15Place>(15);
    registerPacket<Packet16BlockItemSwitch>(16);
    registerPacket<Packet17AddToInventory>(17);
    registerPacket<Packet18ArmAnimation>(18);
    registerPacket<Packet20NamedEntitySpawn>(20);
    registerPacket<Packet21PickupSpawn>(21);
    registerPacket<Packet22Collect>(22);
    registerPacket<Packet23VehicleSpawn>(23);
    registerPacket<Packet24MobSpawn>(24);
    registerPacket<Packet28EntityVelocity>(28);
    registerPacket<Packet29DestroyEntity>(29);
    registerPacket<Packet30Entity>(30);
    registerPacket<Packet31RelEntityMove>(31);
    registerPacket<Packet32EntityLook>(32);
    registerPacket<Packet33RelEntityMoveLook>(33);
    registerPacket<Packet34EntityTeleport>(34);
    registerPacket<Packet38EntityStatus>(38);
    registerPacket<Packet39AttachEntity>(39);
    registerPacket<Packet50PreChunk>(50);
    registerPacket<Packet51MapChunk>(51);
    registerPacket<Packet52MultiBlockChange>(52);
    registerPacket<Packet53BlockChange>(53);
    registerPacket<Packet59ComplexEntity>(59);
    registerPacket<Packet60Explosion>(60);
    registerPacket<Packet255KickDisconnect>(255);
}

int Packet::getPacketId() const {
    auto it = classToId.find(typeid(*this).name());
    if (it == classToId.end()) {
        throw std::runtime_error("Unknown packet class: " + std::string(typeid(*this).name()));
    }
    return it->second;
}

std::unique_ptr<Packet> Packet::createPacket(int id) {
    auto it = idToFactory.find(id);
    if (it == idToFactory.end()) return nullptr;
    return it->second();
}

// Helper: read exactly n bytes from fd
static bool readFully(int fd, uint8_t* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::recv(fd, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

// Helper: write exactly n bytes to fd
static bool writeFully(int fd, const uint8_t* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::send(fd, buf + total, len - total, MSG_NOSIGNAL);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

std::unique_ptr<Packet> Packet::readPacket(int fd) {
    // Read 1-byte packet ID
    uint8_t packetId;
    if (!readFully(fd, &packetId, 1)) return nullptr;

    auto pkt = createPacket(static_cast<int>(packetId));
    if (!pkt) {
        throw std::runtime_error("Bad packet id " + std::to_string(packetId));
    }

    // For reading, we need to read from the socket directly
    // The tricky part: we don't know the packet size upfront
    // We'll read field by field using a socket-backed ByteBuffer
    // For simplicity, we wrap socket reads in a ByteBuffer adapter

    // Actually, for the Minecraft protocol, each packet type knows
    // its own format, so we create a SocketByteBuffer that reads from fd
    class SocketReader : public ByteBuffer {
    public:
        int fd_;
        SocketReader(int fd) : fd_(fd) {}

        // Override read methods to read from socket
        int8_t readByte() {
            uint8_t b;
            if (!readFully(fd_, &b, 1)) throw std::runtime_error("Connection lost");
            return static_cast<int8_t>(b);
        }

        uint8_t readUByte() {
            uint8_t b;
            if (!readFully(fd_, &b, 1)) throw std::runtime_error("Connection lost");
            return b;
        }

        int16_t readShort() {
            uint8_t buf[2];
            if (!readFully(fd_, buf, 2)) throw std::runtime_error("Connection lost");
            return static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) | buf[1]);
        }

        int32_t readInt() {
            uint8_t buf[4];
            if (!readFully(fd_, buf, 4)) throw std::runtime_error("Connection lost");
            return static_cast<int32_t>(
                (static_cast<uint32_t>(buf[0]) << 24) |
                (static_cast<uint32_t>(buf[1]) << 16) |
                (static_cast<uint32_t>(buf[2]) << 8) |
                buf[3]);
        }

        int64_t readLong() {
            uint8_t buf[8];
            if (!readFully(fd_, buf, 8)) throw std::runtime_error("Connection lost");
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i) v = (v << 8) | buf[i];
            return static_cast<int64_t>(v);
        }

        float readFloat() {
            int32_t i = readInt();
            uint32_t u = static_cast<uint32_t>(i);
            float f;
            std::memcpy(&f, &u, sizeof(f));
            return f;
        }

        double readDouble() {
            int64_t i = readLong();
            uint64_t u = static_cast<uint64_t>(i);
            double d;
            std::memcpy(&d, &u, sizeof(d));
            return d;
        }

        std::string readUTF() {
            int16_t len = readShort();
            if (len < 0) throw std::runtime_error("Negative UTF length");
            std::string s(static_cast<size_t>(len), '\0');
            if (len > 0) {
                if (!readFully(fd_, reinterpret_cast<uint8_t*>(&s[0]), static_cast<size_t>(len)))
                    throw std::runtime_error("Connection lost");
            }
            return s;
        }

        bool readBool() {
            return readByte() != 0;
        }

        void readBytes(uint8_t* buf, size_t len) {
            if (!readFully(fd_, buf, len)) throw std::runtime_error("Connection lost");
        }

        std::vector<uint8_t> readBytes(size_t len) {
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
    if (!writeFully(fd, buf.data.data(), buf.data.size())) {
        throw std::runtime_error("Failed to write packet");
    }
}
