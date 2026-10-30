#include <doctest/doctest.h>

#include "Assets/ModelData.h"
#include "Core/Error.h"

#include <stb_image_write.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace raven;

namespace {

std::string base64(const std::vector<uint8_t>& data) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (i + 1 < data.size() ? uint32_t(data[i + 1]) << 8 : 0) |
                           (i + 2 < data.size() ? data[i + 2] : 0);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += i + 1 < data.size() ? table[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? table[n & 63] : '=';
    }
    return out;
}

template <typename T>
void append(std::vector<uint8_t>& buffer, std::initializer_list<T> values) {
    for (T v : values) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        buffer.insert(buffer.end(), p, p + sizeof(T));
    }
}

std::filesystem::path writeFile(const std::string& name, const std::string& text) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << text;
    return path;
}

// Quad in the XY plane made of two primitives with different materials, a parent node with
// a translation and a child node with a scale; the PNG and buffer are embedded data URIs.
std::string testGltf() {
    std::vector<uint8_t> buffer;
    append<float>(buffer, {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0});              // positions  (0..47)
    append<float>(buffer, {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1});              // normals    (48..95)
    append<float>(buffer, {0, 1, 1, 1, 1, 0, 0, 0});                          // uvs        (96..127)
    append<uint16_t>(buffer, {0, 1, 2, 0, 2, 3});                             // indices    (128..139)

    const uint8_t rgba[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    std::vector<uint8_t> png;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* out = static_cast<std::vector<uint8_t>*>(ctx);
            out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &png, 2, 2, 4, rgba, 8);

    return R"({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_unlit"],
  "buffers": [{"byteLength": 140, "uri": "data:application/octet-stream;base64,)" + base64(buffer) + R"("}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48},
    {"buffer": 0, "byteOffset": 96, "byteLength": 32},
    {"buffer": 0, "byteOffset": 128, "byteLength": 12}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0,0,0], "max": [1,1,0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 3, "byteOffset": 6, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "images": [{"uri": "data:image/png;base64,)" + base64(png) + R"("}],
  "samplers": [{"magFilter": 9728, "minFilter": 9984, "wrapS": 33071, "wrapT": 10497}],
  "textures": [{"source": 0, "sampler": 0}],
  "materials": [
    {"name": "Painted", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.25,
     "roughnessFactor": 0.75}, "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true},
    {"name": "Glow", "emissiveFactor": [1, 0.5, 0], "extensions": {"KHR_materials_unlit": {}}}
  ],
  "meshes": [{"name": "Quad", "primitives": [
    {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0},
    {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 4, "material": 1}
  ]}],
  "nodes": [
    {"name": "Parent", "translation": [1, 2, 3], "children": [1]},
    {"name": "Child", "scale": [2, 2, 2], "mesh": 0}
  ],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})";
}

} // namespace

TEST_CASE("gltf: meshes, materials, embedded image, sampler and node hierarchy") {
    const auto path = writeFile("raven_test.gltf", testGltf());
    const ModelData model = loadGltf(path);

    REQUIRE(model.meshes.size() == 1);
    const MeshData& mesh = model.meshes[0];
    CHECK(mesh.name == "Quad");
    REQUIRE(mesh.submeshes.size() == 2);
    CHECK(mesh.vertices.size() == 8); // Each primitive keeps its own vertex range.
    CHECK(mesh.submeshes[0].materialIndex == 0);
    CHECK(mesh.submeshes[1].materialIndex == 1);
    CHECK(mesh.indices[3] == 4);      // Second primitive's indices are rebased.
    CHECK(mesh.validate().empty());
    CHECK(glm::length(glm::vec3(mesh.vertices[0].tangent)) == doctest::Approx(1.0f)); // Generated.
    CHECK(mesh.vertices[0].tangent.x == doctest::Approx(1.0f));

    REQUIRE(model.materials.size() == 2);
    const ModelMaterial& painted = model.materials[0];
    CHECK(painted.alpha == ModelMaterial::Alpha::Mask);
    CHECK(painted.alphaCutoff == doctest::Approx(0.3f));
    CHECK(painted.doubleSided);
    CHECK(painted.metallic == doctest::Approx(0.25f));
    CHECK(painted.baseColor.image == 0);
    CHECK(painted.baseColor.sampler == 0);
    CHECK(model.materials[1].shading == ModelMaterial::Shading::Unlit);
    CHECK(model.materials[1].emissive == glm::vec3(1, 0.5f, 0));

    REQUIRE(model.images.size() == 1);
    CHECK(model.images[0].colorSpace == ColorSpace::Srgb);
    CHECK(model.images[0].levels.size() == 2); // 2x2 + generated 1x1.
    CHECK(model.images[0].levels[0][4 + 1] == 255);

    REQUIRE(model.samplers.size() == 1);
    CHECK(model.samplers[0].specified);
    CHECK(model.samplers[0].magNearest);
    CHECK(model.samplers[0].minNearest);
    CHECK(model.samplers[0].mipNearest);
    CHECK(model.samplers[0].wrapU == ModelSampler::Wrap::ClampToEdge);

    REQUIRE(model.nodes.size() == 1);
    const glm::vec4 corner = model.nodes[0].world * glm::vec4(1, 1, 0, 1);
    CHECK(corner.x == doctest::Approx(3.0f)); // translate(1,2,3) * scale(2)
    CHECK(corner.y == doctest::Approx(4.0f));
    CHECK(corner.z == doctest::Approx(3.0f));
    CHECK(model.bounds.max.x == doctest::Approx(3.0f));
    CHECK(model.warnings.empty());
    std::filesystem::remove(path);
}

TEST_CASE("gltf: clear errors for missing, malformed and unsupported files") {
    CHECK_THROWS_AS(loadGltf("does/not/exist.gltf"), raven::Error);
    const auto broken = writeFile("raven_broken.gltf", "{ not json");
    CHECK_THROWS_AS(loadGltf(broken), raven::Error);
    const auto draco = writeFile("raven_required.gltf",
                                 R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_draco_mesh_compression"],)"
                                 R"("extensionsRequired":["KHR_draco_mesh_compression"]})");
    try {
        loadGltf(draco);
        FAIL("expected an error");
    } catch (const raven::Error& e) {
        CHECK(std::string(e.what()).find("KHR_draco_mesh_compression") != std::string::npos);
    }
    std::filesystem::remove(broken);
    std::filesystem::remove(draco);
}
