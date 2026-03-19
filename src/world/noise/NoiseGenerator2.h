#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include "../../core/JavaRandom.h"

// Exact port of Java's NoiseGenerator2 (2D Simplex noise)
class NoiseGenerator2 {
private:
    static constexpr int grad3[12][3] = {
        {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
        {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
        {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}
    };

    int perm[512];

    static const double F2;
    static const double G2;

    static int fastFloor(double v) {
        return v > 0.0 ? (int)v : (int)v - 1;
    }

    static double dot2(const int g[3], double x, double y) {
        return (double)g[0] * x + (double)g[1] * y;
    }

public:
    double xCoord;
    double yCoord;
    double zCoord;

    NoiseGenerator2() : NoiseGenerator2(JavaRandom()) {}

    NoiseGenerator2(JavaRandom rand) {
        perm[0] = 0; // init
        xCoord = rand.nextDouble() * 256.0;
        yCoord = rand.nextDouble() * 256.0;
        zCoord = rand.nextDouble() * 256.0;

        for (int i = 0; i < 256; ++i) {
            perm[i] = i;
        }

        for (int i = 0; i < 256; ++i) {
            int j = rand.nextInt(256 - i) + i;
            int temp = perm[i];
            perm[i] = perm[j];
            perm[j] = temp;
            perm[i + 256] = perm[i];
        }
    }

    void func_4115_a(std::vector<double>& arr, double xBase, double yBase,
                     int xSize, int ySize, double xScale, double yScale, double amplitude) const {
        int index = 0;

        for (int ix = 0; ix < xSize; ++ix) {
            double x = (xBase + (double)ix) * xScale + xCoord;

            for (int iy = 0; iy < ySize; ++iy) {
                double y = (yBase + (double)iy) * yScale + yCoord;

                // Skew input space
                double s = (x + y) * F2;
                int i = fastFloor(x + s);
                int j = fastFloor(y + s);
                double t = (double)(i + j) * G2;
                double X0 = (double)i - t;
                double Y0 = (double)j - t;
                double x0 = x - X0;
                double y0 = y - Y0;

                int i1, j1;
                if (x0 > y0) {
                    i1 = 1; j1 = 0;
                } else {
                    i1 = 0; j1 = 1;
                }

                double x1 = x0 - (double)i1 + G2;
                double y1 = y0 - (double)j1 + G2;
                double x2 = x0 - 1.0 + 2.0 * G2;
                double y2 = y0 - 1.0 + 2.0 * G2;

                int ii = i & 255;
                int jj = j & 255;
                int gi0 = perm[ii + perm[jj]] % 12;
                int gi1 = perm[ii + i1 + perm[jj + j1]] % 12;
                int gi2 = perm[ii + 1 + perm[jj + 1]] % 12;

                double t0 = 0.5 - x0 * x0 - y0 * y0;
                double n0;
                if (t0 < 0.0) {
                    n0 = 0.0;
                } else {
                    t0 *= t0;
                    n0 = t0 * t0 * dot2(grad3[gi0], x0, y0);
                }

                double t1 = 0.5 - x1 * x1 - y1 * y1;
                double n1;
                if (t1 < 0.0) {
                    n1 = 0.0;
                } else {
                    t1 *= t1;
                    n1 = t1 * t1 * dot2(grad3[gi1], x1, y1);
                }

                double t2 = 0.5 - x2 * x2 - y2 * y2;
                double n2;
                if (t2 < 0.0) {
                    n2 = 0.0;
                } else {
                    t2 *= t2;
                    n2 = t2 * t2 * dot2(grad3[gi2], x2, y2);
                }

                arr[index++] += 70.0 * (n0 + n1 + n2) * amplitude;
            }
        }
    }
};

// Static member definitions
inline const double NoiseGenerator2::F2 = 0.5 * (std::sqrt(3.0) - 1.0);
inline const double NoiseGenerator2::G2 = (3.0 - std::sqrt(3.0)) / 6.0;
