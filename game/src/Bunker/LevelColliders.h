#pragma once

#include "Physics/CollisionWorld.h"

#include "SceneFile/SceneFile.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lastlight {

// Collision boxes per model (model-local space), from game/assets/colliders.json (written by
// game/tools/make_greybox_kit.py from the module geometry). An empty list = no collision
// (lights, signs, trays). Temporary until scene files carry Collider components (D6/D7).
struct ColliderTable {
    std::unordered_map<std::string, std::vector<Aabb>> models;
};
// Throws zodar::Error for a missing or malformed file.
ColliderTable loadColliderTable(const std::filesystem::path& file);

struct LevelColliderStats {
    size_t boxes = 0;
    size_t fromTable = 0;  // modelRenderer entities with a table entry
    size_t fromBounds = 0; // ... without one: their model bounds (e.g. a new real export)
    size_t skipped = 0;    // ... without either
};

// Model-local bounds for models missing from the table (nullopt: no collision).
using ModelBoundsFn = std::function<std::optional<Aabb>(const std::string& model)>;

// Adds one world-space box per collider of every modelRenderer entity (world transform from
// the document's parent chain; rotated boxes become their enclosing AABB).
LevelColliderStats buildLevelColliders(const zodar::SceneDocument& document, const ColliderTable& table,
                                       const ModelBoundsFn& fallback, CollisionWorld& out);

} // namespace lastlight
