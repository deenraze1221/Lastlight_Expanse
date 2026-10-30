#include <doctest/doctest.h>

#include "Assets/MeshData.h"
#include "Assets/Primitives.h"

using namespace raven;

namespace {

// Every triangle's geometric normal must agree with its vertex normals (CCW = front).
void checkWinding(const MeshData& mesh) {
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[i]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        REQUIRE(glm::dot(n, a.normal + b.normal + c.normal) > 0.0f);
    }
}

} // namespace

TEST_CASE("primitives: cube shape, winding, bounds and tangents") {
    const MeshData cube = primitives::cube(2.0f);
    CHECK(cube.validate().empty());
    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);
    CHECK(cube.bounds.min == glm::vec3(-1.0f));
    CHECK(cube.bounds.max == glm::vec3(1.0f));
    checkWinding(cube);
    for (const auto& v : cube.vertices) {
        CHECK(glm::dot(glm::vec3(v.tangent), v.normal) == doctest::Approx(0.0f));
        CHECK(glm::length(glm::vec3(v.tangent)) == doctest::Approx(1.0f));
    }
}

TEST_CASE("primitives: plane and sphere") {
    const MeshData plane = primitives::plane(4.0f, 2.0f, 3, 2.0f);
    CHECK(plane.validate().empty());
    CHECK(plane.vertices.size() == 16);
    CHECK(plane.indices.size() == 54);
    CHECK(plane.bounds.extent().x == doctest::Approx(4.0f));
    CHECK(plane.vertices.back().uv0 == glm::vec2(2.0f));
    checkWinding(plane);

    const MeshData sphere = primitives::uvSphere(1.0f, 16, 8);
    CHECK(sphere.validate().empty());
    CHECK(sphere.bounds.max.y == doctest::Approx(1.0f));
    checkWinding(sphere);
    CHECK(sphere.indices.size() == 16 * (8 - 1) * 6); // Pole rows emit one triangle per segment.
}

TEST_CASE("mesh: generated normals and validation") {
    MeshData mesh;
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {1, 0, 0};
    mesh.vertices[2].position = {0, 1, 0};
    mesh.indices = {0, 1, 2};
    mesh.submeshes = {{0, 3, 0, {}}};
    mesh.generateNormals();
    CHECK(mesh.vertices[0].normal == glm::vec3(0, 0, 1));
    mesh.computeBounds();
    CHECK(mesh.submeshes[0].bounds.max == glm::vec3(1, 1, 0));

    mesh.indices = {0, 1, 5};
    CHECK(mesh.validate() == "index out of range");
    mesh.indices = {0, 1};
    CHECK_FALSE(mesh.validate().empty());
}

TEST_CASE("bounds: transform and empty handling") {
    Bounds b;
    CHECK(b.isEmpty());
    b.expand({-1, -1, -1});
    b.expand({1, 1, 1});
    glm::mat4 m(1.0f);
    m[3] = glm::vec4(10, 0, 0, 1);
    const Bounds t = b.transformed(m);
    CHECK(t.min.x == doctest::Approx(9.0f));
    CHECK(Bounds{}.transformed(m).isEmpty());
}
