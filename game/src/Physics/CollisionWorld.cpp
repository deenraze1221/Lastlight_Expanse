#include "Physics/CollisionWorld.h"

#include "Scene/Voxel/VoxelWorld.h"
#include "Core/Error.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lastlight {
namespace {

constexpr float kSkin = 0.001f;

} // namespace

bool CollisionWorld::gather(const Aabb& query, std::vector<Aabb>& boxes) const {
    boxes = m_boxes;
    if (!m_voxels) return true;
    std::vector<zodar::voxel::SolidBox> solids;
    const auto status = m_voxels->collectSolidBoxes({query.min, query.max}, solids);
    for (const auto& solid : solids) boxes.push_back({solid.min, solid.max});
    return status == zodar::voxel::QueryStatus::Complete;
}

float CollisionWorld::sweepAxis(Aabb& box, int axis, float delta) const {
    if (delta == 0.0f) {
        return 0.0f;
    }
    Aabb query = box;
    query.min[axis] += std::min(0.0f, delta);
    query.max[axis] += std::max(0.0f, delta);
    std::vector<Aabb> boxes;
    if (!gather(query, boxes)) return 0.0f;
    float allowed = delta;
    for (const Aabb& s : boxes) {
        if (s.overlaps(box)) {
            continue; // already inside (e.g. spawned in a wall): never trap the mover
        }
        // Overlap on the two other axes is required for a contact along this axis.
        bool across = true;
        for (int a = 0; a < 3; ++a) {
            if (a != axis && !(box.min[a] < s.max[a] && box.max[a] > s.min[a])) {
                across = false;
            }
        }
        if (!across) {
            continue;
        }
        if (delta > 0.0f && s.min[axis] >= box.max[axis]) {
            allowed = std::min(allowed, std::max(0.0f, s.min[axis] - box.max[axis] - kSkin));
        } else if (delta < 0.0f && s.max[axis] <= box.min[axis]) {
            allowed = std::max(allowed, std::min(0.0f, s.max[axis] - box.min[axis] + kSkin));
        }
    }
    box.min[axis] += allowed;
    box.max[axis] += allowed;
    return allowed;
}

CollisionWorld::SweepResult CollisionWorld::sweep(const Aabb& start, const glm::vec3& delta) const {
    SweepResult r;
    Aabb box = start;
    for (const int axis : {1, 0, 2}) {
        const float moved = sweepAxis(box, axis, delta[axis]);
        r.moved[axis] = moved;
        const bool hit = std::abs(moved - delta[axis]) > 1e-6f;
        if (axis == 0) r.hitX = hit;
        if (axis == 1) r.hitY = hit;
        if (axis == 2) r.hitZ = hit;
    }
    return r;
}

std::optional<CollisionWorld::RayHit> CollisionWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                                              float maxDistance) const {
    const float length = glm::length(direction);
    if (length <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 dir = direction / length;
    std::optional<RayHit> best;
    for (const Aabb& s : m_boxes) {
        float tNear = 0.0f, tFar = maxDistance;
        int nearAxis = -1;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            if (std::abs(dir[a]) < 1e-9f) {
                miss = origin[a] < s.min[a] || origin[a] > s.max[a];
                continue;
            }
            float t0 = (s.min[a] - origin[a]) / dir[a];
            float t1 = (s.max[a] - origin[a]) / dir[a];
            if (t0 > t1) std::swap(t0, t1);
            if (t0 > tNear) {
                tNear = t0;
                nearAxis = a;
            }
            tFar = std::min(tFar, t1);
            miss = tNear > tFar;
        }
        if (miss || nearAxis < 0 || (best && tNear >= best->distance)) {
            continue; // nearAxis < 0: the origin is inside the box
        }
        RayHit hit;
        hit.distance = tNear;
        hit.point = origin + dir * tNear;
        hit.normal[nearAxis] = dir[nearAxis] > 0.0f ? -1.0f : 1.0f;
        best = hit;
    }
    if (m_voxels) {
        const glm::vec3 end = origin + dir * maxDistance;
        std::vector<Aabb> coverage;
        ZODAR_ENSURE(gather({glm::min(origin, end), glm::max(origin, end)}, coverage),
                     "collision ray touches missing voxel chunks");
        const auto hit = m_voxels->raycast(origin, dir, maxDistance,
                                          {zodar::voxel::RaycastMode::Collision});
        if (hit && (!best || hit->distance < best->distance))
            best = RayHit{hit->distance, hit->point, hit->surfaceNormal};
    }
    return best;
}

bool CollisionWorld::overlaps(const Aabb& box) const {
    std::vector<Aabb> boxes;
    if (!gather(box, boxes)) return true;
    return std::ranges::any_of(boxes, [&](const Aabb& s) { return s.overlaps(box); });
}

} // namespace lastlight
