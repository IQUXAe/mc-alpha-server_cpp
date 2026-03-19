#pragma once

#include <cmath>
#include <array>
#include <cstdint>

class MathHelper {
public:
    static void init() {
        for (int i = 0; i < 65536; ++i) {
            sinTable[i] = static_cast<float>(std::sin(static_cast<double>(i) * M_PI * 2.0 / 65536.0));
        }
    }

    static float sin(float v) {
        return sinTable[static_cast<int>(v * 10430.378f) & 0xFFFF];
    }

    static float cos(float v) {
        return sinTable[static_cast<int>(v * 10430.378f + 16384.0f) & 0xFFFF];
    }

    static float sqrt_float(float v) {
        return static_cast<float>(std::sqrt(static_cast<double>(v)));
    }

    static float sqrt_double(double v) {
        return static_cast<float>(std::sqrt(v));
    }

    static int32_t floor_float(float v) {
        int32_t i = static_cast<int32_t>(v);
        return v < static_cast<float>(i) ? i - 1 : i;
    }

    static int32_t floor_double(double v) {
        int32_t i = static_cast<int32_t>(v);
        return v < static_cast<double>(i) ? i - 1 : i;
    }

    static float abs(float v) {
        return v >= 0.0f ? v : -v;
    }

    static double abs_max(double a, double b) {
        if (a < 0.0) a = -a;
        if (b < 0.0) b = -b;
        return a > b ? a : b;
    }

private:
    static inline std::array<float, 65536> sinTable = {};
};
