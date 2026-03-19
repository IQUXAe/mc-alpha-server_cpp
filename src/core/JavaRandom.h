#pragma once
#include <cstdint>

class JavaRandom {
private:
    uint64_t seed;
public:
    JavaRandom() : seed(0) {}
    explicit JavaRandom(int64_t s) {
        setSeed(s);
    }
    void setSeed(int64_t s) {
        seed = (s ^ 0x5DEECE66DLL) & ((1LL << 48) - 1);
    }
    int next(int bits) {
        seed = (seed * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1);
        return (int)(((uint64_t)seed) >> (48 - bits));
    }
    int nextInt() {
        return next(32);
    }
    int nextInt(int bound) {
        if ((bound & -bound) == bound)
            return (int)((bound * (uint64_t)next(31)) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val = bits % bound;
        } while (bits - val + (bound - 1) < 0);
        return val;
    }
    int64_t nextLong() {
        return ((int64_t)(next(32)) << 32) + next(32);
    }
    double nextDouble() {
        return (((int64_t)(next(26)) << 27) + next(27)) / (double)(1LL << 53);
    }
    float nextFloat() {
        return next(24) / (float)(1 << 24);
    }
};
