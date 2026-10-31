// Scene / material JSON (Phase B2), CPU only: parsing, validation messages, round-trips and
// instantiation error paths. Rendering equivalence is covered in Gpu/WorldRenderTests.cpp.
#include "SceneFile/SceneFile.h"

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// Writes `files` (name -> content) into a fresh temporary directory and returns it.
std::filesystem::path writeFiles(const std::string& tag, const std::vector<std::pair<std::string, std::string>>& files) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("zodar_scenefile_" + tag);
    std::filesystem::remove_all(dir);
    for (const auto& [name, text] : files) {
        std::filesystem::create_directories((dir / name).parent_path());
        std::ofstream(dir / name, std::ios::binary) << text;
    }
    return dir;
}

const DataIssue* findIssue(const std::vector<DataIssue>& issues, std::string_view path) {
    for (const DataIssue& i : issues) {
        if (i.path == path) {
            return &i;
        }
    }
    return nullptr;
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
    // Schema errors carry the line:column of the offending value.
    CHECK(issues[0].path == "/baseColorTexture");
    CHECK(issues[0].line == 2);
    CHECK(issues[0].toString().starts_with("bad.material.json:2:"));

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

    REQUIRE(doc.entities.size() == 9); // 5 meshes, lamp + expanded prefab bulb, sky, camera.
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

TEST_CASE("scene file: schema errors carry line:column (nearest value for missing fields)") {
    const std::string text = "{\n  \"entities\": [\n    { \"transform\": { \"scale\": \"big\" } },\n"
                             "    { \"meshRenderer\": {\n        \"material\": 1 } }\n  ]\n}\n";
    const auto issues = issuesOf([&] { (void)parseScene(text, "lines.scene.json"); });
    const DataIssue* scale = findIssue(issues, "/entities/0/transform/scale");
    REQUIRE(scale);
    CHECK(scale->line == 3);
    CHECK(scale->column == 31);
    const DataIssue* mesh = findIssue(issues, "/entities/1/meshRenderer/mesh"); // Missing: object position.
    REQUIRE(mesh);
    CHECK(mesh->line == 4);
    CHECK(mesh->column == 23);
    const DataIssue* material = findIssue(issues, "/entities/1/meshRenderer/material");
    REQUIRE(material);
    CHECK(material->line == 5);
    CHECK(material->toString() == "lines.scene.json:5:21: /entities/1/meshRenderer/material: expected a non-empty "
                                  "string, got number");
}

TEST_CASE("scene file: reflected components parse, validate and round-trip") {
    const SceneDocument doc = parseScene(R"({"entities": [
        {"camera": {"orthographic": true, "orthographicHeight": 4, "farPlane": 100}},
        {"directionalLight": {"color": [1, 0.9, 0.8], "intensity": 4}},
        {"pointLight": {"range": 7}, "spotLight": {"outerConeDegrees": 40}},
        {"environment": {"clearColor": [0.1, 0.2, 0.3, 1], "drawBackground": false}, "fog": {"density": 0.05}}]})");
    REQUIRE(doc.entities.size() == 4);
    CHECK(doc.entities[0].camera->orthographic);
    CHECK(doc.entities[0].camera->farPlane == 100.0f);
    CHECK(doc.entities[0].camera->verticalFovDegrees == 60.0f); // Default kept.
    CHECK(doc.entities[1].directionalLight->intensity == 4.0f);
    CHECK(doc.entities[2].pointLight->range == 7.0f);
    CHECK(doc.entities[2].pointLight->intensity == 10.0f);
    CHECK(doc.entities[2].spotLight->outerConeDegrees == 40.0f);
    CHECK_FALSE(doc.entities[3].environment->drawBackground);
    CHECK(doc.entities[3].fog->density == 0.05f);
    CHECK(parseScene(serializeScene(doc)) == doc);

    const auto issues = issuesOf([] {
        (void)parseScene(R"({"entities": [{"spotLight": {"outerConeDegrees": 95, "colour": [1, 1, 1]},
            "fog": {"maxOpacity": 2}, "camera": {"active": 1}, "pointLight": [1]}]})");
    });
    CHECK(hasIssue(issues, "/entities/0/spotLight/outerConeDegrees", "outside [0, 89.9]"));
    CHECK(hasIssue(issues, "/entities/0/spotLight/colour", "unknown field"));
    CHECK(hasIssue(issues, "/entities/0/fog/maxOpacity", "outside [0, 1]"));
    CHECK(hasIssue(issues, "/entities/0/camera/active", "expected true or false"));
    CHECK(hasIssue(issues, "/entities/0/pointLight", "expected an object"));
}

TEST_CASE("scene file: reflection tables and generated component reference") {
    const ComponentInfo info = componentInfo<PointLightComponent>();
    CHECK(info.key == "pointLight");
    REQUIRE(info.fields.size() == 3);
    PointLightComponent light;
    info.fields[1].get<float>(&light) = 42.0f;
    CHECK(light.intensity == 42.0f);
    CHECK(formatField(info.fields[0], &light) == "[1, 1, 1]");
    const Transform identity;
    CHECK(formatField(componentInfo<Transform>().fields[1], &identity) == "[0, 0, 0, 1]");

    const std::string reference = sceneComponentReference();
    CHECK(reference.find("## `spotLight`") != std::string::npos);
    CHECK(reference.find("| `outerConeDegrees` | number | `31.5` | 0 .. 89.9 |") != std::string::npos);
    const std::filesystem::path doc = std::filesystem::path(ZODAR_EXAMPLES_DIR) / ".." / "docs" / "guide" / "SCENE_COMPONENTS.md";
    if (std::getenv("ZODAR_UPDATE_DOCS") != nullptr) {
        std::ofstream(doc, std::ios::binary) << reference;
    }
    std::ifstream in(doc, std::ios::binary);
    const std::string committed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK_MESSAGE(committed == reference, "docs/guide/SCENE_COMPONENTS.md is stale: rerun with ZODAR_UPDATE_DOCS=1");
}

TEST_CASE("scene file: collectLighting and collectCamera read the World") {
    const SceneDocument doc = parseScene(R"({"entities": [
        {"id": "rig", "transform": {"position": [10, 0, 0]}},
        {"parent": "rig", "transform": {"position": [0, 2, 0]}, "pointLight": {"intensity": 5}},
        {"transform": {"position": [1, 1, 1]}, "pointLight": {"intensity": 7}},
        {"transform": {"rotationDegrees": [-90, 0, 0]}, "directionalLight": {"intensity": 2}},
        {"transform": {"rotationDegrees": [0, 90, 0]}, "spotLight": {"innerConeDegrees": 10}},
        {"camera": {"active": false}},
        {"transform": {"position": [0, 1, 5]}, "camera": {"verticalFovDegrees": 45}},
        {"fog": {"density": 0.2}}, {"environment": {"ambientIntensity": 0.5}}]})");
    World world;
    (void)instantiateScene(doc, world, {});

    gfx::SceneView view;
    view.lighting.pointLights.push_back({}); // Replaced, not appended.
    collectLighting(world, view);
    REQUIRE(view.lighting.pointLights.size() == 2);
    CHECK(view.lighting.pointLights[0].position == glm::vec3(10, 2, 0)); // Parent transform applied.
    CHECK(view.lighting.pointLights[0].intensity == 5.0f);
    CHECK(view.lighting.pointLights[1].intensity == 7.0f);
    CHECK(view.lighting.sun.enabled);
    CHECK(view.lighting.sun.intensity == 2.0f);
    CHECK(view.lighting.sun.direction.y == doctest::Approx(-1.0f).epsilon(1e-5)); // -Z pitched down.
    REQUIRE(view.lighting.spotLights.size() == 1);
    CHECK(view.lighting.spotLights[0].direction.x == doctest::Approx(-1.0f).epsilon(1e-5));
    CHECK(view.lighting.spotLights[0].innerConeRadians == doctest::Approx(glm::radians(10.0f)));
    CHECK(view.lighting.fog.enabled);
    CHECK(view.lighting.fog.density == 0.2f);
    CHECK(view.lighting.ambientIntensity == 0.5f);

    gfx::Camera camera;
    REQUIRE(collectCamera(world, camera));
    CHECK(camera.position == glm::vec3(0, 1, 5));
    CHECK(camera.projection.verticalFovRadians == doctest::Approx(glm::radians(45.0f)));

    World empty;
    gfx::SceneView defaults;
    collectLighting(empty, defaults);
    CHECK_FALSE(defaults.lighting.sun.enabled);
    CHECK_FALSE(defaults.lighting.fog.enabled);
    CHECK_FALSE(collectCamera(empty, camera));
}

TEST_CASE("scene file: prefabs expand with prefixed ids, shared materials and overrides") {
    const SceneDocument doc = loadSceneFile(kExample / "scene.scene.json");
    const SceneEntity& lamp = doc.entities[5];
    CHECK(lamp.id == "lamp");
    const SceneEntity& bulb = doc.entities[6];
    CHECK(bulb.id == "lamp/bulb");
    CHECK(bulb.parent == "lamp");
    CHECK(bulb.name == "Bulb");
    REQUIRE(bulb.pointLight);
    CHECK(bulb.pointLight->intensity == 25.0f); // Overridden.
    CHECK(bulb.pointLight->range == 15.0f);     // From the prefab.

    const std::filesystem::path dir = writeFiles("prefab", {
        {"m/stone.material.json", R"({"roughness": 0.9})"},
        {"p/rock.prefab.json", R"({"materials": {"stone": "../m/stone.material.json", "moss": {"roughness": 1}},
            "entities": [{"id": "body", "meshRenderer": {"mesh": "rock", "material": "stone"}},
                         {"id": "tip", "parent": "body", "transform": {"position": [0, 1, 0]}}]})"},
        {"p/pile.prefab.json", R"({"entities": [{"id": "a", "prefab": "rock.prefab.json"},
                                                 {"id": "b", "prefab": "rock.prefab.json"}]})"},
        {"level.scene.json", R"({"materials": {"gold": {"metallic": 1}}, "entities": [
            {"id": "pile", "prefab": "p/pile.prefab.json"},
            {"id": "r", "prefab": "p/rock.prefab.json",
             "overrides": {"body": {"meshRenderer": {"material": "gold"}}, "tip": {"name": "Tip"}}}]})"},
        {"cycle.scene.json", R"({"entities": [{"id": "x", "prefab": "loop.prefab.json"}]})"},
        {"loop.prefab.json", R"({"entities": [{"id": "y", "prefab": "loop.prefab.json"}]})"},
        {"bad.scene.json", R"({"entities": [
            {"prefab": "p/rock.prefab.json"},
            {"id": "q", "prefab": "missing.prefab.json"},
            {"id": "s", "prefab": "p/rock.prefab.json", "overrides": {"nope": {}, "body": {"meshRenderer": {"material": "zz"}}}},
            {"id": "t", "overrides": {}}]})"},
    });

    const SceneDocument level = loadSceneFile(dir / "level.scene.json");
    // pile, pile/a, pile/a/body, pile/a/tip, pile/b, pile/b/body, pile/b/tip, r, r/body, r/tip.
    REQUIRE(level.entities.size() == 10);
    CHECK(level.entities[2].id == "pile/a/body");
    CHECK(level.entities[2].parent == "pile/a");
    CHECK(level.entities[3].parent == "pile/a/body");
    CHECK(level.entities[2].meshRenderer->material == "p/pile.prefab.json#rock.prefab.json#stone");
    CHECK(level.entities[8].meshRenderer->material == "gold"); // Override uses the scene's material.
    CHECK(level.entities[9].name == "Tip");
    CHECK(level.entities[9].transform->position == glm::vec3(0, 1, 0)); // Untouched by the override.
    // Prefab materials are added once per prefab path, with sources rebased to the scene.
    std::vector<std::string> ids;
    for (const SceneMaterial& m : level.materials) {
        ids.push_back(m.id);
    }
    CHECK(ids == std::vector<std::string>{"gold", "p/pile.prefab.json#rock.prefab.json#stone",
                                          "p/pile.prefab.json#rock.prefab.json#moss", "p/rock.prefab.json#stone",
                                          "p/rock.prefab.json#moss"});
    CHECK(level.materials[1].source == "m/stone.material.json");
    CHECK(level.materials[3].source == "m/stone.material.json");
    CHECK(level.materials[3].data.roughness == 0.9f);
    // The flat document round-trips (no prefab references are written).
    CHECK(parseScene(serializeScene(level), "flat", dir) == level);

    const auto cycle = issuesOf([&] { (void)loadSceneFile(dir / "cycle.scene.json"); });
    CHECK(std::any_of(cycle.begin(), cycle.end(), [](const DataIssue& i) {
        return i.message.find("prefab cycle") != std::string::npos;
    }));

    const auto bad = issuesOf([&] { (void)loadSceneFile(dir / "bad.scene.json"); });
    CHECK(hasIssue(bad, "/entities/0/id", "prefab instance needs an id"));
    CHECK(hasIssue(bad, "/entities/1/prefab", "cannot load prefab 'missing.prefab.json'"));
    CHECK(hasIssue(bad, "/entities/2/overrides/nope", "has no entity with id 'nope'"));
    CHECK(hasIssue(bad, "/entities/2/overrides/body/meshRenderer/material", "unknown material 'zz'"));
    CHECK(hasIssue(bad, "/entities/3/overrides", "needs 'prefab'"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("scene file: captureScene writes a live World back to a document") {
    // The capture never dereferences meshes; distinct addresses are enough.
    static int storage[2];
    const auto* cube = reinterpret_cast<const gfx::GpuMesh*>(&storage[0]);
    const auto* ball = reinterpret_cast<const gfx::GpuMesh*>(&storage[1]);
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();
    const Entity d = world.create();
    world.add<Name>(a, Name{"A"});
    world.add<Transform>(b, Transform{{1, 2, 3}, glm::angleAxis(0.3f, glm::vec3(0, 1, 0)), glm::vec3(2)});
    world.add<MeshRenderer>(b, MeshRenderer{cube, 0, 4, false});
    world.add<PointLightComponent>(c, PointLightComponent{{1, 0, 0}, 3, 9});
    world.add<MeshRenderer>(d, MeshRenderer{ball, 1, 5, true});
    world.setParent(c, b); // b: children c, d (in that order); a is a root.
    world.setParent(d, b);
    world.setParent(b, a);

    SceneCaptureNames names;
    names.meshes = {{cube, "cube"}, {ball, "ball"}};
    names.materials = {{4, "red"}, {5, "blue"}};
    names.materialDefinitions = {{"red", "", parseMaterial("{}", "", "red")}, {"blue", "", parseMaterial("{}", "", "blue")}};
    const SceneDocument doc = captureScene(world, names);
    REQUIRE(doc.entities.size() == 4);
    CHECK(doc.materials.size() == 2);
    CHECK(doc.entities[0].id == "e0");
    CHECK(doc.entities[0].name == "A");
    CHECK(doc.entities[1].id == "e1");
    CHECK(doc.entities[1].parent == "e0");
    CHECK(doc.entities[1].transform == world.get<Transform>(b));
    CHECK(doc.entities[1].meshRenderer == SceneMeshRenderer{"cube", 0, "red", false});
    CHECK(doc.entities[2].id.empty());
    CHECK(doc.entities[2].parent == "e1");
    CHECK(doc.entities[2].pointLight == PointLightComponent{{1, 0, 0}, 3, 9});
    CHECK(doc.entities[3].meshRenderer == SceneMeshRenderer{"ball", 1, "blue", true});
    const std::string saved = serializeScene(doc);
    CHECK(parseScene(saved) == doc);

    // Re-instantiating reproduces the hierarchy (child order included).
    World copy;
    const SceneInstance instance = instantiateScene(parseScene(R"({"entities": [{"id": "e0"}, {"id": "e1", "parent": "e0"},
        {"parent": "e1", "pointLight": {}}, {"parent": "e1"}]})"), copy, {});
    const auto children = copy.children(instance.entities[1]);
    REQUIRE(children.size() == 2);
    CHECK(children[0] == instance.entities[2]);

    names.materials.erase(5);
    const auto issues = issuesOf([&] { (void)captureScene(world, names, "save"); });
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "/entities/3/meshRenderer/material");
    CHECK(issues[0].message.find("material handle 5 has no id") != std::string::npos);
}
