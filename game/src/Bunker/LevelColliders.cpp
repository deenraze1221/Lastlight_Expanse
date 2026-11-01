#include "Bunker/LevelColliders.h"

#include "Core/Error.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>

namespace lastlight {

ColliderTable loadColliderTable(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) zodar::fail("cannot open collider table '" + file.string() + "'");
    ColliderTable table;
    try {
        const nlohmann::json root = nlohmann::json::parse(in);
        for (const auto& [model, boxes] : root.at("models").items()) {
            std::vector<Aabb>& out = table.models[model];
            for (const auto& b : boxes) {
                if (b.size() != 6) zodar::fail("collider of '" + model + "' needs 6 numbers");
                out.push_back({{b[0].get<float>(), b[1].get<float>(), b[2].get<float>()},
                               {b[3].get<float>(), b[4].get<float>(), b[5].get<float>()}});
            }
        }
    } catch (const nlohmann::json::exception& e) {
        zodar::fail("malformed collider table '" + file.string() + "': " + e.what());
    }
    return table;
}

namespace {

Aabb transformed(const Aabb& box, const glm::mat4& m) {
    Aabb out{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                          (i & 4) ? box.max.z : box.min.z};
        const glm::vec3 p = glm::vec3(m * glm::vec4(c, 1.0f));
        out.min = glm::min(out.min, p);
        out.max = glm::max(out.max, p);
    }
    return out;
}

} // namespace

LevelColliderStats buildLevelColliders(const zodar::SceneDocument& document, const ColliderTable& table,
                                       const ModelBoundsFn& fallback, CollisionWorld& out) {
    LevelColliderStats stats;
    std::unordered_map<std::string, size_t> byId;
    for (size_t i = 0; i < document.entities.size(); ++i) {
        if (!document.entities[i].id.empty()) byId.emplace(document.entities[i].id, i);
    }
    // World matrices in file order (parents may come later: resolve recursively, memoized).
    std::vector<std::optional<glm::mat4>> world(document.entities.size());
    std::function<glm::mat4(size_t, int)> worldOf = [&](size_t i, int depth) -> glm::mat4 {
        if (world[i]) return *world[i];
        const zodar::SceneEntity& e = document.entities[i];
        glm::mat4 m = e.transform ? e.transform->localMatrix() : glm::mat4(1.0f);
        if (!e.parent.empty() && depth < 64) {
            if (const auto p = byId.find(e.parent); p != byId.end()) m = worldOf(p->second, depth + 1) * m;
        }
        world[i] = m;
        return m;
    };
    for (size_t i = 0; i < document.entities.size(); ++i) {
        const zodar::SceneEntity& e = document.entities[i];
        if (!e.modelRenderer || !e.modelRenderer->visible) continue;
        const std::string& model = e.modelRenderer->model;
        const glm::mat4 m = worldOf(i, 0);
        if (const auto it = table.models.find(model); it != table.models.end()) {
            ++stats.fromTable;
            for (const Aabb& b : it->second) {
                out.addStatic(transformed(b, m));
                ++stats.boxes;
            }
        } else if (const std::optional<Aabb> bounds = fallback ? fallback(model) : std::nullopt) {
            ++stats.fromBounds;
            out.addStatic(transformed(*bounds, m));
            ++stats.boxes;
        } else {
            ++stats.skipped;
        }
    }
    return stats;
}

} // namespace lastlight
