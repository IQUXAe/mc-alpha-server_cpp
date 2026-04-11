#pragma once

#include "MathHelper.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

class Vec3D {
public:
    double xCoord;
    double yCoord;
    double zCoord;

    Vec3D(double x = 0.0, double y = 0.0, double z = 0.0)
        : xCoord(x == -0.0 ? 0.0 : x),
          yCoord(y == -0.0 ? 0.0 : y),
          zCoord(z == -0.0 ? 0.0 : z) {}

    static Vec3D createVectorHelper(double x, double y, double z) {
        return Vec3D(x, y, z);
    }

    Vec3D& setComponents(double x, double y, double z) {
        xCoord = x;
        yCoord = y;
        zCoord = z;
        return *this;
    }

    Vec3D normalize() const {
        double len = MathHelper::sqrt_double(xCoord * xCoord + yCoord * yCoord + zCoord * zCoord);
        if (len < 1.0E-4) return Vec3D(0.0, 0.0, 0.0);
        return Vec3D(xCoord / len, yCoord / len, zCoord / len);
    }

    Vec3D addVector(double x, double y, double z) const {
        return Vec3D(xCoord + x, yCoord + y, zCoord + z);
    }

    double distanceTo(const Vec3D& other) const {
        double dx = other.xCoord - xCoord;
        double dy = other.yCoord - yCoord;
        double dz = other.zCoord - zCoord;
        return MathHelper::sqrt_double(dx * dx + dy * dy + dz * dz);
    }

    double squareDistanceTo(const Vec3D& other) const {
        double dx = other.xCoord - xCoord;
        double dy = other.yCoord - yCoord;
        double dz = other.zCoord - zCoord;
        return dx * dx + dy * dy + dz * dz;
    }

    double squareDistanceTo(double x, double y, double z) const {
        double dx = x - xCoord;
        double dy = y - yCoord;
        double dz = z - zCoord;
        return dx * dx + dy * dy + dz * dz;
    }

    double lengthVector() const {
        return MathHelper::sqrt_double(xCoord * xCoord + yCoord * yCoord + zCoord * zCoord);
    }

    std::optional<Vec3D> getIntermediateWithXValue(const Vec3D& other, double x) const {
        double dx = other.xCoord - xCoord;
        double dy = other.yCoord - yCoord;
        double dz = other.zCoord - zCoord;
        if (dx * dx < 1.0E-7) return std::nullopt;
        double t = (x - xCoord) / dx;
        if (t >= 0.0 && t <= 1.0) return Vec3D(xCoord + dx * t, yCoord + dy * t, zCoord + dz * t);
        return std::nullopt;
    }

    std::optional<Vec3D> getIntermediateWithYValue(const Vec3D& other, double y) const {
        double dx = other.xCoord - xCoord;
        double dy = other.yCoord - yCoord;
        double dz = other.zCoord - zCoord;
        if (dy * dy < 1.0E-7) return std::nullopt;
        double t = (y - yCoord) / dy;
        if (t >= 0.0 && t <= 1.0) return Vec3D(xCoord + dx * t, yCoord + dy * t, zCoord + dz * t);
        return std::nullopt;
    }

    std::optional<Vec3D> getIntermediateWithZValue(const Vec3D& other, double z) const {
        double dx = other.xCoord - xCoord;
        double dy = other.yCoord - yCoord;
        double dz = other.zCoord - zCoord;
        if (dz * dz < 1.0E-7) return std::nullopt;
        double t = (z - zCoord) / dz;
        if (t >= 0.0 && t <= 1.0) return Vec3D(xCoord + dx * t, yCoord + dy * t, zCoord + dz * t);
        return std::nullopt;
    }

    std::string toString() const {
        return "(" + std::to_string(xCoord) + ", " + std::to_string(yCoord) + ", " + std::to_string(zCoord) + ")";
    }
};
