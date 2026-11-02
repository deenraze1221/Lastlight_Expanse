#include "Bunker/BunkerVoxels.h"
#include "Bunker/LevelColliders.h"
#include "Assets/ModelData.h"
#include "SceneFile/SceneFile.h"
#include "Scene/Voxel/Structure.h"
#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace {
const std::filesystem::path assets = LASTLIGHT_ASSETS_DIR;
const zodar::SceneEntity& entity(const zodar::SceneDocument& doc, std::string_view id) {
    for (const auto& e : doc.entities) if (e.id == id) return e;
    throw std::runtime_error("missing test entity: " + std::string(id));
}
glm::mat4 matrix(const zodar::SceneDocument& doc, const zodar::SceneEntity& e) {
    const auto local = e.transform ? e.transform->localMatrix() : glm::mat4(1);
    return e.parent.empty() ? local : matrix(doc, entity(doc, e.parent)) * local;
}
lastlight::Aabb transformed(const lastlight::Aabb& box, const glm::mat4& m) {
    lastlight::Aabb out{glm::vec3(std::numeric_limits<float>::max()),
                       glm::vec3(std::numeric_limits<float>::lowest())};
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p = glm::vec3(m * glm::vec4(i & 1 ? box.max.x : box.min.x,
            i & 2 ? box.max.y : box.min.y, i & 4 ? box.max.z : box.min.z, 1));
        out.min = glm::min(out.min, p); out.max = glm::max(out.max, p);
    }
    return out;
}
}

TEST_CASE("bunker geometry: mounted fixtures are visible and outside solid voxels") {
    lastlight::BunkerVoxels bunker(assets);
    lastlight::CollisionWorld collision;
    collision.setVoxelWorld(bunker.world.get());
    const auto doc = zodar::loadSceneFile(assets / "levels/bunker.scene.json");
    for (const auto* id : {"corridor/exit_sign_5", "dormitory/vent_1", "airlock/emergency_light_1"}) {
        CAPTURE(std::string(id));
        const auto& e = entity(doc, id);
        const auto model = zodar::loadGltf(assets / "models" / (e.modelRenderer->model + ".glb"));
        const auto m = matrix(doc, e);
        const lastlight::Aabb local{model.bounds.min, model.bounds.max};
        CHECK_FALSE(collision.overlaps(transformed(local, m)));
        // A viewer in front must see the fixture before the supporting wall. The wall
        // must still be immediately behind it, rather than the fixture floating in space.
        const glm::vec3 normal = glm::vec3(m * glm::vec4(0, 0, 1, 0));
        glm::vec3 front = (local.min + local.max) * 0.5f;
        front.z = local.max.z;
        const auto viewer = glm::vec3(m * glm::vec4(front, 1)) + normal * 0.2f;
        const auto hit = collision.raycast(viewer, -normal, 1.0f);
        REQUIRE(hit);
        CHECK(hit->distance > 0.2f + local.max.z - local.min.z);
        CHECK(hit->distance < 0.2f + local.max.z - local.min.z + 0.02f);
    }
}

TEST_CASE("bunker geometry: frame dimensions match clear voxel apertures") {
    lastlight::BunkerVoxels bunker(assets);
    lastlight::CollisionWorld collision;
    collision.setVoxelWorld(bunker.world.get());
    const auto doc = zodar::loadSceneFile(assets / "levels/bunker.scene.json");
    const auto table = lastlight::loadColliderTable(assets / "colliders.json");
    int count = 0;
    for (const auto& e : doc.entities) {
        if (!e.modelRenderer) continue;
        const auto& name = e.modelRenderer->model;
        if (name != "door_frame" && name != "wide_frame") continue;
        CAPTURE(e.id);
        const auto& boxes = table.models.at(name);
        REQUIRE(boxes.size() == 3);
        const auto& header = boxes.back();
        const float half = boxes[1].min.x;
        const float height = header.min.y;
        CHECK(height == doctest::Approx(3.0f));
        CHECK(half * 2 == doctest::Approx(name == "door_frame" ? 2.0f : 3.0f));
        const auto m = matrix(doc, e);
        // Include the wall's full depth; also prove there is solid support directly
        // above the lintel and beside both jambs, not an oversized empty opening.
        CHECK_FALSE(collision.overlaps(transformed({{-half + .01f, .01f, .01f},
                                                   {half - .01f, height - .01f, .49f}}, m)));
        for (glm::vec3 p : std::array<glm::vec3, 3>{{{0, height + .01f, .25f},
                                                   {-half - .01f, 1, .25f}, {half + .01f, 1, .25f}}}) {
            p = glm::vec3(m * glm::vec4(p, 1));
            CHECK(bunker.world->getVoxel(glm::ivec3(glm::floor(p * 2.0f))) != 0);
        }
        ++count;
    }
    CHECK(count == 11);
}

TEST_CASE("bunker geometry: every structure cell is loaded and stairs stay at the exit") {
    lastlight::BunkerVoxels bunker(assets);
    std::ifstream in(assets / "structures/bunker.zstruct");
    const std::string bytes{std::istreambuf_iterator<char>(in), {}};
    const auto structure = zodar::voxel::parseStructure(bytes);
    bool loaded = true;
    size_t stairs = 0;
    for (const auto& [p, name] : structure.cells) {
        const auto v = glm::ivec3(p.x, p.y, p.z) - structure.origin;
        loaded &= bunker.world->hasChunkData(zodar::voxel::chunkOf(v));
        if (name != "stairs@3") continue;
        CHECK(v.x >= -31); CHECK(v.x <= -25);
        CHECK(v.z >= -4); CHECK(v.z < 4);
        CHECK(v.y == (-25 - v.x) / 2);
        ++stairs;
    }
    CHECK(loaded);
    CHECK(stairs == 32);
    CHECK(bunker.world->getVoxel({-25, 0, 0}) == *bunker.catalogue.find("stairs@3"));
    CHECK(bunker.world->getVoxel({-30, 2, 0}) == *bunker.catalogue.find("floor"));
}

#include "Core/Error.h"

TEST_CASE("bunker geometry: oversized structures fail before streaming") {
    const auto root = std::filesystem::temp_directory_path() / "lastlight-bunker-bounds-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "blocks");
    std::filesystem::create_directories(root / "structures");
    std::filesystem::create_directories(root / "textures");
    std::filesystem::copy_file(assets / "blocks/bunker_blocks.blocks.json", root / "blocks/bunker_blocks.blocks.json");
    std::filesystem::copy(assets / "textures/blocks", root / "textures/blocks", std::filesystem::copy_options::recursive);
    std::ifstream in(assets / "structures/bunker.zstruct");
    const std::string bytes{std::istreambuf_iterator<char>(in), {}};
    const auto original = zodar::voxel::parseStructure(bytes);
    auto write = [&](const auto& structure) {
        std::ofstream out(root / "structures/bunker.zstruct");
        out << zodar::voxel::serializeStructure(structure);
    };
    auto structure = original;
    structure.size.x = 1024;
    write(structure);
    CHECK_THROWS_WITH_AS((void)lastlight::BunkerVoxels{root},
        doctest::Contains("exceeds preloaded mesh radius"), zodar::Error);
    structure = original;
    structure.size.y = 100;
    write(structure);
    CHECK_THROWS_WITH_AS((void)lastlight::BunkerVoxels{root},
        doctest::Contains("exceeds preloaded vertical bounds"), zodar::Error);
    std::filesystem::remove_all(root);
}
