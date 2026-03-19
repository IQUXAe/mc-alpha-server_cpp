#pragma once

#include "Vec3D.h"
#include <optional>
#include <cstdint>

struct MovingObjectPosition {
    int blockX;
    int blockY;
    int blockZ;
    int8_t sideHit;
    Vec3D hitVec;

    MovingObjectPosition(int x, int y, int z, int8_t side, const Vec3D& hit)
        : blockX(x), blockY(y), blockZ(z), sideHit(side), hitVec(hit) {}
};

class AxisAlignedBB {
public:
    double minX, minY, minZ;
    double maxX, maxY, maxZ;

    AxisAlignedBB(double x0 = 0.0, double y0 = 0.0, double z0 = 0.0,
                  double x1 = 0.0, double y1 = 0.0, double z1 = 0.0)
        : minX(x0), minY(y0), minZ(z0), maxX(x1), maxY(y1), maxZ(z1) {}

    static AxisAlignedBB getBoundingBox(double x0, double y0, double z0,
                                         double x1, double y1, double z1) {
        return AxisAlignedBB(x0, y0, z0, x1, y1, z1);
    }

    AxisAlignedBB& setBounds(double x0, double y0, double z0,
                              double x1, double y1, double z1) {
        minX = x0; minY = y0; minZ = z0;
        maxX = x1; maxY = y1; maxZ = z1;
        return *this;
    }

    AxisAlignedBB addCoord(double x, double y, double z) const {
        double nx0 = minX, ny0 = minY, nz0 = minZ;
        double nx1 = maxX, ny1 = maxY, nz1 = maxZ;
        if (x < 0.0) nx0 += x; 
        if (x > 0.0) nx1 += x;
        if (y < 0.0) ny0 += y; 
        if (y > 0.0) ny1 += y;
        if (z < 0.0) nz0 += z; 
        if (z > 0.0) nz1 += z;
        return AxisAlignedBB(nx0, ny0, nz0, nx1, ny1, nz1);
    }

    AxisAlignedBB expand(double x, double y, double z) const {
        return AxisAlignedBB(minX - x, minY - y, minZ - z,
                             maxX + x, maxY + y, maxZ + z);
    }

    AxisAlignedBB getOffsetBoundingBox(double x, double y, double z) const {
        return AxisAlignedBB(minX + x, minY + y, minZ + z,
                             maxX + x, maxY + y, maxZ + z);
    }

    double clipXCollide(const AxisAlignedBB& other, double dx) const {
        if (other.maxY > minY && other.minY < maxY && other.maxZ > minZ && other.minZ < maxZ) {
            if (dx > 0.0 && other.maxX <= minX) {
                double d = minX - other.maxX;
                if (d < dx) dx = d;
            }
            if (dx < 0.0 && other.minX >= maxX) {
                double d = maxX - other.minX;
                if (d > dx) dx = d;
            }
        }
        return dx;
    }

    double clipYCollide(const AxisAlignedBB& other, double dy) const {
        if (other.maxX > minX && other.minX < maxX && other.maxZ > minZ && other.minZ < maxZ) {
            if (dy > 0.0 && other.maxY <= minY) {
                double d = minY - other.maxY;
                if (d < dy) dy = d;
            }
            if (dy < 0.0 && other.minY >= maxY) {
                double d = maxY - other.minY;
                if (d > dy) dy = d;
            }
        }
        return dy;
    }

    double clipZCollide(const AxisAlignedBB& other, double dz) const {
        if (other.maxX > minX && other.minX < maxX && other.maxY > minY && other.minY < maxY) {
            if (dz > 0.0 && other.maxZ <= minZ) {
                double d = minZ - other.maxZ;
                if (d < dz) dz = d;
            }
            if (dz < 0.0 && other.minZ >= maxZ) {
                double d = maxZ - other.minZ;
                if (d > dz) dz = d;
            }
        }
        return dz;
    }

    bool intersectsWith(const AxisAlignedBB& other) const {
        return other.maxX > minX && other.minX < maxX &&
               other.maxY > minY && other.minY < maxY &&
               other.maxZ > minZ && other.minZ < maxZ;
    }

    AxisAlignedBB& offset(double x, double y, double z) {
        minX += x; minY += y; minZ += z;
        maxX += x; maxY += y; maxZ += z;
        return *this;
    }

    AxisAlignedBB shrink(double x, double y, double z) const {
        double nx0 = minX, ny0 = minY, nz0 = minZ;
        double nx1 = maxX, ny1 = maxY, nz1 = maxZ;
        if (x < 0.0) nx0 -= x; 
        if (x > 0.0) nx1 -= x;
        if (y < 0.0) ny0 -= y; 
        if (y > 0.0) ny1 -= y;
        if (z < 0.0) nz0 -= z; 
        if (z > 0.0) nz1 -= z;
        return AxisAlignedBB(nx0, ny0, nz0, nx1, ny1, nz1);
    }

    AxisAlignedBB copy() const {
        return AxisAlignedBB(minX, minY, minZ, maxX, maxY, maxZ);
    }

    void setBB(const AxisAlignedBB& other) {
        minX = other.minX; minY = other.minY; minZ = other.minZ;
        maxX = other.maxX; maxY = other.maxY; maxZ = other.maxZ;
    }

    std::optional<MovingObjectPosition> clip(const Vec3D& from, const Vec3D& to) const {
        std::optional<Vec3D> hits[6];
        hits[0] = from.getIntermediateWithXValue(to, minX); // face 4 (west)
        hits[1] = from.getIntermediateWithXValue(to, maxX); // face 5 (east)
        hits[2] = from.getIntermediateWithYValue(to, minY); // face 0 (bottom)
        hits[3] = from.getIntermediateWithYValue(to, maxY); // face 1 (top)
        hits[4] = from.getIntermediateWithZValue(to, minZ); // face 2 (north)
        hits[5] = from.getIntermediateWithZValue(to, maxZ); // face 3 (south)

        if (hits[0] && !isVecInYZ(*hits[0])) hits[0] = std::nullopt;
        if (hits[1] && !isVecInYZ(*hits[1])) hits[1] = std::nullopt;
        if (hits[2] && !isVecInXZ(*hits[2])) hits[2] = std::nullopt;
        if (hits[3] && !isVecInXZ(*hits[3])) hits[3] = std::nullopt;
        if (hits[4] && !isVecInXY(*hits[4])) hits[4] = std::nullopt;
        if (hits[5] && !isVecInXY(*hits[5])) hits[5] = std::nullopt;

        static constexpr int8_t faceMap[6] = {4, 5, 0, 1, 2, 3};

        int closestIdx = -1;
        double closestDist = 0.0;
        for (int i = 0; i < 6; ++i) {
            if (hits[i]) {
                double d = from.squareDistanceTo(*hits[i]);
                if (closestIdx < 0 || d < closestDist) {
                    closestIdx = i;
                    closestDist = d;
                }
            }
        }

        if (closestIdx < 0) return std::nullopt;
        return MovingObjectPosition(0, 0, 0, faceMap[closestIdx], *hits[closestIdx]);
    }

private:
    bool isVecInYZ(const Vec3D& v) const {
        return v.yCoord >= minY && v.yCoord <= maxY && v.zCoord >= minZ && v.zCoord <= maxZ;
    }

    bool isVecInXZ(const Vec3D& v) const {
        return v.xCoord >= minX && v.xCoord <= maxX && v.zCoord >= minZ && v.zCoord <= maxZ;
    }

    bool isVecInXY(const Vec3D& v) const {
        return v.xCoord >= minX && v.xCoord <= maxX && v.yCoord >= minY && v.yCoord <= maxY;
    }
};
