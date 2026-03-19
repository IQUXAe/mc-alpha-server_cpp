#pragma once

#include <vector>
#include <cstdint>

class NibbleArray {
public:
    std::vector<uint8_t> data;

    NibbleArray() = default;

    explicit NibbleArray(int size) : data(size >> 1, 0) {}

    explicit NibbleArray(std::vector<uint8_t> d) : data(std::move(d)) {}

    int getNibble(int x, int y, int z) const {
        int index = (x << 11) | (z << 7) | y;
        int halfIndex = index >> 1;
        int oddBit = index & 1;
        if (oddBit == 0) {
            return data[halfIndex] & 15;
        } else {
            return (data[halfIndex] >> 4) & 15;
        }
    }

    void setNibble(int x, int y, int z, int value) {
        int index = (x << 11) | (z << 7) | y;
        int halfIndex = index >> 1;
        int oddBit = index & 1;
        if (oddBit == 0) {
            data[halfIndex] = static_cast<uint8_t>((data[halfIndex] & 0xF0) | (value & 0x0F));
        } else {
            data[halfIndex] = static_cast<uint8_t>((data[halfIndex] & 0x0F) | ((value & 0x0F) << 4));
        }
    }

    bool isValid() const {
        return !data.empty();
    }
};
