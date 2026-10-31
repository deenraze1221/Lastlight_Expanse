#include <doctest/doctest.h>

#include "World/World.h"

#include <stdexcept>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

using zodar::Entity;
using zodar::Name;
using zodar::Transform;
using zodar::World;

namespace {
bool near(const glm::vec3& a, const glm::vec3& b) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3{1e-5f})); }
bool matNear(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c) {
        if (!glm::all(glm::lessThan(glm::abs(a[c] - b[c]), glm::vec4{1e-5f}))) {
            return false;
        }
    }
    return true;
}
glm::vec3 apply(const glm::mat4& m, const glm::vec3& p) { return glm::vec3{m * glm::vec4{p, 1.0f}}; }
struct Health {
    int value = 0;
};
} // namespace

TEST_CASE("World components: add, get, tryGet, has, remove") {
    World world;
    const Entity e = world.create();
    CHECK_FALSE(world.has<Name>(e));
    CHECK(world.tryGet<Name>(e) == nullptr);
    CHECK_THROWS_AS((void)world.get<Name>(e), std::out_of_range);

    world.add<Name>(e, "player");
    CHECK(world.has<Name>(e));
    CHECK(world.get<Name>(e).value == "player");
    world.get<Name>(e).value = "hero";
    REQUIRE(world.tryGet<Name>(e) != nullptr);
    CHECK(world.tryGet<Name>(e)->value == "hero");

    // add replaces an existing component.
    world.add<Health>(e, 5);
    world.add<Health>(e, 9);
    CHECK(world.get<Health>(e).value == 9);

    const World& cworld = world;
    CHECK(cworld.get<Health>(e).value == 9);
    CHECK(cworld.tryGet<Transform>(e) == nullptr);

    CHECK(world.remove<Health>(e));
    CHECK_FALSE(world.remove<Health>(e));
    CHECK_FALSE(world.has<Health>(e));
    CHECK(world.has<Name>(e));
}

TEST_CASE("World components: dead and null entities are rejected") {
    World world;
    const Entity e = world.create();
    world.add<Name>(e, "x");
    world.destroy(e);
    CHECK_FALSE(world.has<Name>(e));
    CHECK(world.tryGet<Name>(e) == nullptr);
    CHECK_FALSE(world.remove<Name>(e));
    CHECK_THROWS_AS((void)world.get<Name>(e), std::out_of_range);
    CHECK_THROWS_AS(world.add<Name>(e), std::invalid_argument);
    CHECK_THROWS_AS(world.add<Name>(Entity::null()), std::invalid_argument);

    // A recycled index does not inherit components.
    const Entity reused = world.create();
    CHECK_FALSE(world.has<Name>(reused));
}

TEST_CASE("Transform: defaults and local matrix (T * R * S)") {
    Transform t;
    CHECK(t.localMatrix() == glm::mat4{1.0f});

    t.position = {1.0f, 2.0f, 3.0f};
    t.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    t.scale = glm::vec3{2.0f};
    // (1,0,0) -> scale (2,0,0) -> rotate 90 deg about Z (0,2,0) -> translate (1,4,3).
    CHECK(near(apply(t.localMatrix(), {1.0f, 0.0f, 0.0f}), {1.0f, 4.0f, 3.0f}));
}

TEST_CASE("World hierarchy: setParent, parent, children") {
    World world;
    const Entity root = world.create();
    const Entity a = world.create();
    const Entity b = world.create();
    CHECK(world.parent(a).isNull());
    CHECK(world.children(root).empty());

    CHECK(world.setParent(a, root));
    CHECK(world.setParent(b, root));
    CHECK(world.setParent(b, root)); // no-op
    CHECK(world.parent(a) == root);
    REQUIRE(world.children(root).size() == 2);
    CHECK(world.children(root)[0] == a);
    CHECK(world.children(root)[1] == b);
    CHECK(world.isDescendantOf(a, root));
    CHECK_FALSE(world.isDescendantOf(root, a));

    // Reparent b under a.
    CHECK(world.setParent(b, a));
    CHECK(world.children(root).size() == 1);
    CHECK(world.children(a).size() == 1);
    CHECK(world.isDescendantOf(b, root));

    // Detach.
    CHECK(world.setParent(b, Entity::null()));
    CHECK(world.parent(b).isNull());
    CHECK(world.children(a).empty());
}

TEST_CASE("World hierarchy: cycles, self and dead entities are rejected") {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();
    REQUIRE(world.setParent(b, a));
    REQUIRE(world.setParent(c, b));

    CHECK_FALSE(world.setParent(a, a));
    CHECK_FALSE(world.setParent(a, c)); // c is a descendant of a
    CHECK_FALSE(world.setParent(a, b));
    CHECK(world.parent(a).isNull());

    const Entity dead = world.create();
    world.destroy(dead);
    CHECK_FALSE(world.setParent(dead, a));
    CHECK_FALSE(world.setParent(a, dead));
    CHECK(world.setParent(a, Entity::null())); // already a root: no-op
    CHECK(world.children(dead).empty());
    CHECK(world.parent(dead).isNull());
}

TEST_CASE("World hierarchy: destroy removes the subtree and detaches from the parent") {
    World world;
    const Entity root = world.create();
    const Entity a = world.create();
    const Entity a1 = world.create();
    const Entity a2 = world.create();
    const Entity b = world.create();
    world.setParent(a, root);
    world.setParent(b, root);
    world.setParent(a1, a);
    world.setParent(a2, a1);

    CHECK(world.destroy(a));
    CHECK_FALSE(world.isAlive(a));
    CHECK_FALSE(world.isAlive(a1));
    CHECK_FALSE(world.isAlive(a2));
    CHECK(world.isAlive(b));
    REQUIRE(world.children(root).size() == 1);
    CHECK(world.children(root)[0] == b);
    CHECK(world.entityCount() == 2);

    CHECK(world.destroy(root));
    CHECK(world.entityCount() == 0);
}

TEST_CASE("World transforms: world matrix composes parents") {
    World world;
    const Entity root = world.create();
    const Entity mid = world.create(); // no Transform: identity
    const Entity leaf = world.create();
    world.add<Transform>(root).position = {10.0f, 0.0f, 0.0f};
    world.get<Transform>(root).rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    world.add<Transform>(leaf).position = {0.0f, 0.0f, 1.0f};
    world.get<Transform>(leaf).scale = glm::vec3{3.0f};
    world.setParent(mid, root);
    world.setParent(leaf, mid);

    CHECK(world.localMatrix(mid) == glm::mat4{1.0f});
    CHECK(world.localMatrix(leaf) == world.get<Transform>(leaf).localMatrix());
    // Leaf origin: (0,0,1) rotated 90 deg about Y -> (1,0,0), + (10,0,0) = (11,0,0).
    const glm::mat4 m = world.worldMatrix(leaf);
    CHECK(near(apply(m, glm::vec3{0.0f}), {11.0f, 0.0f, 0.0f}));
    CHECK(matNear(m, world.worldMatrix(mid) * world.localMatrix(leaf)));

    // Detached: world == local. Dead: identity.
    world.setParent(leaf, Entity::null());
    CHECK(world.worldMatrix(leaf) == world.localMatrix(leaf));
    world.destroy(leaf);
    CHECK(world.worldMatrix(leaf) == glm::mat4{1.0f});
}
