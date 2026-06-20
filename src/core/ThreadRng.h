#pragma once

#include <random>
#include <cstdint>

// Replacement for std::rand() that uses a thread-local mt19937 seeded
// uniformly per thread. std::rand() is not thread-safe (modifies shared
// state without synchronization) and std::mt19937 / std::uniform_int_distribution
// don't sacrifice a lot of throughput for the typical Minecraft call sites
// (per-entity tick paths, single draws only).
//
// The world server runs the main entity / block-tick loop on the same
// thread, so "deterministic per-process" is preserved (the global seed is
// auto-generated at program startup time, before the first use here). For
// future work that needs cross-tick reproducibility with a known seed,
// replace `rng()` with a World::rand-derived sequence.
//
// All helpers return a freshly drawn int or float; for a continuous stream
// keep a local rng and call `engine()`.
namespace ThreadRng {

inline std::mt19937& engine() {
    thread_local std::mt19937 rng{std::random_device{}()};
    return rng;
}

inline int nextInt(int bound) {
    if (bound <= 0) return 0;
    std::uniform_int_distribution<int> d(0, bound - 1);
    return d(engine());
}

inline int nextIntInRange(int min, int maxInclusive) {
    if (maxInclusive < min) return min;
    std::uniform_int_distribution<int> d(min, maxInclusive);
    return d(engine());
}

inline float nextFloat() {
    std::uniform_real_distribution<float> d(0.0f, 1.0f);
    return d(engine());
}

inline double nextDouble() {
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(engine());
}

} // namespace ThreadRng
