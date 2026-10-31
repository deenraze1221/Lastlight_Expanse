// Scene / material JSON (Phase B2), CPU only: parsing, validation messages, round-trips and
// instantiation error paths. Rendering equivalence is covered in Gpu/WorldRenderTests.cpp.
#include "SceneFile/SceneFile.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace zodar;

namespace {

const std::filesystem::path kExample = std::filesystem::path(ZODAR_EXAMPLES_DIR) / "scene_from_json";

// Runs `fn`, expecting a DataError; returns its issues.
template <typename Fn>
std::vector<DataIssue> issuesOf(Fn&& fn) {
    try {
        fn();
    } catch (const DataError& e) {
        return e.issues();
    }
    FAIL("expected DataError");
    return {};
}

bool hasIssue(const std::vector<DataIssue>& issues, std::string_view path, std::string_view text) {
    return std::any_of(issues.begin(), issues.end(), [&](const DataIssue& i) {
        return i.path == path && i.message.find(text) != std::string::npos;
    });
}

} // namespace

TEST_CASE("scene file: material defaults, fields and round-trip") {
    const MaterialData defaults = parseMaterial("{}", "m.json", "plain");
    CHECK(defaults.name == "plain");
    CHECK(defaults.shader == "zodar/pbr");
    CHECK(defaults.roughness == 0.5f);
    CHECK(defaults.baseColor == glm::vec4(1.0f));

    const MaterialData m = parseMaterial(R"({"name": "glass", "shader": "zodar/unlit", "alphaMode": "blend",
        "alphaCutoff": 0.25, "doubleSided": true, "baseColor": [0.1, 0.2, 0.3, 0.4], "metallic": 1,
        "roughness": 0.3, "normalScale": 2, "occlusionStrength": 0.5, "emissive": [4, 0, 0],
        "custom0": [1, 2, 3, 4], "custom1": [-1, 0, 0, 0]})");
    CHECK(m.name == "glass");
    CHECK(m.alphaMode == gfx::AlphaMode::Blend);
    CHECK(m.doubleSided);
    CHECK(m.baseColor == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
    CHECK(m.emissive == glm::vec3(4, 0, 0));
    CHECK(m.custom1.x == -1.0f);
    CHECK(parseMaterial(serializeMaterial(m)) == m);
    CHECK(serializeMaterial(parseMaterial(serializeMaterial(m))) == serializeMaterial(m));

    const gfx::MaterialDesc d = toMaterialDesc(m, 7);
    CHECK(d.shader == 7);
    CHECK(d.baseColorFactor == m.baseColor);
    CHECK(d.alphaMode == gfx::AlphaMode::Blend);
    CHECK(d.baseColor.texture == nullptr);
}

TEST_CASE("scene file: material validation reports every problem with its path") {
    const auto issues = issuesOf([] {
        (void)parseMaterial(R"({"name": "bad", "roughness": 1.5, "baseColor": [1, 0, 0], "alphaMode": "glass",
            "metallic": "high", "baseColorTexture": "a.png", "roughnes": 0.3})",
                            "bad.material.json");
    });
    CHECK(issues.size() == 6);
    CHECK(hasIssue(issues, "/roughness", "outside [0, 1]"));
    CHECK(hasIssue(issues, "/baseColor", "array of 4 numbers"));
    CHECK(hasIssue(issues, "/alphaMode", "unknown alpha mode 'glass'"));
    CHECK(hasIssue(issues, "/metallic", "expected a number, got string"));
    CHECK(hasIssue(issues, "/baseColorTexture", "not supported yet"));
    CHECK(hasIssue(issues, "/roughnes", "unknown field"));
    CHECK(issues[0].file == "bad.material.json");
    CHECK(issues[0].toString().starts_with("bad.material.json: /"));

    CHECK(hasIssue(issuesOf([] { (void)parseMaterial("{}"); }), "/name", "no name"));
    CHECK(hasIssue(issuesOf([] { (void)parseMaterial("[1]", "x", "n"); }), "", "expected an object"));
}

TEST_CASE("scene file: syntax errors carry file:line:column") {
    const auto issues = issuesOf([] { (void)parseScene("{\n  \"entities\": [\n    {,}\n  ]\n}", "broken.scene.json"); });
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].file == "broken.scene.json");
    CHECK(issues[0].line == 3);
    CHECK(issues[0].column == 6);
    CHECK(issues[0].message.starts_with("invalid JSON"));
    CHECK(issues[0].toString().starts_with("broken.scene.json:3:6: invalid JSON"));

    const auto missing = issuesOf([] { (void)loadSceneFile(kExample / "does_not_exist.scene.json"); });
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].message == "cannot open file");
}

TEST_CASE("scene file: example scene parses with referenced and inline materials") {
    const SceneDocument doc = loadSceneFile(kExample / "scene.scene.json");
    REQUIRE(doc.materials.size() == 2);
    CHECK(doc.materials[0].id == "red");
    CHECK(doc.materials[0].source == "materials/red.material.json");
    CHECK(doc.materials[0].data.name == "red"); // From the file name.
    CHECK(doc.materials[0].data.baseColor == glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
    CHECK(doc.materials[1].id == "blue");
    CHECK(doc.materials[1].source.empty());
    CHECK(doc.materials[1].data.name == "blue"); // Inline: defaults to the id.

    REQUIRE(doc.entities.size() == 5);
    const SceneEntity& root = doc.entities[0];
    CHECK(root.id == "root");
    CHECK(root.name == "Pedestal");
    REQUIRE(root.transform);
    CHECK(root.transform->position == glm::vec3(0, 0.5f, -3));
    CHECK(root.transform->rotation == glm::quat(0.98006658f, 0, 0.19866933f, 0));
    CHECK(root.transform->scale == glm::vec3(1));
    REQUIRE(root.meshRenderer);
    CHECK(root.meshRenderer->mesh == "cube");
    CHECK(doc.entities[3].parent == "root");
    CHECK(doc.entities[2].meshRenderer->material == "blue");

    // Save -> load round-trip is exact and the output is stable.
    const std::string saved = serializeScene(doc);
    const SceneDocument reloaded = parseScene(saved, "saved", kExample);
    CHECK(reloaded == doc);
    CHECK(serializeScene(reloaded) == saved);
}

TEST_CASE("scene file: transform forms (uniform scale, rotationDegrees)") {
    const SceneDocument doc = parseScene(R"({"entities": [
        {"transform": {"scale": 2, "rotationDegrees": [0, 90, 0]}},
        {"transform": {}}, {}]})");
    REQUIRE(doc.entities.size() == 3);
    const Transform& t = *doc.entities[0].transform;
    CHECK(t.scale == glm::vec3(2));
    const glm::vec3 x = t.rotation * glm::vec3(1, 0, 0);
    CHECK(x.z == doctest::Approx(-1.0f).epsilon(1e-5));
    CHECK(*doc.entities[1].transform == Transform{});
    CHECK_FALSE(doc.entities[2].transform);
    CHECK_FALSE(doc.entities[2].meshRenderer);
    CHECK(parseScene(serializeScene(doc)) == doc);
}

TEST_CASE("scene file: scene validation (references, hierarchy, types)") {
    const auto issues = issuesOf([] {
        (void)parseScene(R"({"version": 2, "extra": 1,
            "materials": {"m": {"roughness": -1}, "n": 5, "f": "missing.material.json"},
            "entities": [
              {"id": "a", "parent": "b"},
              {"id": "b", "parent": "a"},
              {"id": "a"},
              {"id": "c", "parent": "c"},
              {"parent": "nobody", "transform": {"rotation": [0, 0, 0, 2], "rotationDegrees": [0, 0, 0]}},
              {"meshRenderer": {"material": "zzz", "submesh": -1}},
              {"name": 3},
              7]})",
                         "bad.scene.json", kExample);
    });
    CHECK(hasIssue(issues, "/version", "unsupported scene version 2"));
    CHECK(hasIssue(issues, "/extra", "unknown field"));
    CHECK(hasIssue(issues, "/materials/m/roughness", "outside"));
    CHECK(hasIssue(issues, "/materials/n", "expected a material object or a material file path"));
    CHECK(hasIssue(issues, "/materials/f", "cannot load material file 'missing.material.json'"));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const DataIssue& i) {
        return i.file.ends_with("missing.material.json") && i.message == "cannot open file";
    }));
    CHECK(hasIssue(issues, "/entities/0/parent", "parent cycle"));
    CHECK(hasIssue(issues, "/entities/1/parent", "parent cycle"));
    CHECK(hasIssue(issues, "/entities/2/id", "duplicate entity id 'a'"));
    CHECK(hasIssue(issues, "/entities/3/parent", "own ancestor"));
    CHECK(hasIssue(issues, "/entities/4/parent", "unknown parent entity id 'nobody'"));
    CHECK(hasIssue(issues, "/entities/4/transform/rotation", "unit length"));
    CHECK(hasIssue(issues, "/entities/4/transform/rotationDegrees", "not both"));
    CHECK(hasIssue(issues, "/entities/5/meshRenderer/mesh", "required field is missing"));
    CHECK(hasIssue(issues, "/entities/5/meshRenderer/submesh", "non-negative integer"));
    CHECK(hasIssue(issues, "/entities/5/meshRenderer/material", "unknown material 'zzz'"));
    CHECK(hasIssue(issues, "/entities/6/name", "expected a string"));
    CHECK(hasIssue(issues, "/entities/7", "expected an object, got number"));
    for (const DataIssue& issue : issues) {
        CHECK_FALSE(issue.file.empty());
    }
}

TEST_CASE("scene file: instantiate validates bindings and leaves the world unchanged on error") {
    const SceneDocument doc = loadSceneFile(kExample / "scene.scene.json");
    World world;
    const Entity existing = world.create();

    SceneBindings bindings; // Nothing bound.
    bindings.materials["red"] = 1;
    const auto issues = issuesOf([&] { (void)instantiateScene(doc, world, bindings, "scene.scene.json"); });
    CHECK(hasIssue(issues, "/entities/0/meshRenderer/mesh", "mesh 'cube' is not bound"));
    CHECK(hasIssue(issues, "/entities/2/meshRenderer/material", "material 'blue' is not bound"));
    CHECK(issues[0].file == "scene.scene.json");
    CHECK(world.entityCount() == 1);
    CHECK(world.isAlive(existing));

    // A hand-built document with a parent cycle is rolled back.
    SceneDocument cyclic;
    cyclic.entities = {{"a", "b", {}, {}, {}}, {"b", "a", {}, {}, {}}};
    const auto cycle = issuesOf([&] { (void)instantiateScene(cyclic, world, bindings); });
    CHECK(hasIssue(cycle, "/entities/1/parent", "creates a cycle"));
    CHECK(world.entityCount() == 1);

    // Without mesh renderers no GPU objects are needed: names, transforms and hierarchy.
    const SceneDocument plain = parseScene(R"({"entities": [
        {"id": "p", "name": "Parent", "transform": {"position": [1, 0, 0]}},
        {"name": "Child", "parent": "p", "transform": {"position": [0, 2, 0]}}]})");
    const SceneInstance instance = instantiateScene(plain, world, {});
    REQUIRE(instance.entities.size() == 2);
    CHECK(instance.byId.at("p") == instance.entities[0]);
    CHECK(world.get<Name>(instance.entities[1]).value == "Child");
    CHECK(world.parent(instance.entities[1]) == instance.entities[0]);
    CHECK(glm::vec3(world.worldMatrix(instance.entities[1])[3]) == glm::vec3(1, 2, 0));
}
