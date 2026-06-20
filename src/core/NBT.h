#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include "../network/Packet.h"

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
    virtual void writeContents(ByteBuffer& buf) const = 0;
    void write(ByteBuffer& buf) const { writeContents(buf); }
};

class NBTByte : public NBTTag {
public:
    int8_t value;
    explicit NBTByte(int8_t v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Byte; }
    void writeContents(ByteBuffer& buf) const override { buf.writeByte(value); }
};

class NBTShort : public NBTTag {
public:
    int16_t value;
    explicit NBTShort(int16_t v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Short; }
    void writeContents(ByteBuffer& buf) const override { buf.writeShort(value); }
};

class NBTInt : public NBTTag {
public:
    int32_t value;
    explicit NBTInt(int32_t v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Int; }
    void writeContents(ByteBuffer& buf) const override { buf.writeInt(value); }
};

class NBTLong : public NBTTag {
public:
    int64_t value;
    explicit NBTLong(int64_t v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Long; }
    void writeContents(ByteBuffer& buf) const override { buf.writeLong(value); }
};

class NBTFloat : public NBTTag {
public:
    float value;
    explicit NBTFloat(float v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Float; }
    void writeContents(ByteBuffer& buf) const override { buf.writeFloat(value); }
};

class NBTDouble : public NBTTag {
public:
    double value;
    explicit NBTDouble(double v) : value(v) {}
    NBTTagType getType() const override { return NBTTagType::TAG_Double; }
    void writeContents(ByteBuffer& buf) const override { buf.writeDouble(value); }
};

class NBTString : public NBTTag {
public:
    std::string value;
    explicit NBTString(std::string v) : value(std::move(v)) {}
    NBTTagType getType() const override { return NBTTagType::TAG_String; }
    void writeContents(ByteBuffer& buf) const override {
        buf.writeShort(static_cast<int16_t>(value.size()));
        for (char c : value) buf.writeUByte(static_cast<uint8_t>(c));
    }
};

class NBTByteArray : public NBTTag {
public:
    std::vector<uint8_t> value;
    explicit NBTByteArray(std::vector<uint8_t> v) : value(std::move(v)) {}
    NBTTagType getType() const override { return NBTTagType::TAG_ByteArray; }
    void writeContents(ByteBuffer& buf) const override {
        buf.writeInt(static_cast<int32_t>(value.size()));
        buf.writeBytes(value);
    }
};

class NBTCompound;
class NBTList;

class ProxyElement {
private:
    NBTCompound* parent_;
    std::string key_;
public:
    ProxyElement(NBTCompound* parent, std::string key) : parent_(parent), key_(std::move(key)) {}
    ProxyElement& operator=(const std::shared_ptr<NBTTag>& tag);
    operator std::shared_ptr<NBTTag>() const;
};

class NBTCompoundTagsProxy {
private:
    NBTCompound* parent_;
public:
    explicit NBTCompoundTagsProxy(NBTCompound* parent) : parent_(parent) {}
    void invalidateCache() const {} // No-op now
    ProxyElement operator[](const std::string& key) {
        return ProxyElement(parent_, key);
    }
    std::map<std::string, std::shared_ptr<NBTTag>>::iterator find(const std::string& key);
    std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator find(const std::string& key) const;
    std::map<std::string, std::shared_ptr<NBTTag>>::iterator end();
    std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator end() const;
    std::map<std::string, std::shared_ptr<NBTTag>>::iterator begin();
    std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator begin() const;
    bool empty() const;
    size_t size() const;
};

class NBTListTagsProxy {
private:
    NBTList* parent_;
public:
    explicit NBTListTagsProxy(NBTList* parent) : parent_(parent) {}
    void invalidateCache() const {}
    void push_back(const std::shared_ptr<NBTTag>& tag);
    size_t size() const;
    bool empty() const;
    std::shared_ptr<NBTTag> operator[](size_t idx) const;
    NBTListTagsProxy& operator=(const std::vector<std::shared_ptr<NBTTag>>& vec);
    std::vector<std::shared_ptr<NBTTag>>::iterator begin();
    std::vector<std::shared_ptr<NBTTag>>::const_iterator begin() const;
    std::vector<std::shared_ptr<NBTTag>>::iterator end();
    std::vector<std::shared_ptr<NBTTag>>::const_iterator end() const;
};

class NBTCompound : public NBTTag {
public:
    std::map<std::string, std::shared_ptr<NBTTag>> map;
    NBTCompoundTagsProxy tags;

    NBTCompound() : tags(this) {}
    ~NBTCompound() override = default;
    NBTCompound(const NBTCompound& other) : tags(this) {
        // Deep copy not strictly necessary if shared_ptr, but NBT conceptually owns
        for (auto& pair : other.map) map[pair.first] = pair.second;
    }
    NBTCompound& operator=(const NBTCompound& other) {
        if (this != &other) {
            map.clear();
            for (auto& pair : other.map) map[pair.first] = pair.second;
        }
        return *this;
    }
    NBTCompound(NBTCompound&& other) noexcept : tags(this), map(std::move(other.map)) {}
    NBTCompound& operator=(NBTCompound&& other) noexcept {
        if (this != &other) {
            map = std::move(other.map);
        }
        return *this;
    }

    NBTTagType getType() const override { return NBTTagType::TAG_Compound; }

    void setByte(const std::string& name, int8_t v) { map[name] = std::make_shared<NBTByte>(v); }
    void setShort(const std::string& name, int16_t v) { map[name] = std::make_shared<NBTShort>(v); }
    void setInt(const std::string& name, int32_t v) { map[name] = std::make_shared<NBTInt>(v); }
    void setLong(const std::string& name, int64_t v) { map[name] = std::make_shared<NBTLong>(v); }
    void setFloat(const std::string& name, float v) { map[name] = std::make_shared<NBTFloat>(v); }
    void setDouble(const std::string& name, double v) { map[name] = std::make_shared<NBTDouble>(v); }
    void setString(const std::string& name, std::string v) { map[name] = std::make_shared<NBTString>(v); }
    void setByteArray(const std::string& name, std::vector<uint8_t> v) { map[name] = std::make_shared<NBTByteArray>(v); }
    void setByteArray(const std::string& name, const uint8_t* val_ptr, size_t val_len) { map[name] = std::make_shared<NBTByteArray>(std::vector<uint8_t>(val_ptr, val_ptr + val_len)); }
    void setCompound(const std::string& name, std::shared_ptr<NBTCompound> v);
    void setList(const std::string& name, std::shared_ptr<NBTList> v);
    void setBoolean(const std::string& name, bool v) { map[name] = std::make_shared<NBTByte>(v ? 1 : 0); }

    int8_t getByte(const std::string& name) const { auto it = map.find(name); if (it != map.end() && it->second->getType() == NBTTagType::TAG_Byte) return std::dynamic_pointer_cast<NBTByte>(it->second)->value; return 0; }
    int16_t getShort(const std::string& name) const { auto it = map.find(name); if (it != map.end() && it->second->getType() == NBTTagType::TAG_Short) return std::dynamic_pointer_cast<NBTShort>(it->second)->value; return 0; }
    int32_t getInt(const std::string& name) const { auto it = map.find(name); if (it != map.end() && it->second->getType() == NBTTagType::TAG_Int) return std::dynamic_pointer_cast<NBTInt>(it->second)->value; return 0; }
    int64_t getLong(const std::string& name) const { auto it = map.find(name); if (it != map.end() && it->second->getType() == NBTTagType::TAG_Long) return std::dynamic_pointer_cast<NBTLong>(it->second)->value; return 0; }
    float getFloat(const std::string& name) const { auto it = map.find(name); if (it != map.end() && it->second->getType() == NBTTagType::TAG_Float) return std::dynamic_pointer_cast<NBTFloat>(it->second)->value; return 0; }
    double getDouble(const std::string& name) const { auto it = map.find(name); if (it != map.end() && it->second->getType() == NBTTagType::TAG_Double) return std::dynamic_pointer_cast<NBTDouble>(it->second)->value; return 0; }
    std::vector<uint8_t> getByteArray(const std::string& name) const { auto it = map.find(name); if (it != map.end() && it->second->getType() == NBTTagType::TAG_ByteArray) return std::dynamic_pointer_cast<NBTByteArray>(it->second)->value; return {}; }
    std::string getString(const std::string& name) const { auto it = map.find(name); if (it != map.end() && it->second->getType() == NBTTagType::TAG_String) return std::dynamic_pointer_cast<NBTString>(it->second)->value; return ""; }
    std::shared_ptr<NBTCompound> getCompound(const std::string& name) const;
    std::shared_ptr<NBTList> getList(const std::string& name) const;

    void writeContents(ByteBuffer& buf) const override {
        for (const auto& pair : map) {
            buf.writeUByte(static_cast<uint8_t>(pair.second->getType()));
            buf.writeShort(static_cast<int16_t>(pair.first.size()));
            for (char c : pair.first) buf.writeUByte(static_cast<uint8_t>(c));
            pair.second->writeContents(buf);
        }
        buf.writeUByte(static_cast<uint8_t>(NBTTagType::TAG_End));
    }

    void writeRoot(ByteBuffer& buf, const std::string& rootName) const {
        buf.writeUByte(static_cast<uint8_t>(NBTTagType::TAG_Compound));
        buf.writeShort(static_cast<int16_t>(rootName.size()));
        for (char c : rootName) buf.writeUByte(static_cast<uint8_t>(c));
        writeContents(buf);
    }

    static std::shared_ptr<NBTTag> readTag(ByteBuffer& buf, NBTTagType type);

    void read(ByteBuffer& buf) {
        map.clear();
        while (buf.remaining() > 0) {
            uint8_t type = buf.readUByte();
            if (type == 0) break; // TAG_End
            
            int16_t nameLen = buf.readShort();
            std::string name(static_cast<size_t>(nameLen), '\0');
            for (int i = 0; i < nameLen; ++i) name[i] = static_cast<char>(buf.readUByte());
            
            auto tag = readTag(buf, static_cast<NBTTagType>(type));
            if (tag) map[name] = tag;
        }
    }

    static std::shared_ptr<NBTCompound> readRoot(ByteBuffer& buf) {
        if (buf.remaining() == 0) return nullptr;
        uint8_t type = buf.readUByte();
        if (type != static_cast<uint8_t>(NBTTagType::TAG_Compound)) return nullptr;
        
        int16_t nameLen = buf.readShort();
        for (int i = 0; i < nameLen; ++i) buf.readUByte(); // skip name
        
        auto comp = std::make_shared<NBTCompound>();
        comp->read(buf);
        return comp;
    }
};

class NBTList : public NBTTag {
public:
    std::vector<std::shared_ptr<NBTTag>> elements;
    NBTListTagsProxy tags;
    NBTTagType tagType = NBTTagType::TAG_End;

    NBTList() : tags(this) {}
    ~NBTList() override = default;
    NBTList(const NBTList& other) : elements(other.elements), tags(this), tagType(other.tagType) {}
    NBTList& operator=(const NBTList& other) {
        if (this != &other) {
            elements = other.elements;
            tagType = other.tagType;
        }
        return *this;
    }
    NBTList(NBTList&& other) noexcept : elements(std::move(other.elements)), tags(this), tagType(other.tagType) {}
    NBTList& operator=(NBTList&& other) noexcept {
        if (this != &other) {
            elements = std::move(other.elements);
            tagType = other.tagType;
        }
        return *this;
    }

    NBTTagType getType() const override { return NBTTagType::TAG_List; }

    void writeContents(ByteBuffer& buf) const override {
        NBTTagType actualType = elements.empty() ? NBTTagType::TAG_Byte : tagType;
        buf.writeUByte(static_cast<uint8_t>(actualType));
        buf.writeInt(static_cast<int32_t>(elements.size()));
        for (const auto& tag : elements) {
            tag->writeContents(buf);
        }
    }
};

inline void NBTCompound::setCompound(const std::string& name, std::shared_ptr<NBTCompound> v) {
    if (v) map[name] = v;
    else map.erase(name);
}

inline void NBTCompound::setList(const std::string& name, std::shared_ptr<NBTList> v) {
    if (v) map[name] = v;
    else map.erase(name);
}

inline std::shared_ptr<NBTCompound> NBTCompound::getCompound(const std::string& name) const {
    auto it = map.find(name);
    if (it != map.end() && it->second->getType() == NBTTagType::TAG_Compound) {
        return std::dynamic_pointer_cast<NBTCompound>(it->second);
    }
    return nullptr;
}

inline std::shared_ptr<NBTList> NBTCompound::getList(const std::string& name) const {
    auto it = map.find(name);
    if (it != map.end() && it->second->getType() == NBTTagType::TAG_List) {
        return std::dynamic_pointer_cast<NBTList>(it->second);
    }
    return nullptr;
}

inline ProxyElement& ProxyElement::operator=(const std::shared_ptr<NBTTag>& tag) {
    if (!tag) {
        parent_->map.erase(key_);
    } else {
        parent_->map[key_] = tag;
    }
    return *this;
}

inline ProxyElement::operator std::shared_ptr<NBTTag>() const {
    auto it = parent_->map.find(key_);
    if (it != parent_->map.end()) return it->second;
    return nullptr;
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::iterator NBTCompoundTagsProxy::find(const std::string& key) {
    return parent_->map.find(key);
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator NBTCompoundTagsProxy::find(const std::string& key) const {
    return parent_->map.find(key);
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::iterator NBTCompoundTagsProxy::end() {
    return parent_->map.end();
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator NBTCompoundTagsProxy::end() const {
    return parent_->map.end();
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::iterator NBTCompoundTagsProxy::begin() {
    return parent_->map.begin();
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator NBTCompoundTagsProxy::begin() const {
    return parent_->map.begin();
}

inline bool NBTCompoundTagsProxy::empty() const {
    return parent_->map.empty();
}

inline size_t NBTCompoundTagsProxy::size() const {
    return parent_->map.size();
}

inline void NBTListTagsProxy::push_back(const std::shared_ptr<NBTTag>& tag) {
    if (!tag) return;
    if (parent_->elements.empty()) {
        parent_->tagType = tag->getType();
    }
    parent_->elements.push_back(tag);
}

inline size_t NBTListTagsProxy::size() const {
    return parent_->elements.size();
}

inline bool NBTListTagsProxy::empty() const {
    return parent_->elements.empty();
}

inline std::shared_ptr<NBTTag> NBTListTagsProxy::operator[](size_t idx) const {
    return parent_->elements[idx];
}

inline NBTListTagsProxy& NBTListTagsProxy::operator=(const std::vector<std::shared_ptr<NBTTag>>& vec) {
    parent_->elements = vec;
    if (!vec.empty()) {
        parent_->tagType = vec[0]->getType();
    }
    return *this;
}

inline std::vector<std::shared_ptr<NBTTag>>::iterator NBTListTagsProxy::begin() {
    return parent_->elements.begin();
}

inline std::vector<std::shared_ptr<NBTTag>>::const_iterator NBTListTagsProxy::begin() const {
    return parent_->elements.begin();
}

inline std::vector<std::shared_ptr<NBTTag>>::iterator NBTListTagsProxy::end() {
    return parent_->elements.end();
}

inline std::vector<std::shared_ptr<NBTTag>>::const_iterator NBTListTagsProxy::end() const {
    return parent_->elements.end();
}

inline std::shared_ptr<NBTTag> NBTCompound::readTag(ByteBuffer& buf, NBTTagType type) {
    if (type == NBTTagType::TAG_Byte) {
        return std::make_shared<NBTByte>(buf.readByte());
    } else if (type == NBTTagType::TAG_Short) {
        return std::make_shared<NBTShort>(buf.readShort());
    } else if (type == NBTTagType::TAG_Int) {
        return std::make_shared<NBTInt>(buf.readInt());
    } else if (type == NBTTagType::TAG_Long) {
        return std::make_shared<NBTLong>(buf.readLong());
    } else if (type == NBTTagType::TAG_Float) {
        return std::make_shared<NBTFloat>(buf.readFloat());
    } else if (type == NBTTagType::TAG_Double) {
        return std::make_shared<NBTDouble>(buf.readDouble());
    } else if (type == NBTTagType::TAG_ByteArray) {
        int32_t len = buf.readInt();
        if (len < 0) throw std::runtime_error("NBT readTag: negative byte array length");
        std::vector<uint8_t> data(static_cast<size_t>(len));
        if (len > 0) {
            buf.readBytes(data.data(), static_cast<size_t>(len));
        }
        return std::make_shared<NBTByteArray>(std::move(data));
    } else if (type == NBTTagType::TAG_String) {
        int16_t len = buf.readShort();
        if (len < 0) throw std::runtime_error("NBT readTag: negative string length");
        std::string s(static_cast<size_t>(len), '\0');
        for (int i = 0; i < len; ++i) s[i] = static_cast<char>(buf.readUByte());
        return std::make_shared<NBTString>(std::move(s));
    } else if (type == NBTTagType::TAG_List) {
        auto list = std::make_shared<NBTList>();
        list->tagType = static_cast<NBTTagType>(buf.readUByte());
        int32_t count = buf.readInt();
        if (count < 0) throw std::runtime_error("NBT readTag: negative list count");
        for (int32_t i = 0; i < count; ++i) {
            list->elements.push_back(readTag(buf, list->tagType));
        }
        return list;
    } else if (type == NBTTagType::TAG_Compound) {
        auto comp = std::make_shared<NBTCompound>();
        comp->read(buf);
        return comp;
    }
    throw std::runtime_error("NBT readTag: unknown tag type " + std::to_string(static_cast<int>(type)));
}
