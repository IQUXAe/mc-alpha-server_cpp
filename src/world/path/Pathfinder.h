#pragma once

#include "../../core/MathHelper.h"
#include "../../core/Vec3D.h"
#include "../../entity/Entity.h"
#include "../../core/Material.h"
#include "../../../rust/alpha_bridge/alpha_bridge.h"
#include "../World.h"

#include <vector>
#include <memory>
#include <optional>

struct PathPoint {
    int xCoord;
    int yCoord;
    int zCoord;

    PathPoint(int x, int y, int z) : xCoord(x), yCoord(y), zCoord(z) {}

    float distanceTo(const PathPoint& other) const {
        const float dx = static_cast<float>(other.xCoord - xCoord);
        const float dy = static_cast<float>(other.yCoord - yCoord);
        const float dz = static_cast<float>(other.zCoord - zCoord);
        return MathHelper::sqrt_float(dx * dx + dy * dy + dz * dz);
    }
};

class PathEntity {
public:
    explicit PathEntity(std::vector<PathPoint> points) : points_(std::move(points)) {}

    void incrementPathIndex() {
        ++pathIndex_;
    }

    [[nodiscard]] bool isFinished() const {
        return pathIndex_ >= points_.size();
    }

    [[nodiscard]] std::optional<Vec3D> getPosition(const Entity& entity) const {
        if (isFinished()) {
            return std::nullopt;
        }

        const auto& point = points_[pathIndex_];
        const double widthOffset = static_cast<double>(static_cast<int>(entity.width + 1.0f)) * 0.5;
        return Vec3D(
            static_cast<double>(point.xCoord) + widthOffset,
            static_cast<double>(point.yCoord),
            static_cast<double>(point.zCoord) + widthOffset);
    }

private:
    std::vector<PathPoint> points_;
    std::size_t pathIndex_ = 0;
};

class Pathfinder {
public:
    explicit Pathfinder(World& world) : world_(world) {}

    std::unique_ptr<PathEntity> createEntityPathTo(const Entity& entity, int x, int y, int z, float maxDistance) {
        return createEntityPathTo(entity, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5,
                                  static_cast<double>(z) + 0.5, maxDistance);
    }

    std::unique_ptr<PathEntity> createEntityPathTo(const Entity& entity, double x, double y, double z,
                                                   float maxDistance) {
        PathfinderWorldAccessor accessor {
            .world = &world_,
            .is_liquid = [](void* world_ptr, int32_t px, int32_t py, int32_t pz) -> bool {
                auto* w = static_cast<World*>(world_ptr);
                Material* m = w->getBlockMaterialNoChunkLoad(px, py, pz);
                return m && m->getIsLiquid();
            },
            .blocks_movement = [](void* world_ptr, int32_t px, int32_t py, int32_t pz) -> bool {
                auto* w = static_cast<World*>(world_ptr);
                Material* m = w->getBlockMaterialNoChunkLoad(px, py, pz);
                return m && m->blocksMovement();
            }
        };

        constexpr int MAX_PATH_POINTS = 1024;
        std::vector<FfiPathPoint> ffiPoints(MAX_PATH_POINTS);

        int32_t count = rust_pathfinder_find_path(
            accessor,
            entity.boundingBox.minX,
            entity.boundingBox.minY,
            entity.boundingBox.minZ,
            x,
            y,
            z,
            entity.width,
            entity.height,
            maxDistance,
            ffiPoints.data(),
            MAX_PATH_POINTS
        );

        if (count <= 0) {
            return nullptr;
        }

        std::vector<PathPoint> points;
        points.reserve(count);
        for (int32_t i = 0; i < count; ++i) {
            points.emplace_back(ffiPoints[i].x, ffiPoints[i].y, ffiPoints[i].z);
        }

        return std::make_unique<PathEntity>(std::move(points));
    }

private:
    World& world_;
};

