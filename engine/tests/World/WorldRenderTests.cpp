#include "WorldRender/WorldRender.h"

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <map>
#include <vector>

using namespace zodar;

namespace {

// The bridge never dereferences meshes; distinct addresses are enough for CPU tests.
const gfx::GpuMesh* fakeMesh(int i) {
    static int storage[4];
    return reinterpret_cast<const gfx::GpuMesh*>(&storage[i]);
}

struct FakeSink final : RenderInstanceSink {
    struct Instance {
        const gfx::GpuMesh* mesh;
        uint32_t submesh;
        gfx::MaterialHandle material;
        glm::mat4 transform;
    };
    std::map<gfx::InstanceHandle, Instance> live;
    gfx::InstanceHandle next = 0;
    int transformCalls = 0;
    int materialCalls = 0;

    gfx::InstanceHandle add(const gfx::GpuMesh& mesh, uint32_t submesh, gfx::MaterialHandle material,
                            const glm::mat4& transform) override {
        live[next] = {&mesh, submesh, material, transform};
        return next++;
    }
    void setTransform(gfx::InstanceHandle i, const glm::mat4& t) override {
        REQUIRE(live.count(i) == 1);
        live[i].transform = t;
        ++transformCalls;
    }
    void setMaterial(gfx::InstanceHandle i, gfx::MaterialHandle m) override {
        REQUIRE(live.count(i) == 1);
        live[i].material = m;
        ++materialCalls;
    }
    void remove(gfx::InstanceHandle i) override { REQUIRE(live.erase(i) == 1); }
};

Transform at(glm::vec3 p) {
    Transform t;
    t.position = p;
    return t;
}

} // namespace

TEST_CASE("world render: collectDraws uses world transforms and skips non-renderables") {
    World world;
    const Entity parent = world.create();
    world.add<Transform>(parent, at({1, 0, 0}));
    world.add<MeshRenderer>(parent, MeshRenderer{fakeMesh(0), 0, 3});
    const Entity child = world.create();
    world.add<Transform>(child, at({0, 2, 0}));
    world.add<MeshRenderer>(child, MeshRenderer{fakeMesh(1), 1, 4});
    world.setParent(child, parent);
    const Entity hidden = world.create();
    world.add<MeshRenderer>(hidden, MeshRenderer{fakeMesh(0), 0, 3, false});
    const Entity noMesh = world.create();
    world.add<MeshRenderer>(noMesh);
    world.add<Transform>(world.create(), at({5, 5, 5})); // Transform only: not rendered.

    std::vector<gfx::MeshDraw> draws;
    collectDraws(world, draws);
    REQUIRE(draws.size() == 2);
    // Order is the World's storage order (unspecified here); look draws up by mesh.
    const gfx::MeshDraw& p = draws[0].mesh == fakeMesh(0) ? draws[0] : draws[1];
    const gfx::MeshDraw& c = draws[0].mesh == fakeMesh(0) ? draws[1] : draws[0];
    CHECK(p.mesh == fakeMesh(0));
    CHECK(p.material == 3);
    CHECK(p.transform == glm::translate(glm::mat4(1), {1, 0, 0}));
    CHECK(c.mesh == fakeMesh(1));
    CHECK(c.submesh == 1);
    CHECK(c.material == 4);
    CHECK(c.transform == glm::translate(glm::mat4(1), {1, 2, 0}));
}

TEST_CASE("world render: sync adds, updates and removes instances incrementally") {
    World world;
    FakeSink sink;
    WorldRenderSync sync(sink);

    const Entity a = world.create();
    world.add<Transform>(a, at({1, 0, 0}));
    world.add<MeshRenderer>(a, MeshRenderer{fakeMesh(0), 0, 1});
    const Entity b = world.create();
    world.add<MeshRenderer>(b, MeshRenderer{fakeMesh(1), 0, 2});
    world.setParent(b, a);

    WorldRenderSync::Stats s = sync.sync(world);
    CHECK(s.added == 2);
    CHECK(s.updated == 0);
    CHECK(s.removed == 0);
    REQUIRE(sink.live.size() == 2);
    CHECK(sink.live.at(sync.instanceOf(b)).transform == glm::translate(glm::mat4(1), {1, 0, 0}));

    SUBCASE("unchanged world: no sink calls") {
        s = sync.sync(world);
        CHECK(s.added + s.updated + s.removed == 0);
        CHECK(sink.transformCalls == 0);
        CHECK(sink.materialCalls == 0);
    }
    SUBCASE("moving a parent updates its descendants") {
        world.get<Transform>(a).position = {0, 0, 3};
        s = sync.sync(world);
        CHECK(s.updated == 2);
        CHECK(sink.transformCalls == 2);
        CHECK(sink.live.at(sync.instanceOf(b)).transform == glm::translate(glm::mat4(1), {0, 0, 3}));
    }
    SUBCASE("material change updates in place, mesh change re-creates") {
        const gfx::InstanceHandle before = sync.instanceOf(a);
        world.get<MeshRenderer>(a).material = 7;
        world.get<MeshRenderer>(b).mesh = fakeMesh(2);
        s = sync.sync(world);
        CHECK(s.updated == 1);
        CHECK(s.added == 1);
        CHECK(s.removed == 1);
        CHECK(sync.instanceOf(a) == before);
        CHECK(sink.live.at(before).material == 7);
        CHECK(sink.live.at(sync.instanceOf(b)).mesh == fakeMesh(2));
        CHECK(sink.live.size() == 2);
    }
    SUBCASE("hiding, removing the component and destroying remove instances") {
        world.get<MeshRenderer>(a).visible = false;
        s = sync.sync(world);
        CHECK(s.removed == 1);
        CHECK(sync.instanceOf(a) == gfx::kInvalidInstance);
        world.get<MeshRenderer>(a).visible = true;
        CHECK(sync.sync(world).added == 1);

        world.remove<MeshRenderer>(b);
        CHECK(sync.sync(world).removed == 1);
        world.destroy(a);
        CHECK(sync.sync(world).removed == 1);
        CHECK(sink.live.empty());
        CHECK(sync.instanceCount() == 0);
    }
    SUBCASE("destroying a parent removes the whole subtree; clear empties the sink") {
        const Entity c = world.create();
        world.add<MeshRenderer>(c, MeshRenderer{fakeMesh(3), 0, 1});
        CHECK(sync.sync(world).added == 1);
        world.destroy(a);
        CHECK(sync.sync(world).removed == 2);
        CHECK(sink.live.size() == 1);
        sync.clear();
        CHECK(sink.live.empty());
        CHECK(sync.instanceCount() == 0);
    }
}

TEST_CASE("world render: a recycled entity id gets a fresh instance") {
    World world;
    FakeSink sink;
    WorldRenderSync sync(sink);
    const Entity a = world.create();
    world.add<MeshRenderer>(a, MeshRenderer{fakeMesh(0), 0, 1});
    sync.sync(world);
    world.destroy(a);
    const Entity reused = world.create();
    world.add<MeshRenderer>(reused, MeshRenderer{fakeMesh(1), 0, 2});
    const WorldRenderSync::Stats s = sync.sync(world);
    CHECK(s.added == 1);
    CHECK(s.removed == 1);
    REQUIRE(sink.live.size() == 1);
    CHECK(sink.live.begin()->second.mesh == fakeMesh(1));
}
