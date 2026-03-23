#pragma once

#include <vector>
#include <span>
#include <cstdint>

class NibbleArray {
public:
    std::vector<uint8_t> data;

    NibbleArray() = default;
    explicit NibbleArray(int size) : data(size >> 1, 0) {}
    explicit NibbleArray(std::vector<uint8_t> d) : data(std::move(d)) {}

    [[nodiscard]] int getNibble(int x, int y, int z) const {
        int index = (x << 11) | (z << 7) | y;
        uint8_t byte = data[index >> 1];
        return (index & 1) ? (byte >> 4) & 0xF : byte & 0xF;
    }

    void setNibble(int x, int y, int z, int value) {
        int index = (x << 11) | (z << 7) | y;
        auto& byte = data[index >> 1];
        if (index & 1)
            byte = static_cast<uint8_t>((byte & 0x0F) | ((value & 0xF) << 4));
        else
            byte = static_cast<uint8_t>((byte & 0xF0) | (value & 0xF));
    }

    [[nodiscard]] bool isValid() const { return !data.empty(); }
    [[nodiscard]] std::span<const uint8_t> view() const { return data; }
};
