#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include "../network/Packet.h" // For ByteBuffer
#include "../../rust/alpha_bridge/alpha_bridge.h"

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
    // writeContents: writes only the payload (no type byte, no name) — used inside NBTList
    virtual void writeContents(ByteBuffer& buf) const = 0;
    // write: writes full tag payload (used inside NBTCompound entries, called after type+name are written)
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

class NBTList : public NBTTag {
public:
    std::vector<std::shared_ptr<NBTTag>> tags;
    NBTTagType tagType = NBTTagType::TAG_End;
    
    NBTTagType getType() const override { return NBTTagType::TAG_List; }
    
    // Java NBTTagList.writeTagContents: tagType + count + each element's contents only (no type/name)
    void writeContents(ByteBuffer& buf) const override {
        NBTTagType actualType = tags.empty() ? NBTTagType::TAG_Byte : tagType;
        buf.writeUByte(static_cast<uint8_t>(actualType));
        buf.writeInt(static_cast<int32_t>(tags.size()));
        for (const auto& tag : tags) {
            tag->writeContents(buf);
        }
    }
};

// Forward declarations of helper conversion functions
class NBTCompound;
inline NbtCompound* cpp_to_rust_compound(const NBTCompound& comp);
inline NbtList* cpp_to_rust_list(const NBTList& list);
inline std::shared_ptr<NBTCompound> rust_to_cpp_compound(const NbtCompound* r_comp);
inline std::shared_ptr<NBTList> rust_to_cpp_list(const NbtList* r_list);

class NBTCompound : public NBTTag {
public:
    std::map<std::string, std::shared_ptr<NBTTag>> tags;
    NBTTagType getType() const override { return NBTTagType::TAG_Compound; }

    void setByte(const std::string& name, int8_t v) { tags[name] = std::make_shared<NBTByte>(v); }
    void setShort(const std::string& name, int16_t v) { tags[name] = std::make_shared<NBTShort>(v); }
    void setInt(const std::string& name, int32_t v) { tags[name] = std::make_shared<NBTInt>(v); }
    void setLong(const std::string& name, int64_t v) { tags[name] = std::make_shared<NBTLong>(v); }
    void setFloat(const std::string& name, float v) { tags[name] = std::make_shared<NBTFloat>(v); }
    void setDouble(const std::string& name, double v) { tags[name] = std::make_shared<NBTDouble>(v); }
    void setString(const std::string& name, std::string v) { tags[name] = std::make_shared<NBTString>(std::move(v)); }
    void setByteArray(const std::string& name, std::vector<uint8_t> v) { tags[name] = std::make_shared<NBTByteArray>(std::move(v)); }
    void setCompound(const std::string& name, std::shared_ptr<NBTCompound> v) { tags[name] = v; }
    void setList(const std::string& name, std::shared_ptr<NBTList> v) { tags[name] = v; }
    void setBoolean(const std::string& name, bool v) { tags[name] = std::make_shared<NBTByte>(v ? 1 : 0); }

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

    float getFloat(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTFloat>(it->second);
            if (t) return t->value;
        }
        return 0.0f;
    }

    double getDouble(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTDouble>(it->second);
            if (t) return t->value;
        }
        return 0.0;
    }

    std::vector<uint8_t> getByteArray(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTByteArray>(it->second);
            if (t) return t->value;
        }
        return {};
    }

    std::string getString(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            auto t = std::dynamic_pointer_cast<NBTString>(it->second);
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

    std::shared_ptr<NBTList> getList(const std::string& name) const {
        auto it = tags.find(name);
        if (it != tags.end()) {
            return std::dynamic_pointer_cast<NBTList>(it->second);
        }
        return nullptr;
    }

    void writeContents(ByteBuffer& buf) const override {
        NbtCompound* r_comp = cpp_to_rust_compound(*this);
        AlphaBuffer serialized = alpha_nbt_compound_serialize_root(r_comp, "");
        if (serialized.data && serialized.len >= 3) {
            buf.writeBytes(serialized.data + 3, serialized.len - 3);
        }
        if (serialized.data) {
            alpha_buffer_free(serialized);
        }
        alpha_nbt_compound_free(r_comp);
    }

    void write(ByteBuffer& buf) const { writeContents(buf); }

    void writeRoot(ByteBuffer& buf, const std::string& rootName) const {
        NbtCompound* r_comp = cpp_to_rust_compound(*this);
        AlphaBuffer serialized = alpha_nbt_compound_serialize_root(r_comp, rootName.c_str());
        if (serialized.data) {
            buf.writeBytes(serialized.data, serialized.len);
            alpha_buffer_free(serialized);
        }
        alpha_nbt_compound_free(r_comp);
    }

    static std::shared_ptr<NBTTag> readTag(ByteBuffer& buf, NBTTagType type);

    void read(ByteBuffer& buf) {
        size_t remaining = buf.remaining();
        if (remaining == 0) return;

        std::vector<uint8_t> temp_buf;
        temp_buf.reserve(remaining + 3);
        temp_buf.push_back(10);
        temp_buf.push_back(0);
        temp_buf.push_back(0);
        temp_buf.insert(temp_buf.end(), buf.data.begin() + buf.readPos, buf.data.end());

        char* out_name = nullptr;
        size_t bytes_read = 0;
        NbtCompound* r_comp = alpha_nbt_compound_deserialize_root(temp_buf.data(), temp_buf.size(), &out_name, &bytes_read);
        if (r_comp) {
            auto cpp_comp = rust_to_cpp_compound(r_comp);
            this->tags = std::move(cpp_comp->tags);
            alpha_nbt_compound_free(r_comp);
            if (out_name) alpha_nbt_free_name(out_name);

            if (bytes_read >= 3) {
                buf.readPos += (bytes_read - 3);
            }
        } else {
            throw std::runtime_error("NBT read compound failed in Rust");
        }
    }

    static std::shared_ptr<NBTCompound> readRoot(ByteBuffer& buf) {
        size_t remaining = buf.remaining();
        if (remaining == 0) return nullptr;

        char* out_name = nullptr;
        size_t bytes_read = 0;
        NbtCompound* r_comp = alpha_nbt_compound_deserialize_root(buf.data.data() + buf.readPos, remaining, &out_name, &bytes_read);
        if (!r_comp) return nullptr;

        auto comp = rust_to_cpp_compound(r_comp);
        alpha_nbt_compound_free(r_comp);
        if (out_name) alpha_nbt_free_name(out_name);

        buf.readPos += bytes_read;
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
    } else if (type == NBTTagType::TAG_Float) {
        return std::make_shared<NBTFloat>(buf.readFloat());
    } else if (type == NBTTagType::TAG_Double) {
        return std::make_shared<NBTDouble>(buf.readDouble());
    } else if (type == NBTTagType::TAG_ByteArray) {
        int32_t len = buf.readInt();
        if (len < 0) throw std::runtime_error("NBT readTag: negative byte array length");
        std::vector<uint8_t> data(static_cast<size_t>(len));
        buf.readBytes(data.data(), static_cast<size_t>(len));
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
            list->tags.push_back(readTag(buf, list->tagType));
        }
        return list;
    } else if (type == NBTTagType::TAG_Compound) {
        auto comp = std::make_shared<NBTCompound>();
        comp->read(buf);
        return comp;
    }
    throw std::runtime_error("NBT readTag: unknown tag type " + std::to_string(static_cast<int>(type)));
}

// Inline Conversion Helpers Implementation

inline void cpp_to_rust_tag_to_compound(NbtCompound* r_comp, const std::string& name, const std::shared_ptr<NBTTag>& tag) {
    if (!tag) return;
    auto type = tag->getType();
    if (type == NBTTagType::TAG_Byte) {
        alpha_nbt_compound_set_byte(r_comp, name.c_str(), std::dynamic_pointer_cast<NBTByte>(tag)->value);
    } else if (type == NBTTagType::TAG_Short) {
        alpha_nbt_compound_set_short(r_comp, name.c_str(), std::dynamic_pointer_cast<NBTShort>(tag)->value);
    } else if (type == NBTTagType::TAG_Int) {
        alpha_nbt_compound_set_int(r_comp, name.c_str(), std::dynamic_pointer_cast<NBTInt>(tag)->value);
    } else if (type == NBTTagType::TAG_Long) {
        alpha_nbt_compound_set_long(r_comp, name.c_str(), std::dynamic_pointer_cast<NBTLong>(tag)->value);
    } else if (type == NBTTagType::TAG_Float) {
        alpha_nbt_compound_set_float(r_comp, name.c_str(), std::dynamic_pointer_cast<NBTFloat>(tag)->value);
    } else if (type == NBTTagType::TAG_Double) {
        alpha_nbt_compound_set_double(r_comp, name.c_str(), std::dynamic_pointer_cast<NBTDouble>(tag)->value);
    } else if (type == NBTTagType::TAG_String) {
        alpha_nbt_compound_set_string(r_comp, name.c_str(), std::dynamic_pointer_cast<NBTString>(tag)->value.c_str());
    } else if (type == NBTTagType::TAG_ByteArray) {
        const auto& vec = std::dynamic_pointer_cast<NBTByteArray>(tag)->value;
        alpha_nbt_compound_set_byte_array(r_comp, name.c_str(), vec.data(), vec.size());
    } else if (type == NBTTagType::TAG_Compound) {
        alpha_nbt_compound_set_compound(r_comp, name.c_str(), cpp_to_rust_compound(*std::dynamic_pointer_cast<NBTCompound>(tag)));
    } else if (type == NBTTagType::TAG_List) {
        alpha_nbt_compound_set_list(r_comp, name.c_str(), cpp_to_rust_list(*std::dynamic_pointer_cast<NBTList>(tag)));
    }
}

inline void cpp_to_rust_tag_to_list(NbtList* r_list, const std::shared_ptr<NBTTag>& tag) {
    if (!tag) return;
    auto type = tag->getType();
    if (type == NBTTagType::TAG_Byte) {
        alpha_nbt_list_add_byte(r_list, std::dynamic_pointer_cast<NBTByte>(tag)->value);
    } else if (type == NBTTagType::TAG_Short) {
        alpha_nbt_list_add_short(r_list, std::dynamic_pointer_cast<NBTShort>(tag)->value);
    } else if (type == NBTTagType::TAG_Int) {
        alpha_nbt_list_add_int(r_list, std::dynamic_pointer_cast<NBTInt>(tag)->value);
    } else if (type == NBTTagType::TAG_Long) {
        alpha_nbt_list_add_long(r_list, std::dynamic_pointer_cast<NBTLong>(tag)->value);
    } else if (type == NBTTagType::TAG_Float) {
        alpha_nbt_list_add_float(r_list, std::dynamic_pointer_cast<NBTFloat>(tag)->value);
    } else if (type == NBTTagType::TAG_Double) {
        alpha_nbt_list_add_double(r_list, std::dynamic_pointer_cast<NBTDouble>(tag)->value);
    } else if (type == NBTTagType::TAG_String) {
        alpha_nbt_list_add_string(r_list, std::dynamic_pointer_cast<NBTString>(tag)->value.c_str());
    } else if (type == NBTTagType::TAG_ByteArray) {
        const auto& vec = std::dynamic_pointer_cast<NBTByteArray>(tag)->value;
        alpha_nbt_list_add_byte_array(r_list, vec.data(), vec.size());
    } else if (type == NBTTagType::TAG_Compound) {
        alpha_nbt_list_add_compound(r_list, cpp_to_rust_compound(*std::dynamic_pointer_cast<NBTCompound>(tag)));
    } else if (type == NBTTagType::TAG_List) {
        alpha_nbt_list_add_list(r_list, cpp_to_rust_list(*std::dynamic_pointer_cast<NBTList>(tag)));
    }
}

inline NbtCompound* cpp_to_rust_compound(const NBTCompound& comp) {
    NbtCompound* r_comp = alpha_nbt_compound_create();
    for (const auto& [name, tag] : comp.tags) {
        cpp_to_rust_tag_to_compound(r_comp, name, tag);
    }
    return r_comp;
}

inline NbtList* cpp_to_rust_list(const NBTList& list) {
    NbtList* r_list = alpha_nbt_list_create(static_cast<uint8_t>(list.tagType));
    for (const auto& tag : list.tags) {
        cpp_to_rust_tag_to_list(r_list, tag);
    }
    return r_list;
}

inline std::shared_ptr<NBTTag> rust_to_cpp_tag(uint8_t type, const void* container_ptr, const std::string& name_or_idx, bool is_list, size_t idx = 0) {
    const NbtCompound* r_comp = is_list ? nullptr : reinterpret_cast<const NbtCompound*>(container_ptr);
    const NbtList* r_list = is_list ? reinterpret_cast<const NbtList*>(container_ptr) : nullptr;

    auto get_byte = [&]() -> int8_t {
        return is_list ? alpha_nbt_list_get_byte(r_list, idx) : alpha_nbt_compound_get_byte(r_comp, name_or_idx.c_str());
    };
    auto get_short = [&]() -> int16_t {
        return is_list ? alpha_nbt_list_get_short(r_list, idx) : alpha_nbt_compound_get_short(r_comp, name_or_idx.c_str());
    };
    auto get_int = [&]() -> int32_t {
        return is_list ? alpha_nbt_list_get_int(r_list, idx) : alpha_nbt_compound_get_int(r_comp, name_or_idx.c_str());
    };
    auto get_long = [&]() -> int64_t {
        return is_list ? alpha_nbt_list_get_long(r_list, idx) : alpha_nbt_compound_get_long(r_comp, name_or_idx.c_str());
    };
    auto get_float = [&]() -> float {
        return is_list ? alpha_nbt_list_get_float(r_list, idx) : alpha_nbt_compound_get_float(r_comp, name_or_idx.c_str());
    };
    auto get_double = [&]() -> double {
        return is_list ? alpha_nbt_list_get_double(r_list, idx) : alpha_nbt_compound_get_double(r_comp, name_or_idx.c_str());
    };
    auto get_string = [&]() -> std::string {
        AlphaBuffer buf = is_list ? alpha_nbt_list_get_string(r_list, idx) : alpha_nbt_compound_get_string(r_comp, name_or_idx.c_str());
        std::string s;
        if (buf.data) {
            s = reinterpret_cast<const char*>(buf.data);
            alpha_buffer_free(buf);
        }
        return s;
    };
    auto get_byte_array = [&]() -> std::vector<uint8_t> {
        AlphaBuffer buf = is_list ? alpha_nbt_list_get_byte_array(r_list, idx) : alpha_nbt_compound_get_byte_array(r_comp, name_or_idx.c_str());
        std::vector<uint8_t> v;
        if (buf.data) {
            v.assign(buf.data, buf.data + buf.len);
            alpha_buffer_free(buf);
        }
        return v;
    };
    auto get_compound = [&]() -> NbtCompound* {
        return is_list ? alpha_nbt_list_get_compound(r_list, idx) : alpha_nbt_compound_get_compound(r_comp, name_or_idx.c_str());
    };
    auto get_list = [&]() -> NbtList* {
        return is_list ? alpha_nbt_list_get_list(r_list, idx) : alpha_nbt_compound_get_list(r_comp, name_or_idx.c_str());
    };

    if (type == 1) return std::make_shared<NBTByte>(get_byte());
    if (type == 2) return std::make_shared<NBTShort>(get_short());
    if (type == 3) return std::make_shared<NBTInt>(get_int());
    if (type == 4) return std::make_shared<NBTLong>(get_long());
    if (type == 5) return std::make_shared<NBTFloat>(get_float());
    if (type == 6) return std::make_shared<NBTDouble>(get_double());
    if (type == 7) return std::make_shared<NBTByteArray>(get_byte_array());
    if (type == 8) return std::make_shared<NBTString>(get_string());
    if (type == 9) {
        NbtList* child_list = get_list();
        auto cpp_list = rust_to_cpp_list(child_list);
        alpha_nbt_list_free(child_list);
        return cpp_list;
    }
    if (type == 10) {
        NbtCompound* child_comp = get_compound();
        auto cpp_comp = rust_to_cpp_compound(child_comp);
        alpha_nbt_compound_free(child_comp);
        return cpp_comp;
    }
    return nullptr;
}

inline std::shared_ptr<NBTCompound> rust_to_cpp_compound(const NbtCompound* r_comp) {
    auto cpp_comp = std::make_shared<NBTCompound>();
    if (!r_comp) return cpp_comp;

    AlphaBuffer keys_buf = alpha_nbt_compound_get_keys(r_comp);
    if (keys_buf.data) {
        const char* p = reinterpret_cast<const char*>(keys_buf.data);
        const char* end = p + keys_buf.len;
        while (p < end && *p != '\0') {
            std::string key(p);
            p += key.size() + 1;
            uint8_t type = alpha_nbt_compound_get_tag_type(r_comp, key.c_str());
            auto tag = rust_to_cpp_tag(type, r_comp, key, false);
            if (tag) {
                cpp_comp->tags[key] = tag;
            }
        }
        alpha_buffer_free(keys_buf);
    }
    return cpp_comp;
}

inline std::shared_ptr<NBTList> rust_to_cpp_list(const NbtList* r_list) {
    auto cpp_list = std::make_shared<NBTList>();
    if (!r_list) return cpp_list;

    cpp_list->tagType = static_cast<NBTTagType>(alpha_nbt_list_get_type(r_list));
    size_t size = alpha_nbt_list_get_size(r_list);
    for (size_t i = 0; i < size; ++i) {
        auto tag = rust_to_cpp_tag(static_cast<uint8_t>(cpp_list->tagType), r_list, "", true, i);
        if (tag) {
            cpp_list->tags.push_back(tag);
        }
    }
    return cpp_list;
}
