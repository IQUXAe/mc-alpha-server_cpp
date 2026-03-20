#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>

class NetHandler;

// Big-endian byte buffer for Minecraft protocol I/O
class ByteBuffer {
public:
    std::vector<uint8_t> data;
    size_t readPos = 0;

    ByteBuffer() = default;
    explicit ByteBuffer(const std::vector<uint8_t>& d) : data(d) {}

    // Write methods (big-endian)
    void writeByte(int8_t v) { data.push_back(static_cast<uint8_t>(v)); }
    void writeUByte(uint8_t v) { data.push_back(v); }

    void writeShort(int16_t v) {
        uint16_t u = static_cast<uint16_t>(v);
        data.push_back((u >> 8) & 0xFF);
        data.push_back(u & 0xFF);
    }

    void writeInt(int32_t v) {
        uint32_t u = static_cast<uint32_t>(v);
        data.push_back((u >> 24) & 0xFF);
        data.push_back((u >> 16) & 0xFF);
        data.push_back((u >> 8) & 0xFF);
        data.push_back(u & 0xFF);
    }

    void writeLong(int64_t v) {
        uint64_t u = static_cast<uint64_t>(v);
        for (int i = 56; i >= 0; i -= 8) {
            data.push_back((u >> i) & 0xFF);
        }
    }

    void writeFloat(float v) {
        uint32_t u;
        std::memcpy(&u, &v, sizeof(u));
        writeInt(static_cast<int32_t>(u));
    }

    void writeDouble(double v) {
        uint64_t u;
        std::memcpy(&u, &v, sizeof(u));
        writeLong(static_cast<int64_t>(u));
    }

    // Java's DataOutputStream.writeUTF: 2-byte length prefix + modified UTF-8
    void writeUTF(const std::string& s) {
        // Simplified: assume ASCII for alpha protocol
        if (s.size() > 65535) throw std::runtime_error("String too long for writeUTF");
        writeShort(static_cast<int16_t>(s.size()));
        for (char c : s) {
            data.push_back(static_cast<uint8_t>(c));
        }
    }

    void writeBytes(const uint8_t* buf, size_t len) {
        data.insert(data.end(), buf, buf + len);
    }

    void writeBytes(const std::vector<uint8_t>& buf) {
        data.insert(data.end(), buf.begin(), buf.end());
    }

    // Read methods (big-endian) — virtual so SocketReader can override
    void ensureReadable(size_t n) const {
        if (readPos + n > data.size()) throw std::runtime_error("Buffer underflow");
    }

    virtual ~ByteBuffer() = default;

    virtual int8_t readByte() {
        ensureReadable(1);
        return static_cast<int8_t>(data[readPos++]);
    }

    virtual uint8_t readUByte() {
        ensureReadable(1);
        return data[readPos++];
    }

    virtual int16_t readShort() {
        ensureReadable(2);
        uint16_t v = (static_cast<uint16_t>(data[readPos]) << 8) |
                      static_cast<uint16_t>(data[readPos + 1]);
        readPos += 2;
        return static_cast<int16_t>(v);
    }

    virtual int32_t readInt() {
        ensureReadable(4);
        uint32_t v = (static_cast<uint32_t>(data[readPos]) << 24) |
                     (static_cast<uint32_t>(data[readPos + 1]) << 16) |
                     (static_cast<uint32_t>(data[readPos + 2]) << 8) |
                      static_cast<uint32_t>(data[readPos + 3]);
        readPos += 4;
        return static_cast<int32_t>(v);
    }

    virtual int64_t readLong() {
        ensureReadable(8);
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | static_cast<uint64_t>(data[readPos++]);
        }
        return static_cast<int64_t>(v);
    }

    virtual float readFloat() {
        int32_t i = readInt();
        uint32_t u = static_cast<uint32_t>(i);
        float f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    }

    virtual double readDouble() {
        int64_t i = readLong();
        uint64_t u = static_cast<uint64_t>(i);
        double d;
        std::memcpy(&d, &u, sizeof(d));
        return d;
    }

    virtual std::string readUTF() {
        int16_t len = readShort();
        if (len < 0) throw std::runtime_error("Negative UTF length");
        ensureReadable(static_cast<size_t>(len));
        std::string s(reinterpret_cast<const char*>(&data[readPos]), static_cast<size_t>(len));
        readPos += static_cast<size_t>(len);
        return s;
    }

    virtual void readBytes(uint8_t* buf, size_t len) {
        ensureReadable(len);
        std::memcpy(buf, &data[readPos], len);
        readPos += len;
    }

    virtual std::vector<uint8_t> readBytes(size_t len) {
        ensureReadable(len);
        std::vector<uint8_t> result(data.begin() + readPos, data.begin() + readPos + len);
        readPos += len;
        return result;
    }

    virtual bool readBool() {
        return readByte() != 0;
    }

    void writeBool(bool v) {
        writeByte(v ? 1 : 0);
    }

    size_t remaining() const { return data.size() - readPos; }
};

// Base packet class
class Packet {
public:
    bool isChunkDataPacket = false;

    virtual ~Packet() = default;
    virtual void readPacketData(ByteBuffer& buf) = 0;
    virtual void writePacketData(ByteBuffer& buf) = 0;
    virtual void processPacket(NetHandler& handler) = 0;
    virtual int getPacketSize() = 0;
    virtual std::unique_ptr<Packet> clone() const = 0;

    int getPacketId() const;

    static void registerPackets();
    static std::unique_ptr<Packet> createPacket(int id);

    // Read a full packet from a socket fd (blocking)
    // Returns nullptr on disconnect
    static std::unique_ptr<Packet> readPacket(int fd);

    // Write a packet to a socket fd
    static void writePacket(Packet& pkt, int fd);

    // Registry
    static std::unordered_map<int, std::function<std::unique_ptr<Packet>()>> idToFactory;
    static std::unordered_map<std::string, int> classToId;

    template<typename T>
    static void addMapping(int id) {
        idToFactory[id] = []() -> std::unique_ptr<Packet> { return std::make_unique<T>(); };
        classToId[typeid(T).name()] = id;
    }
};
