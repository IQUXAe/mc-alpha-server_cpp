#pragma once

#include <cstdint>
#include <bit>

class JavaRandom {
public:
    JavaRandom() : seed_(0) {}
    explicit JavaRandom(int64_t s) { setSeed(s); }

    void setSeed(int64_t s) {
        seed_ = (static_cast<uint64_t>(s) ^ 0x5DEECE66DULL) & MASK;
    }

    int next(int bits) {
        seed_ = (seed_ * 0x5DEECE66DULL + 0xBULL) & MASK;
        return static_cast<int>(seed_ >> (48 - bits));
    }

    int nextInt() { return next(32); }

    int nextInt(int bound) {
        if (std::has_single_bit(static_cast<unsigned>(bound)))
            return static_cast<int>((static_cast<uint64_t>(bound) * static_cast<uint64_t>(next(31))) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val = bits % bound;
        } while (bits - val + (bound - 1) < 0);
        return val;
    }

    int64_t nextLong() {
        return (static_cast<int64_t>(next(32)) << 32) + next(32);
    }

    double nextDouble() {
        return ((static_cast<int64_t>(next(26)) << 27) + next(27)) / static_cast<double>(1LL << 53);
    }

    float nextFloat() {
        return next(24) / static_cast<float>(1 << 24);
    }

private:
    static constexpr uint64_t MASK = (1ULL << 48) - 1;
    uint64_t seed_;
};
