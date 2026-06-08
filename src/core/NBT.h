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

// Forward declarations of core wrapper types and proxies
class NBTCompound;
class NBTList;
class NBTCompoundTagsProxy;
class ProxyElement;
class NBTListTagsProxy;

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
    mutable std::map<std::string, std::shared_ptr<NBTTag>> cache_;
    mutable bool cache_dirty_ = true;
    void populateCache() const;
public:
    explicit NBTCompoundTagsProxy(NBTCompound* parent) : parent_(parent) {}
    void invalidateCache() const { cache_dirty_ = true; }
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
    mutable std::vector<std::shared_ptr<NBTTag>> cache_;
    mutable bool cache_dirty_ = true;
    void populateCache() const;
public:
    explicit NBTListTagsProxy(NBTList* parent) : parent_(parent) {}
    void invalidateCache() const { cache_dirty_ = true; }
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
    NbtCompound* rustPtr_ = nullptr;
    NBTCompoundTagsProxy tags;

    NBTCompound() : rustPtr_(alpha_nbt_compound_create()), tags(this) {}
    explicit NBTCompound(NbtCompound* ptr) : rustPtr_(ptr ? ptr : alpha_nbt_compound_create()), tags(this) {}

    ~NBTCompound() override {
        if (rustPtr_) {
            alpha_nbt_compound_free(rustPtr_);
            rustPtr_ = nullptr;
        }
    }

    NBTCompound(const NBTCompound& other) : tags(this) {
        rustPtr_ = alpha_nbt_compound_clone(other.rustPtr_);
    }

    NBTCompound& operator=(const NBTCompound& other) {
        if (this != &other) {
            if (rustPtr_) {
                alpha_nbt_compound_free(rustPtr_);
            }
            rustPtr_ = alpha_nbt_compound_clone(other.rustPtr_);
            tags.invalidateCache();
        }
        return *this;
    }

    NBTCompound(NBTCompound&& other) noexcept : tags(this) {
        rustPtr_ = other.rustPtr_;
        other.rustPtr_ = nullptr;
    }

    NBTCompound& operator=(NBTCompound&& other) noexcept {
        if (this != &other) {
            if (rustPtr_) {
                alpha_nbt_compound_free(rustPtr_);
            }
            rustPtr_ = other.rustPtr_;
            other.rustPtr_ = nullptr;
            tags.invalidateCache();
        }
        return *this;
    }

    NBTTagType getType() const override { return NBTTagType::TAG_Compound; }

    void setByte(const std::string& name, int8_t v) { alpha_nbt_compound_set_byte(rustPtr_, name.c_str(), v); tags.invalidateCache(); }
    void setShort(const std::string& name, int16_t v) { alpha_nbt_compound_set_short(rustPtr_, name.c_str(), v); tags.invalidateCache(); }
    void setInt(const std::string& name, int32_t v) { alpha_nbt_compound_set_int(rustPtr_, name.c_str(), v); tags.invalidateCache(); }
    void setLong(const std::string& name, int64_t v) { alpha_nbt_compound_set_long(rustPtr_, name.c_str(), v); tags.invalidateCache(); }
    void setFloat(const std::string& name, float v) { alpha_nbt_compound_set_float(rustPtr_, name.c_str(), v); tags.invalidateCache(); }
    void setDouble(const std::string& name, double v) { alpha_nbt_compound_set_double(rustPtr_, name.c_str(), v); tags.invalidateCache(); }
    void setString(const std::string& name, std::string v) { alpha_nbt_compound_set_string(rustPtr_, name.c_str(), v.c_str()); tags.invalidateCache(); }
    void setByteArray(const std::string& name, std::vector<uint8_t> v) { alpha_nbt_compound_set_byte_array(rustPtr_, name.c_str(), v.data(), v.size()); tags.invalidateCache(); }
    void setByteArray(const std::string& name, const uint8_t* val_ptr, size_t val_len) { alpha_nbt_compound_set_byte_array(rustPtr_, name.c_str(), val_ptr, val_len); tags.invalidateCache(); }
    void setCompound(const std::string& name, std::shared_ptr<NBTCompound> v);
    void setList(const std::string& name, std::shared_ptr<NBTList> v);
    void setBoolean(const std::string& name, bool v) { alpha_nbt_compound_set_byte(rustPtr_, name.c_str(), v ? 1 : 0); tags.invalidateCache(); }

    int8_t getByte(const std::string& name) const { return alpha_nbt_compound_get_byte(rustPtr_, name.c_str()); }
    int16_t getShort(const std::string& name) const { return alpha_nbt_compound_get_short(rustPtr_, name.c_str()); }
    int32_t getInt(const std::string& name) const { return alpha_nbt_compound_get_int(rustPtr_, name.c_str()); }
    int64_t getLong(const std::string& name) const { return alpha_nbt_compound_get_long(rustPtr_, name.c_str()); }
    float getFloat(const std::string& name) const { return alpha_nbt_compound_get_float(rustPtr_, name.c_str()); }
    double getDouble(const std::string& name) const { return alpha_nbt_compound_get_double(rustPtr_, name.c_str()); }
    std::vector<uint8_t> getByteArray(const std::string& name) const {
        AlphaBuffer buf = alpha_nbt_compound_get_byte_array(rustPtr_, name.c_str());
        std::vector<uint8_t> v;
        if (buf.data) {
            v.assign(buf.data, buf.data + buf.len);
            alpha_buffer_free(buf);
        }
        return v;
    }
    std::string getString(const std::string& name) const {
        AlphaBuffer buf = alpha_nbt_compound_get_string(rustPtr_, name.c_str());
        std::string s;
        if (buf.data) {
            s = reinterpret_cast<const char*>(buf.data);
            alpha_buffer_free(buf);
        }
        return s;
    }
    std::shared_ptr<NBTCompound> getCompound(const std::string& name) const;
    std::shared_ptr<NBTList> getList(const std::string& name) const;

    void writeContents(ByteBuffer& buf) const override {
        AlphaBuffer serialized = alpha_nbt_compound_serialize_root(rustPtr_, "");
        if (serialized.data && serialized.len >= 3) {
            buf.writeBytes(serialized.data + 3, serialized.len - 3);
        }
        if (serialized.data) {
            alpha_buffer_free(serialized);
        }
    }

    void write(ByteBuffer& buf) const { writeContents(buf); }

    void writeRoot(ByteBuffer& buf, const std::string& rootName) const {
        AlphaBuffer serialized = alpha_nbt_compound_serialize_root(rustPtr_, rootName.c_str());
        if (serialized.data) {
            buf.writeBytes(serialized.data, serialized.len);
            alpha_buffer_free(serialized);
        }
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
            if (rustPtr_) {
                alpha_nbt_compound_free(rustPtr_);
            }
            rustPtr_ = r_comp;
            if (out_name) alpha_nbt_free_name(out_name);

            if (bytes_read >= 3) {
                buf.readPos += (bytes_read - 3);
            }
            tags.invalidateCache();
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

        auto comp = std::make_shared<NBTCompound>(r_comp);
        if (out_name) alpha_nbt_free_name(out_name);

        buf.readPos += bytes_read;
        return comp;
    }

    std::shared_ptr<NBTTag> readTagFromRust(const std::string& name, uint8_t type) const;
};

class NBTList : public NBTTag {
public:
    NbtList* rustPtr_ = nullptr;
    NBTListTagsProxy tags;
    NBTTagType tagType = NBTTagType::TAG_End;

    NBTList() : rustPtr_(alpha_nbt_list_create(0)), tags(this) {}
    explicit NBTList(NbtList* ptr) : rustPtr_(ptr ? ptr : alpha_nbt_list_create(0)), tags(this) {
        tagType = static_cast<NBTTagType>(alpha_nbt_list_get_type(rustPtr_));
    }

    ~NBTList() override {
        if (rustPtr_) {
            alpha_nbt_list_free(rustPtr_);
            rustPtr_ = nullptr;
        }
    }

    NBTList(const NBTList& other) : tags(this) {
        rustPtr_ = alpha_nbt_list_clone(other.rustPtr_);
        tagType = other.tagType;
    }

    NBTList& operator=(const NBTList& other) {
        if (this != &other) {
            if (rustPtr_) {
                alpha_nbt_list_free(rustPtr_);
            }
            rustPtr_ = alpha_nbt_list_clone(other.rustPtr_);
            tagType = other.tagType;
            tags.invalidateCache();
        }
        return *this;
    }

    NBTList(NBTList&& other) noexcept : tags(this) {
        rustPtr_ = other.rustPtr_;
        tagType = other.tagType;
        other.rustPtr_ = nullptr;
    }

    NBTList& operator=(NBTList&& other) noexcept {
        if (this != &other) {
            if (rustPtr_) {
                alpha_nbt_list_free(rustPtr_);
            }
            rustPtr_ = other.rustPtr_;
            tagType = other.tagType;
            other.rustPtr_ = nullptr;
            tags.invalidateCache();
        }
        return *this;
    }

    NBTTagType getType() const override { return NBTTagType::TAG_List; }

    void writeContents(ByteBuffer& buf) const override {
        NBTTagType actualType = tags.empty() ? NBTTagType::TAG_Byte : tagType;
        buf.writeUByte(static_cast<uint8_t>(actualType));
        buf.writeInt(static_cast<int32_t>(tags.size()));
        for (const auto& tag : tags) {
            tag->writeContents(buf);
        }
    }

    std::shared_ptr<NBTTag> readTagFromRust(size_t idx, uint8_t type) const;
    void addTagToRust(const std::shared_ptr<NBTTag>& tag);
};

// Inline implementations of setCompound and setList
inline void NBTCompound::setCompound(const std::string& name, std::shared_ptr<NBTCompound> v) {
    if (v) {
        NbtCompound* cloned = alpha_nbt_compound_clone(v->rustPtr_);
        alpha_nbt_compound_set_compound(rustPtr_, name.c_str(), cloned);
    } else {
        alpha_nbt_compound_remove(rustPtr_, name.c_str());
    }
    tags.invalidateCache();
}

inline void NBTCompound::setList(const std::string& name, std::shared_ptr<NBTList> v) {
    if (v) {
        NbtList* cloned = alpha_nbt_list_clone(v->rustPtr_);
        alpha_nbt_compound_set_list(rustPtr_, name.c_str(), cloned);
    } else {
        alpha_nbt_compound_remove(rustPtr_, name.c_str());
    }
    tags.invalidateCache();
}

inline std::shared_ptr<NBTCompound> NBTCompound::getCompound(const std::string& name) const {
    NbtCompound* child = alpha_nbt_compound_get_compound(rustPtr_, name.c_str());
    if (!child) return nullptr;
    return std::make_shared<NBTCompound>(child);
}

inline std::shared_ptr<NBTList> NBTCompound::getList(const std::string& name) const {
    NbtList* child = alpha_nbt_compound_get_list(rustPtr_, name.c_str());
    if (!child) return nullptr;
    return std::make_shared<NBTList>(child);
}

// ProxyElement Inline Methods
inline ProxyElement& ProxyElement::operator=(const std::shared_ptr<NBTTag>& tag) {
    if (!tag) {
        alpha_nbt_compound_remove(parent_->rustPtr_, key_.c_str());
    } else {
        auto type = tag->getType();
        if (type == NBTTagType::TAG_Byte) {
            alpha_nbt_compound_set_byte(parent_->rustPtr_, key_.c_str(), std::dynamic_pointer_cast<NBTByte>(tag)->value);
        } else if (type == NBTTagType::TAG_Short) {
            alpha_nbt_compound_set_short(parent_->rustPtr_, key_.c_str(), std::dynamic_pointer_cast<NBTShort>(tag)->value);
        } else if (type == NBTTagType::TAG_Int) {
            alpha_nbt_compound_set_int(parent_->rustPtr_, key_.c_str(), std::dynamic_pointer_cast<NBTInt>(tag)->value);
        } else if (type == NBTTagType::TAG_Long) {
            alpha_nbt_compound_set_long(parent_->rustPtr_, key_.c_str(), std::dynamic_pointer_cast<NBTLong>(tag)->value);
        } else if (type == NBTTagType::TAG_Float) {
            alpha_nbt_compound_set_float(parent_->rustPtr_, key_.c_str(), std::dynamic_pointer_cast<NBTFloat>(tag)->value);
        } else if (type == NBTTagType::TAG_Double) {
            alpha_nbt_compound_set_double(parent_->rustPtr_, key_.c_str(), std::dynamic_pointer_cast<NBTDouble>(tag)->value);
        } else if (type == NBTTagType::TAG_String) {
            alpha_nbt_compound_set_string(parent_->rustPtr_, key_.c_str(), std::dynamic_pointer_cast<NBTString>(tag)->value.c_str());
        } else if (type == NBTTagType::TAG_ByteArray) {
            const auto& vec = std::dynamic_pointer_cast<NBTByteArray>(tag)->value;
            alpha_nbt_compound_set_byte_array(parent_->rustPtr_, key_.c_str(), vec.data(), vec.size());
        } else if (type == NBTTagType::TAG_Compound) {
            auto child = std::dynamic_pointer_cast<NBTCompound>(tag);
            NbtCompound* cloned = alpha_nbt_compound_clone(child->rustPtr_);
            alpha_nbt_compound_set_compound(parent_->rustPtr_, key_.c_str(), cloned);
        } else if (type == NBTTagType::TAG_List) {
            auto child = std::dynamic_pointer_cast<NBTList>(tag);
            NbtList* cloned = alpha_nbt_list_clone(child->rustPtr_);
            alpha_nbt_compound_set_list(parent_->rustPtr_, key_.c_str(), cloned);
        }
    }
    parent_->tags.invalidateCache();
    return *this;
}

inline ProxyElement::operator std::shared_ptr<NBTTag>() const {
    if (!alpha_nbt_compound_has_key(parent_->rustPtr_, key_.c_str())) {
        return nullptr;
    }
    uint8_t type = alpha_nbt_compound_get_tag_type(parent_->rustPtr_, key_.c_str());
    return parent_->readTagFromRust(key_, type);
}

// NBTCompoundTagsProxy Inline Methods
inline void NBTCompoundTagsProxy::populateCache() const {
    if (!cache_dirty_) return;
    cache_.clear();
    AlphaBuffer keys_buf = alpha_nbt_compound_get_keys(parent_->rustPtr_);
    if (keys_buf.data) {
        const char* p = reinterpret_cast<const char*>(keys_buf.data);
        const char* end = p + keys_buf.len;
        while (p < end && *p != '\0') {
            std::string key(p);
            p += key.size() + 1;
            uint8_t type = alpha_nbt_compound_get_tag_type(parent_->rustPtr_, key.c_str());
            cache_[key] = parent_->readTagFromRust(key, type);
        }
        alpha_buffer_free(keys_buf);
    }
    cache_dirty_ = false;
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::iterator NBTCompoundTagsProxy::find(const std::string& key) {
    populateCache();
    return cache_.find(key);
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator NBTCompoundTagsProxy::find(const std::string& key) const {
    populateCache();
    return cache_.find(key);
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::iterator NBTCompoundTagsProxy::end() {
    populateCache();
    return cache_.end();
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator NBTCompoundTagsProxy::end() const {
    populateCache();
    return cache_.end();
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::iterator NBTCompoundTagsProxy::begin() {
    populateCache();
    return cache_.begin();
}

inline std::map<std::string, std::shared_ptr<NBTTag>>::const_iterator NBTCompoundTagsProxy::begin() const {
    populateCache();
    return cache_.begin();
}

inline bool NBTCompoundTagsProxy::empty() const {
    populateCache();
    return cache_.empty();
}

inline size_t NBTCompoundTagsProxy::size() const {
    populateCache();
    return cache_.size();
}

// NBTListTagsProxy Inline Methods
inline void NBTListTagsProxy::populateCache() const {
    if (!cache_dirty_) return;
    cache_.clear();
    size_t size = alpha_nbt_list_get_size(parent_->rustPtr_);
    uint8_t type = alpha_nbt_list_get_type(parent_->rustPtr_);
    if (type == 0 && parent_->tagType != NBTTagType::TAG_End) {
        type = static_cast<uint8_t>(parent_->tagType);
        alpha_nbt_list_set_type(parent_->rustPtr_, type);
    }
    for (size_t i = 0; i < size; ++i) {
        cache_.push_back(parent_->readTagFromRust(i, type));
    }
    cache_dirty_ = false;
}

inline void NBTListTagsProxy::push_back(const std::shared_ptr<NBTTag>& tag) {
    if (!tag) return;
    uint8_t type = alpha_nbt_list_get_type(parent_->rustPtr_);
    if (type == 0) {
        uint8_t new_type = static_cast<uint8_t>(tag->getType());
        alpha_nbt_list_set_type(parent_->rustPtr_, new_type);
        parent_->tagType = static_cast<NBTTagType>(new_type);
    }
    parent_->addTagToRust(tag);
    invalidateCache();
}

inline size_t NBTListTagsProxy::size() const {
    return alpha_nbt_list_get_size(parent_->rustPtr_);
}

inline bool NBTListTagsProxy::empty() const {
    return size() == 0;
}

inline std::shared_ptr<NBTTag> NBTListTagsProxy::operator[](size_t idx) const {
    populateCache();
    return cache_[idx];
}

inline NBTListTagsProxy& NBTListTagsProxy::operator=(const std::vector<std::shared_ptr<NBTTag>>& vec) {
    uint8_t type = alpha_nbt_list_get_type(parent_->rustPtr_);
    alpha_nbt_list_free(parent_->rustPtr_);
    parent_->rustPtr_ = alpha_nbt_list_create(type);
    for (const auto& tag : vec) {
        push_back(tag);
    }
    invalidateCache();
    return *this;
}

inline std::vector<std::shared_ptr<NBTTag>>::iterator NBTListTagsProxy::begin() {
    populateCache();
    return cache_.begin();
}

inline std::vector<std::shared_ptr<NBTTag>>::const_iterator NBTListTagsProxy::begin() const {
    populateCache();
    return cache_.begin();
}

inline std::vector<std::shared_ptr<NBTTag>>::iterator NBTListTagsProxy::end() {
    populateCache();
    return cache_.end();
}

inline std::vector<std::shared_ptr<NBTTag>>::const_iterator NBTListTagsProxy::end() const {
    populateCache();
    return cache_.end();
}

// NBTCompound::readTagFromRust and NBTList::readTagFromRust / addTagToRust Inline Implementations
inline std::shared_ptr<NBTTag> NBTCompound::readTagFromRust(const std::string& name, uint8_t type) const {
    if (type == 1) return std::make_shared<NBTByte>(alpha_nbt_compound_get_byte(rustPtr_, name.c_str()));
    if (type == 2) return std::make_shared<NBTShort>(alpha_nbt_compound_get_short(rustPtr_, name.c_str()));
    if (type == 3) return std::make_shared<NBTInt>(alpha_nbt_compound_get_int(rustPtr_, name.c_str()));
    if (type == 4) return std::make_shared<NBTLong>(alpha_nbt_compound_get_long(rustPtr_, name.c_str()));
    if (type == 5) return std::make_shared<NBTFloat>(alpha_nbt_compound_get_float(rustPtr_, name.c_str()));
    if (type == 6) return std::make_shared<NBTDouble>(alpha_nbt_compound_get_double(rustPtr_, name.c_str()));
    if (type == 7) {
        AlphaBuffer buf = alpha_nbt_compound_get_byte_array(rustPtr_, name.c_str());
        std::vector<uint8_t> v;
        if (buf.data) {
            v.assign(buf.data, buf.data + buf.len);
            alpha_buffer_free(buf);
        }
        return std::make_shared<NBTByteArray>(std::move(v));
    }
    if (type == 8) {
        AlphaBuffer buf = alpha_nbt_compound_get_string(rustPtr_, name.c_str());
        std::string s;
        if (buf.data) {
            s = reinterpret_cast<const char*>(buf.data);
            alpha_buffer_free(buf);
        }
        return std::make_shared<NBTString>(std::move(s));
    }
    if (type == 9) {
        NbtList* r_list = alpha_nbt_compound_get_list(rustPtr_, name.c_str());
        return std::make_shared<NBTList>(r_list);
    }
    if (type == 10) {
        NbtCompound* r_comp = alpha_nbt_compound_get_compound(rustPtr_, name.c_str());
        return std::make_shared<NBTCompound>(r_comp);
    }
    return nullptr;
}

inline std::shared_ptr<NBTTag> NBTList::readTagFromRust(size_t idx, uint8_t type) const {
    if (type == 1) return std::make_shared<NBTByte>(alpha_nbt_list_get_byte(rustPtr_, idx));
    if (type == 2) return std::make_shared<NBTShort>(alpha_nbt_list_get_short(rustPtr_, idx));
    if (type == 3) return std::make_shared<NBTInt>(alpha_nbt_list_get_int(rustPtr_, idx));
    if (type == 4) return std::make_shared<NBTLong>(alpha_nbt_list_get_long(rustPtr_, idx));
    if (type == 5) return std::make_shared<NBTFloat>(alpha_nbt_list_get_float(rustPtr_, idx));
    if (type == 6) return std::make_shared<NBTDouble>(alpha_nbt_list_get_double(rustPtr_, idx));
    if (type == 7) {
        AlphaBuffer buf = alpha_nbt_list_get_byte_array(rustPtr_, idx);
        std::vector<uint8_t> v;
        if (buf.data) {
            v.assign(buf.data, buf.data + buf.len);
            alpha_buffer_free(buf);
        }
        return std::make_shared<NBTByteArray>(std::move(v));
    }
    if (type == 8) {
        AlphaBuffer buf = alpha_nbt_list_get_string(rustPtr_, idx);
        std::string s;
        if (buf.data) {
            s = reinterpret_cast<const char*>(buf.data);
            alpha_buffer_free(buf);
        }
        return std::make_shared<NBTString>(std::move(s));
    }
    if (type == 9) {
        NbtList* r_list = alpha_nbt_list_get_list(rustPtr_, idx);
        return std::make_shared<NBTList>(r_list);
    }
    if (type == 10) {
        NbtCompound* r_comp = alpha_nbt_list_get_compound(rustPtr_, idx);
        return std::make_shared<NBTCompound>(r_comp);
    }
    return nullptr;
}

inline void NBTList::addTagToRust(const std::shared_ptr<NBTTag>& tag) {
    auto type = tag->getType();
    if (type == NBTTagType::TAG_Byte) {
        alpha_nbt_list_add_byte(rustPtr_, std::dynamic_pointer_cast<NBTByte>(tag)->value);
    } else if (type == NBTTagType::TAG_Short) {
        alpha_nbt_list_add_short(rustPtr_, std::dynamic_pointer_cast<NBTShort>(tag)->value);
    } else if (type == NBTTagType::TAG_Int) {
        alpha_nbt_list_add_int(rustPtr_, std::dynamic_pointer_cast<NBTInt>(tag)->value);
    } else if (type == NBTTagType::TAG_Long) {
        alpha_nbt_list_add_long(rustPtr_, std::dynamic_pointer_cast<NBTLong>(tag)->value);
    } else if (type == NBTTagType::TAG_Float) {
        alpha_nbt_list_add_float(rustPtr_, std::dynamic_pointer_cast<NBTFloat>(tag)->value);
    } else if (type == NBTTagType::TAG_Double) {
        alpha_nbt_list_add_double(rustPtr_, std::dynamic_pointer_cast<NBTDouble>(tag)->value);
    } else if (type == NBTTagType::TAG_String) {
        alpha_nbt_list_add_string(rustPtr_, std::dynamic_pointer_cast<NBTString>(tag)->value.c_str());
    } else if (type == NBTTagType::TAG_ByteArray) {
        const auto& vec = std::dynamic_pointer_cast<NBTByteArray>(tag)->value;
        alpha_nbt_list_add_byte_array(rustPtr_, vec.data(), vec.size());
    } else if (type == NBTTagType::TAG_Compound) {
        auto child = std::dynamic_pointer_cast<NBTCompound>(tag);
        NbtCompound* cloned = alpha_nbt_compound_clone(child->rustPtr_);
        alpha_nbt_list_add_compound(rustPtr_, cloned);
    } else if (type == NBTTagType::TAG_List) {
        auto child = std::dynamic_pointer_cast<NBTList>(tag);
        NbtList* cloned = alpha_nbt_list_clone(child->rustPtr_);
        alpha_nbt_list_add_list(rustPtr_, cloned);
    }
    tags.invalidateCache();
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
        alpha_nbt_list_free(list->rustPtr_);
        list->rustPtr_ = alpha_nbt_list_create(static_cast<uint8_t>(list->tagType));
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
