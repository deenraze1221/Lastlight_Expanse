#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace zodar::voxel { class VoxelWorld; }

namespace lastlight {

// Axis-aligned box in world space (metres).
struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    bool overlaps(const Aabb& o) const {
        return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z &&
               max.z > o.min.z;
    }
};

// Temporary static collision for the bunker (approved Phase D decision 2): game-side
// axis-aligned boxes behind this small interface, so that Phase E can replace the
// implementation with Jolt without changing its users (exploration controller now, the D7
// player controller and interaction raycasts later). Static geometry only.
class CollisionWorld {
public:
    struct SweepResult {
        glm::vec3 moved{0.0f};   // movement actually applied (delta, clipped per axis)
        bool hitX = false, hitY = false, hitZ = false;
        bool grounded() const { return hitY; } // only meaningful for downward moves
    };
    struct RayHit {
        float distance = 0.0f;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f};
    };

    // Non-owning; voxel world must outlive this adapter. Missing data blocks movement.
    void setVoxelWorld(const zodar::voxel::VoxelWorld* world) { m_voxels = world; }
    void addStatic(const Aabb& box) { m_boxes.push_back(box); }
    void clear() { m_boxes.clear(); }
    size_t size() const { return m_boxes.size(); }
    const std::vector<Aabb>& boxes() const { return m_boxes; }

    // Moves `box` by `delta` one axis at a time (Y, then X, then Z) and stops each axis at the
    // first static box it would enter (a small skin keeps resting contacts stable). Boxes the
    // moving box already overlaps are ignored, so it can always move out of them.
    SweepResult sweep(const Aabb& box, const glm::vec3& delta) const;
    // Nearest static box hit along the ray within maxDistance (direction need not be unit).
    std::optional<RayHit> raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;
    bool overlaps(const Aabb& box) const;

private:
    float sweepAxis(Aabb& box, int axis, float delta) const;

    bool gather(const Aabb& query, std::vector<Aabb>& boxes) const;
    const zodar::voxel::VoxelWorld* m_voxels = nullptr;
    std::vector<Aabb> m_boxes;
};

} // namespace lastlight
