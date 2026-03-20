#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "../network/Packet.h" // For ByteBuffer

enum class NBTTagType : uint8_t {
    TAG_End = 0,
    TAG_Byte = 1,
    TAG_Short = 2,
    TAG_Int = 3,
    TAG_Long = 4,
    TAG_Float = 5,
    TAG_Double = 6,
    TAG_ByteArray = 7,
    TAG_String = 8,
    TAG_List = 9,
    TAG_Compound = 10
};

class NBTTag {
public:
    virtual ~NBTTag() = default;
    virtual NBTTagType getType() const = 0;
    virtual void write(ByteBuffer& buf) const = 0;
};

class NBTByte : public NBTTag {
public:
    int8_t value;
    explicit NBTByte(int8_t v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Byte; }
    void write(ByteBuffer& buf) const override { buf.writeByte(value); }
};

class NBTShort : public NBTTag {
public:
    int16_t value;
    explicit NBTShort(int16_t v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Short; }
    void write(ByteBuffer& buf) const override { buf.writeShort(value); }
};

class NBTInt : public NBTTag {
public:
    int32_t value;
    explicit NBTInt(int32_t v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Int; }
    void write(ByteBuffer& buf) const override { buf.writeInt(value); }
};

class NBTLong : public NBTTag {
public:
    int64_t value;
    explicit NBTLong(int64_t v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Long; }
    void write(ByteBuffer& buf) const override { buf.writeLong(value); }
};

class NBTString : public NBTTag {
public:
    std::string value;
    explicit NBTString(std::string v) : value(std::move(v)) {}
    NBTTagType getType() const override { return NBTTagType::TAG_String; }
    void write(ByteBuffer& buf) const override {
        buf.writeShort(static_cast<int16_t>(value.size()));
        for (char c : value) buf.writeUByte(static_cast<uint8_t>(c));
    }
};

class NBTByteArray : public NBTTag {
public:
    std::vector<uint8_t> value;
    explicit NBTByteArray(std::vector<uint8_t> v) : value(std::move(v)) {}
    NBTTagType getType() const override { return NBTTagType::TAG_ByteArray; }
    void write(ByteBuffer& buf) const override {
        buf.writeInt(static_cast<int32_t>(value.size()));
        buf.writeBytes(value);
    }
};

class NBTCompound : public NBTTag {
public:
    std::map<std::string, std::shared_ptr<NBTTag>> tags;
    NBTTagType getType() const override { return NBTTagType::TAG_Compound; }

    void setByte(const std::string& name, int8_t v) { tags[name] = std::make_shared<NBTByte>(v); }
    void setShort(const std::string& name, int16_t v) { tags[name] = std::make_shared<NBTShort>(v); }
    void setInt(const std::string& name, int32_t v) { tags[name] = std::make_shared<NBTInt>(v); }
    void setLong(const std::string& name, int64_t v) { tags[name] = std::make_shared<NBTLong>(v); }
    void setString(const std::string& name, std::string v) { tags[name] = std::make_shared<NBTString>(std::move(v)); }
    void setByteArray(const std::string& name, std::vector<uint8_t> v) { tags[name] = std::make_shared<NBTByteArray>(std::move(v)); }
    void setCompound(const std::string& name, std::shared_ptr<NBTCompound> v) { tags[name] = v; }

    int8_t getByte(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTByte>(it->second);
            if (t) return t->value;
        }
        return 0;
    }

    int16_t getShort(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTShort>(it->second);
            if (t) return t->value;
        }
        return 0;
    }

    int32_t getInt(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTInt>(it->second);
            if (t) return t->value;
        }
        return 0;
    }

    int64_t getLong(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTLong>(it->second);
            if (t) return t->value;
        }
        return 0;
    }

    std::vector<uint8_t> getByteArray(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTByteArray>(it->second);
            if (t) return t->value;
        }
        return {};
    }

    std::shared_ptr<NBTCompound> getCompound(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            return std::dynamic_pointer_cast<NBTCompound>(it->second);
        }
        return nullptr;
    }

    void write(ByteBuffer& buf) const override {
        for (auto const& [name, tag] : tags) {
            buf.writeUByte(static_cast<uint8_t>(tag->getType()));
            buf.writeShort(static_cast<int16_t>(name.size()));
            for (char c : name) buf.writeUByte(static_cast<uint8_t>(c));
            tag->write(buf);
        }
        buf.writeUByte(static_cast<uint8_t>(NBTTagType::TAG_End));
    }

    void writeRoot(ByteBuffer& buf, const std::string& rootName) const {
        buf.writeUByte(static_cast<uint8_t>(NBTTagType::TAG_Compound));
        buf.writeShort(static_cast<int16_t>(rootName.size()));
        for (char c : rootName) buf.writeUByte(static_cast<uint8_t>(c));
        write(buf);
    }

    static std::shared_ptr<NBTTag> readTag(ByteBuffer& buf, NBTTagType type);

    void read(ByteBuffer& buf) {
        while (true) {
            uint8_t type = buf.readUByte();
            if (type == static_cast<uint8_t>(NBTTagType::TAG_End)) break;
            
            int16_t nameLen = buf.readShort();
            std::string name(nameLen, '\0');
            for (int i = 0; i < nameLen; ++i) name[i] = (char)buf.readUByte();
            
            tags[name] = readTag(buf, static_cast<NBTTagType>(type));
        }
    }

    static std::shared_ptr<NBTCompound> readRoot(ByteBuffer& buf) {
        uint8_t type = buf.readUByte();
        if (type != static_cast<uint8_t>(NBTTagType::TAG_Compound)) return nullptr;
        
        int16_t nameLen = buf.readShort();
        buf.readPos += nameLen; // Skip root name

        auto comp = std::make_shared<NBTCompound>();
        comp->read(buf);
        return comp;
    }
};

inline std::shared_ptr<NBTTag> NBTCompound::readTag(ByteBuffer& buf, NBTTagType type) {
    if (type == NBTTagType::TAG_Byte) {
        return std::make_shared<NBTByte>(buf.readByte());
    } else if (type == NBTTagType::TAG_Short) {
        return std::make_shared<NBTShort>(buf.readShort());
    } else if (type == NBTTagType::TAG_Int) {
        return std::make_shared<NBTInt>(buf.readInt());
    } else if (type == NBTTagType::TAG_Long) {
        return std::make_shared<NBTLong>(buf.readLong());
    } else if (type == NBTTagType::TAG_ByteArray) {
        int32_t len = buf.readInt();
        std::vector<uint8_t> data(len);
        buf.readBytes(data.data(), len);
        return std::make_shared<NBTByteArray>(std::move(data));
    } else if (type == NBTTagType::TAG_String) {
        int16_t len = buf.readShort();
        std::string s(len, '\0');
        for (int i = 0; i < len; ++i) s[i] = (char)buf.readUByte();
        return std::make_shared<NBTString>(std::move(s));
    } else if (type == NBTTagType::TAG_Compound) {
        auto comp = std::make_shared<NBTCompound>();
        comp->read(buf);
        return comp;
    }
    return nullptr;
}
