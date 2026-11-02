// The real bunker level: colliders from colliders.json + levels/bunker.scene.json, a walk from
// the entrance through every room and corridor (doorways wide enough, no blocking geometry,
// never falling through the floor).
#include "Bunker/LevelColliders.h"
#include "Bunker/BunkerVoxels.h"
#include "Player/ExplorationWalker.h"

#include "SceneFile/SceneFile.h"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace lastlight;

namespace {

const std::filesystem::path kAssets = LASTLIGHT_ASSETS_DIR;

struct Level {
    zodar::SceneDocument doc = zodar::loadSceneFile(kAssets / "levels/bunker.scene.json");
    ColliderTable table = loadColliderTable(kAssets / "colliders.json");
    BunkerVoxels voxels{kAssets};
    CollisionWorld world;
    LevelColliderStats stats = buildLevelColliders(doc, table, nullptr, world);
    Level() { world.setVoxelWorld(voxels.world.get()); }
};

glm::vec3 spawnPoint(const zodar::SceneDocument& doc) {
    for (const auto& e : doc.entities) {
        if (e.id == "player_start") return e.transform->position - glm::vec3(0, 1.65f, 0);
    }
    FAIL("no player_start");
    return {};
}

// Steers straight to (x, z); returns true when within 0.25 m before the timeout.
bool walkTo(ExplorationWalker& walker, const CollisionWorld& world, float x, float z, float& minY) {
    for (int i = 0; i < 60 * 25; ++i) {
        const glm::vec2 d(x - walker.feet().x, z - walker.feet().z);
        if (glm::length(d) < 0.25f) return true;
        WalkInput input;
        input.forward = 1.0f;
        input.yaw = std::atan2(-d.x, -d.y);
        walker.step(world, input, 1.0f / 60.0f);
        minY = std::min(minY, walker.feet().y);
    }
    return false;
}

} // namespace

TEST_CASE("bunker: every modelRenderer has collision data; the entrance spawn stands on the floor") {
    Level level;
    CHECK(level.stats.skipped == 0);
    CHECK(level.stats.fromBounds == 0);
    CHECK(level.stats.boxes > 100);
    ExplorationWalker walker;
    walker.spawn(spawnPoint(level.doc));
    CHECK_FALSE(level.world.overlaps(walker.box()));
    for (int i = 0; i < 60; ++i) walker.step(level.world, {}, 1.0f / 60.0f);
    CHECK(walker.grounded());
    CHECK(walker.feet().y == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("bunker: walk from the entrance through every room and corridor") {
    Level level;
    ExplorationWalker walker;
    walker.spawn(spawnPoint(level.doc));
    float minY = 0.0f;
    struct Waypoint {
        const char* what;
        float x, z;
    };
    const std::vector<Waypoint> route = {
        {"up the exit stairs", -15.0f, 0.0f}, {"back down", -10.5f, 0.0f},
        {"blast-door arch", -8.0f, 0.0f}, {"airlock", -4.0f, 0.0f}, {"airlock door", 0.0f, 0.0f},
        {"corridor west", 2.0f, 0.0f},
        {"dormitory door", 6.0f, 0.0f}, {"dormitory", 6.0f, 3.5f}, {"dormitory aisle", 3.5f, 7.0f},
        {"dormitory door", 6.0f, 3.5f}, {"corridor", 6.0f, 0.0f},
        {"weapon dealer", 6.0f, -3.5f}, {"weapon dealer counter", 6.0f, -5.0f}, {"corridor", 6.0f, 0.0f},
        {"locker room door", 14.0f, 0.0f}, {"locker room", 14.0f, 3.5f}, {"locker room aisle", 13.0f, 6.3f},
        {"locker room door", 14.0f, 3.5f}, {"corridor", 14.0f, 0.0f},
        {"medic", 18.0f, 0.0f}, {"medic area", 18.0f, -3.5f}, {"medic counter", 18.0f, -5.0f}, {"corridor", 18.0f, 0.0f},
        {"common area west opening", 26.0f, 0.0f}, {"common area", 26.0f, 3.5f}, {"common area tables", 28.5f, 6.0f},
        {"common area centre", 32.0f, 8.25f}, {"common area east", 38.0f, 3.5f}, {"corridor", 38.0f, 0.0f},
        {"resource trader", 30.0f, 0.0f}, {"resource trader area", 30.0f, -5.0f}, {"corridor", 30.0f, 0.0f},
        {"technician", 42.0f, 0.0f}, {"technician area", 42.0f, -5.0f}, {"corridor", 42.0f, 0.0f},
        {"generator room door", 50.0f, 0.0f}, {"generator room", 50.0f, 3.5f}, {"between generators", 53.5f, 5.0f},
        {"generator room door", 50.0f, 3.5f}, {"corridor", 50.0f, 0.0f},
        {"scout", 54.0f, 0.0f}, {"scout area", 54.0f, -5.0f}, {"corridor", 54.0f, 0.0f},
        {"corridor east end", 58.0f, 0.0f},
    };
    for (const Waypoint& w : route) {
        CAPTURE(w.what);
        CHECK(walkTo(walker, level.world, w.x, w.z, minY));
    }
    CHECK(minY > -0.01f);
    CHECK_FALSE(level.world.overlaps(walker.box()));
}
