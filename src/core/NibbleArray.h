#pragma once

#include <span>
#include <cstdint>
#include <algorithm>

class NibbleArray {
public:
    uint8_t* data_ptr = nullptr;
    int size = 0;
    bool owns_data = false;

    NibbleArray() = default;

    explicit NibbleArray(int size) {
        this->size = size >> 1;
        this->data_ptr = new uint8_t[this->size]();
        this->owns_data = true;
    }

    explicit NibbleArray(uint8_t* ptr, int size, bool owns) 
        : data_ptr(ptr), size(size), owns_data(owns) {}

    explicit NibbleArray(std::vector<uint8_t> d) {
        this->size = d.size();
        this->data_ptr = new uint8_t[this->size];
        std::copy(d.begin(), d.end(), this->data_ptr);
        this->owns_data = true;
    }

    // Destructor
    ~NibbleArray() {
        if (owns_data && data_ptr) {
            delete[] data_ptr;
        }
    }

    // Move constructor / assignment
    NibbleArray(NibbleArray&& other) noexcept 
        : data_ptr(other.data_ptr), size(other.size), owns_data(other.owns_data) {
        other.data_ptr = nullptr;
        other.size = 0;
        other.owns_data = false;
    }

    NibbleArray& operator=(NibbleArray&& other) noexcept {
        if (this != &other) {
            if (owns_data && data_ptr) {
                delete[] data_ptr;
            }
            data_ptr = other.data_ptr;
            size = other.size;
            owns_data = other.owns_data;
            other.data_ptr = nullptr;
            other.size = 0;
            other.owns_data = false;
        }
        return *this;
    }

    // Disallow copy constructor / assignment to prevent double-free
    NibbleArray(const NibbleArray& other) = delete;
    NibbleArray& operator=(const NibbleArray& other) = delete;

    [[nodiscard]] int getNibble(int x, int y, int z) const {
        if (!data_ptr) return 0;
        int index = (x << 11) | (z << 7) | y;
        uint8_t byte = data_ptr[index >> 1];
        return (index & 1) ? (byte >> 4) & 0xF : byte & 0xF;
    }

    void setNibble(int x, int y, int z, int value) {
        if (!data_ptr) return;
        int index = (x << 11) | (z << 7) | y;
        auto& byte = data_ptr[index >> 1];
        if (index & 1)
            byte = static_cast<uint8_t>((byte & 0x0F) | ((value & 0xF) << 4));
        else
            byte = static_cast<uint8_t>((byte & 0xF0) | (value & 0xF));
    }

    [[nodiscard]] bool isValid() const { return data_ptr != nullptr; }
    [[nodiscard]] std::span<const uint8_t> view() const { 
        return std::span<const uint8_t>(data_ptr, size); 
    }
};

