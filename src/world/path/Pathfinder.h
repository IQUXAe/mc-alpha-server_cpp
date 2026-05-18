#pragma once

#include "../../core/MathHelper.h"
#include "../../core/Vec3D.h"
#include "../../entity/Entity.h"
#include "../../core/Material.h"

#include <array>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>

class World;

struct PathPoint {
    int xCoord;
    int yCoord;
    int zCoord;
    int index = -1;
    float totalPathDistance = 0.0f;
    float distanceToNext = 0.0f;
    float distanceToTarget = 0.0f;
    PathPoint* previous = nullptr;
    bool isFirst = false;

    PathPoint(int x, int y, int z) : xCoord(x), yCoord(y), zCoord(z) {}

    float distanceTo(const PathPoint& other) const {
        const float dx = static_cast<float>(other.xCoord - xCoord);
        const float dy = static_cast<float>(other.yCoord - yCoord);
        const float dz = static_cast<float>(other.zCoord - zCoord);
        return MathHelper::sqrt_float(dx * dx + dy * dy + dz * dz);
    }
};

class Path {
public:
    PathPoint* addPoint(PathPoint* point) {
        if (point->index >= 0) {
            throw std::logic_error("Path point already assigned");
        }

        heap_.push_back(point);
        point->index = static_cast<int>(heap_.size() - 1);
        sortBack(point->index);
        return point;
    }

    void clearPath() {
        for (auto* point : heap_) {
            if (point) {
                point->index = -1;
            }
        }
        heap_.clear();
    }

    [[nodiscard]] bool isPathEmpty() const {
        return heap_.empty();
    }

    PathPoint* dequeue() {
        PathPoint* result = heap_.front();
        heap_.front() = heap_.back();
        heap_.pop_back();
        if (!heap_.empty()) {
            heap_.front()->index = 0;
            sortForward(0);
        }
        result->index = -1;
        return result;
    }

    void changeDistance(PathPoint* point, float distance) {
        const float oldDistance = point->distanceToTarget;
        point->distanceToTarget = distance;
        if (distance < oldDistance) {
            sortBack(point->index);
        } else {
            sortForward(point->index);
        }
    }

private:
    std::vector<PathPoint*> heap_;

    void sortBack(int index) {
        PathPoint* point = heap_[index];
        const float targetDistance = point->distanceToTarget;

        while (index > 0) {
            const int parent = (index - 1) >> 1;
            PathPoint* parentPoint = heap_[parent];
            if (targetDistance >= parentPoint->distanceToTarget) {
                break;
            }

            heap_[index] = parentPoint;
            parentPoint->index = index;
            index = parent;
        }

        heap_[index] = point;
        point->index = index;
    }

    void sortForward(int index) {
        PathPoint* point = heap_[index];
        const float targetDistance = point->distanceToTarget;

        while (true) {
            const int left = 1 + (index << 1);
            const int right = left + 1;
            if (left >= static_cast<int>(heap_.size())) {
                break;
            }

            PathPoint* leftPoint = heap_[left];
            const float leftDistance = leftPoint->distanceToTarget;

            PathPoint* rightPoint = nullptr;
            float rightDistance = std::numeric_limits<float>::infinity();
            if (right < static_cast<int>(heap_.size())) {
                rightPoint = heap_[right];
                rightDistance = rightPoint->distanceToTarget;
            }

            if (leftDistance < rightDistance) {
                if (leftDistance >= targetDistance) {
                    break;
                }
                heap_[index] = leftPoint;
                leftPoint->index = index;
                index = left;
            } else {
                if (rightDistance >= targetDistance) {
                    break;
                }
                heap_[index] = rightPoint;
                rightPoint->index = index;
                index = right;
            }
        }

        heap_[index] = point;
        point->index = index;
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
        path_.clearPath();
        pointMap_.clear();

        PathPoint* start = openPoint(
            MathHelper::floor_double(entity.boundingBox.minX),
            MathHelper::floor_double(entity.boundingBox.minY),
            MathHelper::floor_double(entity.boundingBox.minZ));
        PathPoint* target = openPoint(
            MathHelper::floor_double(x - static_cast<double>(entity.width / 2.0f)),
            MathHelper::floor_double(y),
            MathHelper::floor_double(z - static_cast<double>(entity.width / 2.0f)));
        PathPoint size(
            MathHelper::floor_float(entity.width + 1.0f),
            MathHelper::floor_float(entity.height + 1.0f),
            MathHelper::floor_float(entity.width + 1.0f));

        return addToPath(entity, start, target, size, maxDistance);
    }

private:
    struct TupleHash {
        std::size_t operator()(const std::tuple<int, int, int>& value) const noexcept {
            const auto [x, y, z] = value;
            std::size_t h1 = std::hash<int>{}(x);
            std::size_t h2 = std::hash<int>{}(y);
            std::size_t h3 = std::hash<int>{}(z);
            return h1 ^ (h2 << 1) ^ (h3 << 7);
        }
    };

    World& world_;
    Path path_;
    std::unordered_map<std::tuple<int, int, int>, std::unique_ptr<PathPoint>, TupleHash> pointMap_;
    std::array<PathPoint*, 32> pathOptions_{};

    std::unique_ptr<PathEntity> addToPath(const Entity& entity, PathPoint* start, PathPoint* target,
                                          const PathPoint& size, float maxDistance);
    int findPathOptions(const Entity& entity, PathPoint* current, const PathPoint& size,
                        PathPoint* target, float maxDistance);
    PathPoint* getSafePoint(const Entity& entity, int x, int y, int z, const PathPoint& size, int verticalStep);
    PathPoint* openPoint(int x, int y, int z);
    int getVerticalOffset(const Entity& entity, int x, int y, int z, const PathPoint& size) const;
    std::unique_ptr<PathEntity> createEntityPath(PathPoint* start, PathPoint* end) const;
};

inline std::unique_ptr<PathEntity> Pathfinder::addToPath(const Entity& entity, PathPoint* start, PathPoint* target,
                                                         const PathPoint& size, float maxDistance) {
    start->totalPathDistance = 0.0f;
    start->distanceToNext = start->distanceTo(*target);
    start->distanceToTarget = start->distanceToNext;

    path_.clearPath();
    path_.addPoint(start);

    PathPoint* best = start;
    while (!path_.isPathEmpty()) {
        PathPoint* current = path_.dequeue();
        if (current == target) {
            return createEntityPath(start, target);
        }

        if (current->distanceTo(*target) < best->distanceTo(*target)) {
            best = current;
        }

        current->isFirst = true;
        const int optionCount = findPathOptions(entity, current, size, target, maxDistance);
        for (int i = 0; i < optionCount; ++i) {
            PathPoint* candidate = pathOptions_[i];
            const float pathDistance = current->totalPathDistance + current->distanceTo(*candidate);
            if (candidate->index < 0 || pathDistance < candidate->totalPathDistance) {
                candidate->previous = current;
                candidate->totalPathDistance = pathDistance;
                candidate->distanceToNext = candidate->distanceTo(*target);
                if (candidate->index >= 0) {
                    path_.changeDistance(candidate, candidate->totalPathDistance + candidate->distanceToNext);
                } else {
                    candidate->distanceToTarget = candidate->totalPathDistance + candidate->distanceToNext;
                    path_.addPoint(candidate);
                }
            }
        }
    }

    if (best == start) {
        return nullptr;
    }
    return createEntityPath(start, best);
}

inline int Pathfinder::findPathOptions(const Entity& entity, PathPoint* current, const PathPoint& size,
                                       PathPoint* target, float maxDistance) {
    int optionCount = 0;
    int verticalStep = 0;
    if (getVerticalOffset(entity, current->xCoord, current->yCoord + 1, current->zCoord, size) > 0) {
        verticalStep = 1;
    }

    auto pushCandidate = [&](PathPoint* point) {
        if (point && !point->isFirst && point->distanceTo(*target) < maxDistance) {
            pathOptions_[optionCount++] = point;
        }
    };

    pushCandidate(getSafePoint(entity, current->xCoord, current->yCoord, current->zCoord + 1, size, verticalStep));
    pushCandidate(getSafePoint(entity, current->xCoord - 1, current->yCoord, current->zCoord, size, verticalStep));
    pushCandidate(getSafePoint(entity, current->xCoord + 1, current->yCoord, current->zCoord, size, verticalStep));
    pushCandidate(getSafePoint(entity, current->xCoord, current->yCoord, current->zCoord - 1, size, verticalStep));

    return optionCount;
}

inline PathPoint* Pathfinder::getSafePoint(const Entity& entity, int x, int y, int z, const PathPoint& size,
                                           int verticalStep) {
    PathPoint* safePoint = nullptr;
    if (getVerticalOffset(entity, x, y, z, size) > 0) {
        safePoint = openPoint(x, y, z);
    }

    if (!safePoint && verticalStep > 0 && getVerticalOffset(entity, x, y + verticalStep, z, size) > 0) {
        safePoint = openPoint(x, y + verticalStep, z);
        y += verticalStep;
    }

    if (safePoint) {
        int fallDistance = 0;
        while (y > 0) {
            const int verticalOffset = getVerticalOffset(entity, x, y - 1, z, size);
            if (verticalOffset <= 0) {
                break;
            }
            if (verticalOffset < 0) {
                return nullptr;
            }
            ++fallDistance;
            if (fallDistance >= 4) {
                return nullptr;
            }
            --y;
        }

        if (y > 0) {
            safePoint = openPoint(x, y, z);
        }
    }

    return safePoint;
}

inline PathPoint* Pathfinder::openPoint(int x, int y, int z) {
    const auto key = std::make_tuple(x, y, z);
    auto it = pointMap_.find(key);
    if (it != pointMap_.end()) {
        return it->second.get();
    }

    auto point = std::make_unique<PathPoint>(x, y, z);
    PathPoint* ptr = point.get();
    pointMap_.emplace(key, std::move(point));
    return ptr;
}

inline int Pathfinder::getVerticalOffset(const Entity& entity, int x, int y, int z, const PathPoint& size) const {
    for (int ix = x; ix < x + size.xCoord; ++ix) {
        for (int iy = y; iy < y + size.yCoord; ++iy) {
            for (int iz = z; iz < z + size.zCoord; ++iz) {
                Material* material = world_.getBlockMaterialNoChunkLoad(ix, iy, iz);
                if (!material) {
                    continue;
                }
                if (material->getIsLiquid()) {
                    return -1;
                }
                if (material->blocksMovement()) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

inline std::unique_ptr<PathEntity> Pathfinder::createEntityPath(PathPoint* start, PathPoint* end) const {
    int pathLength = 1;
    for (PathPoint* point = end; point->previous != nullptr; point = point->previous) {
        ++pathLength;
    }

    std::vector<PathPoint> points(pathLength, PathPoint(0, 0, 0));
    PathPoint* current = end;
    for (int index = pathLength - 1; index >= 0; --index) {
        points[index] = PathPoint(current->xCoord, current->yCoord, current->zCoord);
        current = current->previous;
        if (index == 0) {
            break;
        }
    }

    return std::make_unique<PathEntity>(std::move(points));
}
