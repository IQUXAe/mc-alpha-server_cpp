#pragma once

#include "../Packet.h"
#include "../NetHandler.h"
#include "../../world/TileEntity.h"
#include "../../core/NBT.h"
#include <string>
#include <type_traits>

// ============= Packet 0: Keep Alive =============
class Packet0KeepAlive : public Packet {
public:
    void readPacketData(ByteBuffer& buf) override {}
    void writePacketData(ByteBuffer& buf) override {}
    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet0KeepAlive>(*this); }
    int getPacketSize() override { return 0; }
};

// ============= Packet 1: Login =============
class Packet1Login : public Packet {
public:
    int32_t protocolVersion = 0;
    std::string username;
    std::string password;
    int64_t mapSeed = 0;
    int8_t dimension = 0;

    Packet1Login() = default;
    Packet1Login(const std::string& user, const std::string& pass, int32_t entityId, int64_t seed, int8_t dim)
        : protocolVersion(entityId), username(user), password(pass), mapSeed(seed), dimension(dim) {}

    void readPacketData(ByteBuffer& buf) override {
        protocolVersion = buf.readInt();
        username = buf.readUTF();
        password = buf.readUTF();
        mapSeed = buf.readLong();
        dimension = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(protocolVersion);
        buf.writeUTF(username);
        buf.writeUTF(password);
        buf.writeLong(mapSeed);
        buf.writeByte(dimension);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleLogin(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet1Login>(*this); }
    int getPacketSize() override {
        return 4 + static_cast<int>(username.size()) + static_cast<int>(password.size()) + 4 + 5;
    }
};

// ============= Packet 2: Handshake =============
class Packet2Handshake : public Packet {
public:
    std::string username;

    Packet2Handshake() = default;
    explicit Packet2Handshake(const std::string& user) : username(user) {}

    void readPacketData(ByteBuffer& buf) override {
        username = buf.readUTF();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeUTF(username);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleHandshake(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet2Handshake>(*this); }
    int getPacketSize() override {
        return 4 + static_cast<int>(username.size()) + 4;
    }
};

// ============= Packet 3: Chat =============
class Packet3Chat : public Packet {
public:
    std::string message;

    Packet3Chat() = default;
    explicit Packet3Chat(const std::string& msg) : message(msg) {}

    void readPacketData(ByteBuffer& buf) override {
        message = buf.readUTF();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeUTF(message);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleChat(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet3Chat>(*this); }
    int getPacketSize() override {
        return static_cast<int>(message.size()) + 2;
    }
};

// ============= Packet 4: Update Time =============
class Packet4UpdateTime : public Packet {
public:
    int64_t time = 0;

    Packet4UpdateTime() = default;
    explicit Packet4UpdateTime(int64_t t) : time(t) {}

    void readPacketData(ByteBuffer& buf) override {
        time = buf.readLong();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeLong(time);
    }

    void processPacket(NetHandler& handler) override {}

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet4UpdateTime>(*this); }
    int getPacketSize() override { return 8; }
};

// ============= Packet 5: Player Inventory =============
class Packet5PlayerInventory : public Packet {
public:
    int32_t type = 0;
    int16_t itemCount = 0;
    struct SlotData {
        int16_t itemId = -1;
        int8_t count = 0;
        int16_t damage = 0;
    };
    std::vector<SlotData> slots;

    void readPacketData(ByteBuffer& buf) override {
        type = buf.readInt();
        itemCount = buf.readShort();
        slots.resize(itemCount);
        for (int i = 0; i < itemCount; ++i) {
            slots[i].itemId = buf.readShort();
            if (slots[i].itemId >= 0) {
                slots[i].count = buf.readByte();
                slots[i].damage = buf.readShort();
            }
        }
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(type);
        buf.writeShort(static_cast<int16_t>(slots.size()));
        for (auto& s : slots) {
            buf.writeShort(s.itemId);
            if (s.itemId >= 0) {
                buf.writeByte(s.count);
                buf.writeShort(s.damage);
            }
        }
    }

    void processPacket(NetHandler& handler) override {
        handler.handlePlayerInventory(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet5PlayerInventory>(*this); }
    int getPacketSize() override {
        int size = 6;
        for (auto& s : slots) {
            size += 2;
            if (s.itemId >= 0) size += 3;
        }
        return size;
    }
};

// ============= Packet 6: Spawn Position =============
class Packet6SpawnPosition : public Packet {
public:
    int32_t x = 0, y = 0, z = 0;

    Packet6SpawnPosition() = default;
    Packet6SpawnPosition(int32_t x, int32_t y, int32_t z) : x(x), y(y), z(z) {}

    void readPacketData(ByteBuffer& buf) override {
        x = buf.readInt(); y = buf.readInt(); z = buf.readInt();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(x); buf.writeInt(y); buf.writeInt(z);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet6SpawnPosition>(*this); }
    int getPacketSize() override { return 12; }
};

// ============= Packet 7: Use Entity =============
class Packet7UseEntity : public Packet {
public:
    int32_t playerEntityId = 0;
    int32_t targetEntityId = 0;
    bool isLeftClick = false;

    void readPacketData(ByteBuffer& buf) override {
        playerEntityId = buf.readInt();
        targetEntityId = buf.readInt();
        isLeftClick = buf.readBool();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(playerEntityId);
        buf.writeInt(targetEntityId);
        buf.writeBool(isLeftClick);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet7UseEntity>(*this); }
    int getPacketSize() override { return 9; }
};

// ============= Packet 8: Update Health =============
class Packet8UpdateHealth : public Packet {
public:
    int16_t health = 0;

    Packet8UpdateHealth() = default;
    explicit Packet8UpdateHealth(int16_t h) : health(h) {}

    void readPacketData(ByteBuffer& buf) override { health = buf.readShort(); }
    void writePacketData(ByteBuffer& buf) override { buf.writeShort(health); }
    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet8UpdateHealth>(*this); }
    int getPacketSize() override { return 2; }
};

// ============= Packet 9: Respawn =============
class Packet9Respawn : public Packet {
public:
    void readPacketData(ByteBuffer& buf) override {}
    void writePacketData(ByteBuffer& buf) override {}
    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet9Respawn>(*this); }
    int getPacketSize() override { return 0; }
};

// ============= Packet 10: Flying (on ground only) =============
class Packet10Flying : public Packet {
public:
    double x = 0.0, y = 0.0, z = 0.0, stance = 0.0;
    float yaw = 0.0f, pitch = 0.0f;
    bool onGround = false;
    bool moving = false;
    bool rotating = false;

    void readPacketData(ByteBuffer& buf) override {
        onGround = buf.readBool();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeBool(onGround);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleFlying(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet10Flying>(*this); }
    int getPacketSize() override { return 1; }
};

// ============= Packet 11: Player Position =============
class Packet11PlayerPosition : public Packet10Flying {
public:
    Packet11PlayerPosition() { moving = true; }

    void readPacketData(ByteBuffer& buf) override {
        x = buf.readDouble();
        y = buf.readDouble();
        stance = buf.readDouble();
        z = buf.readDouble();
        onGround = buf.readBool();
        moving = true;
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeDouble(x);
        buf.writeDouble(y);
        buf.writeDouble(stance);
        buf.writeDouble(z);
        buf.writeBool(onGround);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet11PlayerPosition>(*this); }
    int getPacketSize() override { return 33; }
};

// ============= Packet 12: Player Look =============
class Packet12PlayerLook : public Packet10Flying {
public:
    Packet12PlayerLook() { rotating = true; }

    void readPacketData(ByteBuffer& buf) override {
        yaw = buf.readFloat();
        pitch = buf.readFloat();
        onGround = buf.readBool();
        rotating = true;
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeFloat(yaw);
        buf.writeFloat(pitch);
        buf.writeBool(onGround);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet12PlayerLook>(*this); }
    int getPacketSize() override { return 9; }
};

// ============= Packet 13: Player Position & Look =============
class Packet13PlayerLookMove : public Packet10Flying {
public:
    Packet13PlayerLookMove() { moving = true; rotating = true; }

    Packet13PlayerLookMove(double x, double y, double stance, double z, float yaw, float pitch, bool onGround) {
        this->x = x;
        this->y = y;
        this->stance = stance;
        this->z = z;
        this->yaw = yaw;
        this->pitch = pitch;
        this->onGround = onGround;
        this->moving = true;
        this->rotating = true;
    }

    void readPacketData(ByteBuffer& buf) override {
        x = buf.readDouble();
        y = buf.readDouble();
        stance = buf.readDouble();
        z = buf.readDouble();
        yaw = buf.readFloat();
        pitch = buf.readFloat();
        onGround = buf.readBool();
        moving = true;
        rotating = true;
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeDouble(x);
        buf.writeDouble(y);
        buf.writeDouble(stance);
        buf.writeDouble(z);
        buf.writeFloat(yaw);
        buf.writeFloat(pitch);
        buf.writeBool(onGround);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet13PlayerLookMove>(*this); }
    int getPacketSize() override { return 41; }
};

// ============= Packet 14: Block Dig =============
class Packet14BlockDig : public Packet {
public:
    int32_t x = 0;
    int8_t y = 0;
    int32_t z = 0;
    int8_t face = 0;
    int8_t status = 0;

    void readPacketData(ByteBuffer& buf) override {
        status = buf.readByte();
        x = buf.readInt();
        y = buf.readByte();
        z = buf.readInt();
        face = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeByte(status);
        buf.writeInt(x);
        buf.writeByte(y);
        buf.writeInt(z);
        buf.writeByte(face);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleBlockDig(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet14BlockDig>(*this); }
    int getPacketSize() override { return 11; }
};

// ============= Packet 15: Place =============
class Packet15Place : public Packet {
public:
    int32_t x = 0;
    int8_t y = 0;
    int32_t z = 0;
    int8_t direction = 0;
    int16_t itemId = -1;
    int8_t amount = 0;
    int16_t damage = 0;

    void readPacketData(ByteBuffer& buf) override {
        itemId = buf.readShort();
        x = buf.readInt();
        y = buf.readByte();
        z = buf.readInt();
        direction = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeShort(itemId);
        buf.writeInt(x);
        buf.writeByte(y);
        buf.writeInt(z);
        buf.writeByte(direction);
    }

    void processPacket(NetHandler& handler) override {
        handler.handlePlace(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet15Place>(*this); }
    int getPacketSize() override { return 12; }
};

// ============= Packet 16: Block Item Switch =============
class Packet16BlockItemSwitch : public Packet {
public:
    int32_t entityId = 0;
    int16_t itemId = 0;

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        itemId = buf.readShort();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeShort(itemId);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleBlockItemSwitch(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet16BlockItemSwitch>(*this); }
    int getPacketSize() override { return 6; }
};

// ============= Packet 17: Add To Inventory =============
class Packet17AddToInventory : public Packet {
public:
    int16_t itemId = 0;
    int8_t count = 0;
    int16_t damage = 0;
    
    Packet17AddToInventory() = default;
    Packet17AddToInventory(int16_t id, int8_t cnt, int16_t dmg) : itemId(id), count(cnt), damage(dmg) {}

    void readPacketData(ByteBuffer& buf) override {
        itemId = buf.readShort();
        count = buf.readByte();
        damage = buf.readShort();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeShort(itemId);
        buf.writeByte(count);
        buf.writeShort(damage);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet17AddToInventory>(*this); }
    int getPacketSize() override { return 5; }
};

// ============= Packet 18: Arm Animation =============
class Packet18ArmAnimation : public Packet {
public:
    int32_t entityId = 0;
    int8_t animate = 0;

    Packet18ArmAnimation() = default;
    Packet18ArmAnimation(int32_t eid, int8_t anim) : entityId(eid), animate(anim) {}

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        animate = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeByte(animate);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleArmAnimation(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet18ArmAnimation>(*this); }
    int getPacketSize() override { return 5; }
};

// ============= Packet 20: Named Entity Spawn =============
class Packet20NamedEntitySpawn : public Packet {
public:
    int32_t entityId = 0;
    std::string name;
    int32_t x = 0, y = 0, z = 0;
    int8_t rotation = 0;
    int8_t pitch = 0;
    int16_t currentItem = 0;

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        name = buf.readUTF();
        x = buf.readInt();
        y = buf.readInt();
        z = buf.readInt();
        rotation = buf.readByte();
        pitch = buf.readByte();
        currentItem = buf.readShort();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeUTF(name);
        buf.writeInt(x);
        buf.writeInt(y);
        buf.writeInt(z);
        buf.writeByte(rotation);
        buf.writeByte(pitch);
        buf.writeShort(currentItem);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet20NamedEntitySpawn>(*this); }
    int getPacketSize() override { return 28 + static_cast<int>(name.size()); }
};

// ============= Packet 21: Pickup Spawn =============
class Packet21PickupSpawn : public Packet {
public:
    int32_t entityId = 0;
    int16_t itemId = 0;
    int8_t count = 0;
    int32_t x = 0, y = 0, z = 0;
    int8_t rotation = 0, pitch = 0, roll = 0;

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        itemId = buf.readShort();
        count = buf.readByte();
        x = buf.readInt(); y = buf.readInt(); z = buf.readInt();
        rotation = buf.readByte(); pitch = buf.readByte(); roll = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeShort(itemId);
        buf.writeByte(count);
        buf.writeInt(x); buf.writeInt(y); buf.writeInt(z);
        buf.writeByte(rotation); buf.writeByte(pitch); buf.writeByte(roll);
    }

    void processPacket(NetHandler& handler) override {
        handler.handlePickupSpawn(*this);
    }
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet21PickupSpawn>(*this); }
    int getPacketSize() override { return 22; }
};

// ============= Packet 22: Collect =============
class Packet22Collect : public Packet {
public:
    int32_t collectedEntityId = 0;
    int32_t collectorEntityId = 0;

    Packet22Collect() = default;
    Packet22Collect(int32_t collected, int32_t collector)
        : collectedEntityId(collected), collectorEntityId(collector) {}

    void readPacketData(ByteBuffer& buf) override {
        collectedEntityId = buf.readInt();
        collectorEntityId = buf.readInt();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(collectedEntityId);
        buf.writeInt(collectorEntityId);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet22Collect>(*this); }
    int getPacketSize() override { return 8; }
};

// ============= Packet 23: Vehicle Spawn =============
class Packet23VehicleSpawn : public Packet {
public:
    int32_t entityId = 0;
    int8_t type = 0;
    int32_t x = 0, y = 0, z = 0;

    Packet23VehicleSpawn() = default;
    Packet23VehicleSpawn(int32_t eid, int8_t t, int32_t x, int32_t y, int32_t z)
        : entityId(eid), type(t), x(x), y(y), z(z) {}

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        type = buf.readByte();
        x = buf.readInt(); y = buf.readInt(); z = buf.readInt();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeByte(type);
        buf.writeInt(x); buf.writeInt(y); buf.writeInt(z);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet23VehicleSpawn>(*this); }
    int getPacketSize() override { return 17; }
};

// ============= Packet 24: Mob Spawn =============
class Packet24MobSpawn : public Packet {
public:
    int32_t entityId = 0;
    int8_t type = 0;
    int32_t x = 0, y = 0, z = 0;
    int8_t yaw = 0, pitch = 0;

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        type = buf.readByte();
        x = buf.readInt(); y = buf.readInt(); z = buf.readInt();
        yaw = buf.readByte(); pitch = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeByte(type);
        buf.writeInt(x); buf.writeInt(y); buf.writeInt(z);
        buf.writeByte(yaw); buf.writeByte(pitch);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet24MobSpawn>(*this); }
    int getPacketSize() override { return 19; }
};

// ============= Packet 28: Entity Velocity =============
class Packet28EntityVelocity : public Packet {
public:
    int32_t entityId = 0;
    int16_t motionX = 0, motionY = 0, motionZ = 0;

    Packet28EntityVelocity() = default;
    Packet28EntityVelocity(int32_t eid, double mx, double my, double mz) : entityId(eid) {
        auto clamp = [](double v) -> int16_t {
            if (v < -3.9) v = -3.9;
            if (v > 3.9) v = 3.9;
            return static_cast<int16_t>(v * 8000.0);
        };
        motionX = clamp(mx);
        motionY = clamp(my);
        motionZ = clamp(mz);
    }

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        motionX = buf.readShort();
        motionY = buf.readShort();
        motionZ = buf.readShort();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeShort(motionX);
        buf.writeShort(motionY);
        buf.writeShort(motionZ);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet28EntityVelocity>(*this); }
    int getPacketSize() override { return 10; }
};

// ============= Packet 29: Destroy Entity =============
class Packet29DestroyEntity : public Packet {
public:
    int32_t entityId = 0;

    Packet29DestroyEntity() = default;
    explicit Packet29DestroyEntity(int32_t eid) : entityId(eid) {}

    void readPacketData(ByteBuffer& buf) override { entityId = buf.readInt(); }
    void writePacketData(ByteBuffer& buf) override { buf.writeInt(entityId); }
    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet29DestroyEntity>(*this); }
    int getPacketSize() override { return 4; }
};

// ============= Packet 30: Entity (no-op movement base) =============
class Packet30Entity : public Packet {
public:
    int32_t entityId = 0;

    Packet30Entity() = default;
    explicit Packet30Entity(int32_t eid) : entityId(eid) {}

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet30Entity>(*this); }
    int getPacketSize() override { return 4; }
};

// ============= Packet 31: Relative Entity Move =============
class Packet31RelEntityMove : public Packet30Entity {
public:
    int8_t dx = 0, dy = 0, dz = 0;

    Packet31RelEntityMove() = default;
    Packet31RelEntityMove(int32_t eid, int8_t dx, int8_t dy, int8_t dz)
        : dx(dx), dy(dy), dz(dz) { entityId = eid; }

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        dx = buf.readByte(); dy = buf.readByte(); dz = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeByte(dx); buf.writeByte(dy); buf.writeByte(dz);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet31RelEntityMove>(*this); }
    int getPacketSize() override { return 7; }
};

// ============= Packet 32: Entity Look =============
class Packet32EntityLook : public Packet30Entity {
public:
    int8_t yaw = 0, pitch = 0;

    Packet32EntityLook() = default;
    Packet32EntityLook(int32_t eid, int8_t yaw, int8_t pitch)
        : yaw(yaw), pitch(pitch) { entityId = eid; }

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        yaw = buf.readByte(); pitch = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeByte(yaw); buf.writeByte(pitch);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet32EntityLook>(*this); }
    int getPacketSize() override { return 6; }
};

// ============= Packet 33: Relative Entity Move + Look =============
class Packet33RelEntityMoveLook : public Packet30Entity {
public:
    int8_t dx = 0, dy = 0, dz = 0;
    int8_t yaw = 0, pitch = 0;

    Packet33RelEntityMoveLook() = default;
    Packet33RelEntityMoveLook(int32_t eid, int8_t dx, int8_t dy, int8_t dz, int8_t yaw, int8_t pitch)
        : dx(dx), dy(dy), dz(dz), yaw(yaw), pitch(pitch) { entityId = eid; }

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        dx = buf.readByte(); dy = buf.readByte(); dz = buf.readByte();
        yaw = buf.readByte(); pitch = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeByte(dx); buf.writeByte(dy); buf.writeByte(dz);
        buf.writeByte(yaw); buf.writeByte(pitch);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet33RelEntityMoveLook>(*this); }
    int getPacketSize() override { return 9; }
};

// ============= Packet 34: Entity Teleport =============
class Packet34EntityTeleport : public Packet {
public:
    int32_t entityId = 0;
    int32_t x = 0, y = 0, z = 0;
    int8_t yaw = 0, pitch = 0;

    Packet34EntityTeleport() = default;
    Packet34EntityTeleport(int32_t eid, int32_t x, int32_t y, int32_t z, int8_t yaw, int8_t pitch)
        : entityId(eid), x(x), y(y), z(z), yaw(yaw), pitch(pitch) {}

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        x = buf.readInt(); y = buf.readInt(); z = buf.readInt();
        yaw = buf.readByte(); pitch = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeInt(x); buf.writeInt(y); buf.writeInt(z);
        buf.writeByte(yaw); buf.writeByte(pitch);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet34EntityTeleport>(*this); }
    int getPacketSize() override { return 18; }
};

// ============= Packet 38: Entity Status =============
class Packet38EntityStatus : public Packet {
public:
    int32_t entityId = 0;
    int8_t status = 0;

    Packet38EntityStatus() = default;
    Packet38EntityStatus(int32_t eid, int8_t s) : entityId(eid), status(s) {}

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        status = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeByte(status);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet38EntityStatus>(*this); }
    int getPacketSize() override { return 5; }
};

// ============= Packet 39: Attach Entity =============
class Packet39AttachEntity : public Packet {
public:
    int32_t entityId = 0;
    int32_t vehicleEntityId = 0;

    void readPacketData(ByteBuffer& buf) override {
        entityId = buf.readInt();
        vehicleEntityId = buf.readInt();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(entityId);
        buf.writeInt(vehicleEntityId);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet39AttachEntity>(*this); }
    int getPacketSize() override { return 8; }
};

// ============= Packet 50: Pre-Chunk =============
class Packet50PreChunk : public Packet {
public:
    int32_t x = 0, z = 0;
    bool load = false;

    Packet50PreChunk() { isChunkDataPacket = true; }
    Packet50PreChunk(int32_t x, int32_t z, bool load) : x(x), z(z), load(load) { isChunkDataPacket = true; }

    void readPacketData(ByteBuffer& buf) override {
        x = buf.readInt(); z = buf.readInt();
        load = buf.readBool();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(x); buf.writeInt(z);
        buf.writeBool(load);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet50PreChunk>(*this); }
    int getPacketSize() override { return 9; }
};

// ============= Packet 51: Map Chunk =============
class Packet51MapChunk : public Packet {
public:
    int32_t x = 0;
    int16_t y = 0;
    int32_t z = 0;
    int32_t sizeX = 0, sizeY = 0, sizeZ = 0;
    std::vector<uint8_t> compressedData;

    Packet51MapChunk() { isChunkDataPacket = true; }

    Packet51MapChunk(int32_t xPos, int16_t yPos, int32_t zPos, int32_t sx, int32_t sy, int32_t sz, const std::vector<uint8_t>& data) {
        isChunkDataPacket = true;
        x = xPos;
        y = yPos;
        z = zPos;
        sizeX = sx;
        sizeY = sy;
        sizeZ = sz;
        compressedData = data;
    }

    void readPacketData(ByteBuffer& buf) override {
        x = buf.readInt();
        y = buf.readShort();
        z = buf.readInt();
        sizeX = buf.readByte() + 1;
        sizeY = buf.readByte() + 1;
        sizeZ = buf.readByte() + 1;
        int32_t compressedSize = buf.readInt();
        compressedData = buf.readBytes(static_cast<size_t>(compressedSize));
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(x);
        buf.writeShort(y);
        buf.writeInt(z);
        buf.writeByte(static_cast<int8_t>(sizeX - 1));
        buf.writeByte(static_cast<int8_t>(sizeY - 1));
        buf.writeByte(static_cast<int8_t>(sizeZ - 1));
        buf.writeInt(static_cast<int32_t>(compressedData.size()));
        buf.writeBytes(compressedData);
    }

    void processPacket(NetHandler& handler) override {}

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet51MapChunk>(*this); }
    int getPacketSize() override {
        return 17 + static_cast<int>(compressedData.size());
    }
};

// ============= Packet 52: Multi Block Change =============
class Packet52MultiBlockChange : public Packet {
public:
    int32_t chunkX = 0, chunkZ = 0;
    std::vector<int16_t> coordinateArray;
    std::vector<int8_t> typeArray;
    std::vector<int8_t> metadataArray;

    void readPacketData(ByteBuffer& buf) override {
        chunkX = buf.readInt();
        chunkZ = buf.readInt();
        int16_t size = buf.readShort();
        coordinateArray.resize(size);
        typeArray.resize(size);
        metadataArray.resize(size);
        for (int i = 0; i < size; ++i) coordinateArray[i] = buf.readShort();
        for (int i = 0; i < size; ++i) typeArray[i] = buf.readByte();
        for (int i = 0; i < size; ++i) metadataArray[i] = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(chunkX);
        buf.writeInt(chunkZ);
        buf.writeShort(static_cast<int16_t>(coordinateArray.size()));
        for (auto& c : coordinateArray) buf.writeShort(c);
        for (auto& t : typeArray) buf.writeByte(t);
        for (auto& m : metadataArray) buf.writeByte(m);
    }

    void processPacket(NetHandler& handler) override {}

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet52MultiBlockChange>(*this); }
    int getPacketSize() override {
        return 10 + static_cast<int>(coordinateArray.size()) * 4;
    }
};

// ============= Packet 53: Block Change =============
class Packet53BlockChange : public Packet {
public:
    int32_t x = 0;
    int8_t y = 0;
    int32_t z = 0;
    int8_t type = 0;
    int8_t metadata = 0;

    Packet53BlockChange() = default;
    Packet53BlockChange(int32_t x, int8_t y, int32_t z, int8_t type, int8_t meta)
        : x(x), y(y), z(z), type(type), metadata(meta) {}

    void readPacketData(ByteBuffer& buf) override {
        x = buf.readInt(); y = buf.readByte(); z = buf.readInt();
        type = buf.readByte(); metadata = buf.readByte();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(x); buf.writeByte(y); buf.writeInt(z);
        buf.writeByte(type); buf.writeByte(metadata);
    }

    void processPacket(NetHandler& handler) override {}
    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet53BlockChange>(*this); }
    int getPacketSize() override { return 11; }
};

// ============= Packet 59: Complex Entity (TileEntity) =============
class Packet59ComplexEntity : public Packet {
public:
    int32_t x = 0;
    int16_t y = 0;
    int32_t z = 0;
    std::vector<uint8_t> nbtData;

    Packet59ComplexEntity() {
        isChunkDataPacket = true;
    }

    Packet59ComplexEntity(int32_t xPos, int16_t yPos, int32_t zPos, TileEntity* tileEntity)
        : x(xPos), y(yPos), z(zPos) {
        isChunkDataPacket = true;
        if (tileEntity) {
            NBTCompound nbt;
            tileEntity->writeToNBT(nbt);
            ByteBuffer buf;
            nbt.writeRoot(buf, "");
            nbtData = buf.data;
        }
    }

    void readPacketData(ByteBuffer& buf) override {
        x = buf.readInt();
        y = buf.readShort();
        z = buf.readInt();
        int16_t len = buf.readShort();
        nbtData = buf.readBytes(static_cast<size_t>(len & 0xFFFF));
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(x);
        buf.writeShort(y);
        buf.writeInt(z);
        buf.writeShort(static_cast<int16_t>(nbtData.size()));
        buf.writeBytes(nbtData);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleComplexEntity(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet59ComplexEntity>(*this); }
    int getPacketSize() override {
        return 12 + static_cast<int>(nbtData.size());
    }
};

// ============= Packet 60: Explosion =============
class Packet60Explosion : public Packet {
public:
    double x = 0.0, y = 0.0, z = 0.0;
    float radius = 0.0f;
    struct BlockPos { int8_t dx, dy, dz; };
    std::vector<BlockPos> destroyedBlocks;

    void readPacketData(ByteBuffer& buf) override {
        x = buf.readDouble(); y = buf.readDouble(); z = buf.readDouble();
        radius = buf.readFloat();
        int32_t count = buf.readInt();
        destroyedBlocks.resize(count);
        for (int i = 0; i < count; ++i) {
            destroyedBlocks[i].dx = buf.readByte();
            destroyedBlocks[i].dy = buf.readByte();
            destroyedBlocks[i].dz = buf.readByte();
        }
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeDouble(x); buf.writeDouble(y); buf.writeDouble(z);
        buf.writeFloat(radius);
        buf.writeInt(static_cast<int32_t>(destroyedBlocks.size()));
        for (auto& b : destroyedBlocks) {
            buf.writeByte(b.dx); buf.writeByte(b.dy); buf.writeByte(b.dz);
        }
    }

    void processPacket(NetHandler& handler) override {}

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet60Explosion>(*this); }
    int getPacketSize() override {
        return 32 + static_cast<int>(destroyedBlocks.size()) * 3;
    }
};

// ============= Packet 255: Kick Disconnect =============
class Packet255KickDisconnect : public Packet {
public:
    std::string reason;

    Packet255KickDisconnect() = default;
    explicit Packet255KickDisconnect(const std::string& r) : reason(r) {}

    void readPacketData(ByteBuffer& buf) override {
        reason = buf.readUTF();
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeUTF(reason);
    }

    void processPacket(NetHandler& handler) override {
        handler.handleKickDisconnect(*this);
    }

    std::unique_ptr<Packet> clone() const override { return std::make_unique<Packet255KickDisconnect>(*this); }
    int getPacketSize() override {
        return static_cast<int>(reason.size()) + 2;
    }
};
