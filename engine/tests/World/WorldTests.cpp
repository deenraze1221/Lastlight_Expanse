#include <doctest/doctest.h>

#include "World/World.h"

#include <unordered_set>
#include <utility>
#include <vector>

using zodar::Entity;
using zodar::World;

TEST_CASE("World: null entity is never alive") {
    World world;
    CHECK(Entity{}.isNull());
    CHECK(Entity::null() == Entity{});
    CHECK_FALSE(static_cast<bool>(Entity::null()));
    CHECK_FALSE(world.isAlive(Entity::null()));
    CHECK_FALSE(world.destroy(Entity::null()));
    CHECK(world.entityCount() == 0);
}

TEST_CASE("World: create returns distinct live entities") {
    World world;
    std::unordered_set<Entity::Id> ids;
    for (int i = 0; i < 100; ++i) {
        const Entity e = world.create();
        CHECK_FALSE(e.isNull());
        CHECK(world.isAlive(e));
        ids.insert(e.id());
    }
    CHECK(ids.size() == 100);
    CHECK(world.entityCount() == 100);
}

TEST_CASE("World: destroy invalidates the handle, double destroy is rejected") {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    CHECK(world.destroy(a));
    CHECK_FALSE(world.isAlive(a));
    CHECK(world.isAlive(b));
    CHECK(world.entityCount() == 1);
    CHECK_FALSE(world.destroy(a));
    CHECK(world.entityCount() == 1);
}

TEST_CASE("World: recycled index does not revive a stale handle") {
    World world;
    const Entity stale = world.create();
    REQUIRE(world.destroy(stale));
    const Entity fresh = world.create();
    CHECK(fresh != stale);
    CHECK(world.isAlive(fresh));
    CHECK_FALSE(world.isAlive(stale));
    CHECK_FALSE(world.destroy(stale));
    CHECK(world.isAlive(fresh));
}

TEST_CASE("World: clear destroys all entities") {
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(world.create());
    }
    world.clear();
    CHECK(world.entityCount() == 0);
    for (const Entity e : entities) {
        CHECK_FALSE(world.isAlive(e));
    }
    CHECK(world.isAlive(world.create()));
    CHECK(world.entityCount() == 1);
}

TEST_CASE("World: handles from another world are not alive here") {
    World a;
    World b;
    const Entity inA = a.create();
    (void)a.create();
    CHECK_FALSE(b.isAlive(inA));
    CHECK_FALSE(b.destroy(inA));
    CHECK(a.isAlive(inA));
}

TEST_CASE("World: move keeps entities") {
    World world;
    const Entity e = world.create();
    World moved = std::move(world);
    CHECK(moved.isAlive(e));
    CHECK(moved.entityCount() == 1);
}
