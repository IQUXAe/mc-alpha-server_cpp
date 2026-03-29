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

    // Advances internal LCG state by `steps` draws in O(log steps).
    // Equivalent to calling next(...) `steps` times without producing values.
    void advance(uint64_t steps) {
        uint64_t accMul = 1;
        uint64_t accAdd = 0;
        uint64_t curMul = MULT;
        uint64_t curAdd = ADD;

        while (steps > 0) {
            if (steps & 1ULL) {
                accMul = (accMul * curMul) & MASK;
                accAdd = (accAdd * curMul + curAdd) & MASK;
            }

            const uint64_t oldMul = curMul;
            curMul = (curMul * curMul) & MASK;
            curAdd = (curAdd * (oldMul + 1ULL)) & MASK;
            steps >>= 1ULL;
        }

        seed_ = (seed_ * accMul + accAdd) & MASK;
    }

    // Hot-path shorthands for common skip counts in world generation code.
    void advance4() { seed_ = (seed_ * MULT_4 + ADD_4) & MASK; }
    void advance6() { seed_ = (seed_ * MULT_6 + ADD_6) & MASK; }

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
    static constexpr uint64_t MULT = 0x5DEECE66DULL;
    static constexpr uint64_t ADD = 0xBULL;
    static constexpr uint64_t MASK = (1ULL << 48) - 1;
    static constexpr uint64_t MULT_4 = 0x32EB772C5F11ULL;
    static constexpr uint64_t ADD_4 = 0x2D3873C4CD04ULL;
    static constexpr uint64_t MULT_6 = 0x45D73749A7F9ULL;
    static constexpr uint64_t ADD_6 = 0x17617168255EULL;
    uint64_t seed_;
};
