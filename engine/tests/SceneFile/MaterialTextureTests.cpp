// Phase C: texture slots in material files (*.material.json) and their samplers.
#include "SceneFile/MaterialFile.h"
#include "SceneFile/SceneFile.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace zodar;

namespace {

std::vector<DataIssue> issuesOf(std::string_view json) {
    try {
        (void)parseMaterial(json, "bad.material.json", "bad");
    } catch (const DataError& e) {
        return e.issues();
    }
    return {};
}

} // namespace

TEST_CASE("material textures: string and object forms parse and round-trip") {
    const MaterialData m = parseMaterial(R"({
        "baseColorTexture": "textures/crate.png",
        "normalTexture": { "path": "n.png", "filter": "linear", "wrap": "clamp" },
        "emissiveTexture": { "path": "e.png", "filter": "pixelArt", "wrap": "mirror" },
        "occlusionTexture": { "path": "o.png", "filter": "nearest" }
    })", "m.material.json", "m");
    CHECK(m.baseColorTexture.path == "textures/crate.png");
    CHECK(m.baseColorTexture.filter == MaterialTextureFilter::Default);
    CHECK(m.baseColorTexture.wrap == MaterialTextureWrap::Repeat);
    CHECK(m.normalTexture.filter == MaterialTextureFilter::Linear);
    CHECK(m.normalTexture.wrap == MaterialTextureWrap::Clamp);
    CHECK(m.emissiveTexture.filter == MaterialTextureFilter::PixelArt);
    CHECK(m.emissiveTexture.wrap == MaterialTextureWrap::Mirror);
    CHECK(m.occlusionTexture.filter == MaterialTextureFilter::Nearest);
    CHECK(m.metallicRoughnessTexture.empty());
    CHECK(parseMaterial(serializeMaterial(m), "rt", "m") == m);

    // Untextured materials serialize exactly as before Phase C (no texture keys).
    const std::string plain = serializeMaterial(parseMaterial("{}", "p", "p"));
    CHECK(plain.find("Texture") == std::string::npos);
}

TEST_CASE("material textures: validation errors name the field") {
    const auto issues = issuesOf(R"({
        "baseColorTexture": 3,
        "normalTexture": { "filter": "bilinear", "wrap": "tile", "extra": 1 },
        "emissiveTexture": ""
    })");
    REQUIRE(issues.size() == 6);
    const auto has = [&](std::string_view path, std::string_view text) {
        for (const DataIssue& i : issues) {
            if (i.path == path && i.message.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    CHECK(has("/baseColorTexture", "expected a texture path string or object"));
    CHECK(has("/normalTexture/path", "required field is missing"));
    CHECK(has("/normalTexture/filter", "unknown texture filter 'bilinear'"));
    CHECK(has("/normalTexture/wrap", "unknown texture wrap 'tile'"));
    CHECK(has("/normalTexture/extra", ""));
    CHECK(has("/emissiveTexture", "texture path is empty"));
}

TEST_CASE("material textures: samplerFor maps filters and wrap modes") {
    using gfx::Filter;
    gfx::SamplerDesc fallback = gfx::SamplerDesc::linear();
    fallback.maxAnisotropy = 4.0f;
    MaterialTextureData t{"a.png", MaterialTextureFilter::Default, MaterialTextureWrap::Clamp};
    gfx::SamplerDesc s = samplerFor(t, fallback);
    CHECK(s.maxAnisotropy == 4.0f);
    CHECK(s.addressU == gfx::AddressMode::ClampToEdge);
    CHECK(s.addressV == gfx::AddressMode::ClampToEdge);

    t.filter = MaterialTextureFilter::PixelArt;
    t.wrap = MaterialTextureWrap::Repeat;
    s = samplerFor(t, fallback);
    CHECK(s.magFilter == Filter::Nearest);
    CHECK(s.minFilter == Filter::Nearest);
    CHECK(s.mipFilter == Filter::Linear);
    CHECK(s.maxAnisotropy == 1.0f);
    CHECK(s.addressU == gfx::AddressMode::Repeat);

    t.filter = MaterialTextureFilter::Nearest;
    CHECK(samplerFor(t, fallback).mipFilter == Filter::Nearest);
    t.filter = MaterialTextureFilter::Linear;
    t.wrap = MaterialTextureWrap::Mirror;
    s = samplerFor(t, fallback);
    CHECK(s.magFilter == Filter::Linear);
    CHECK(s.addressV == gfx::AddressMode::MirroredRepeat);
}

TEST_CASE("material textures: example scene keeps texture paths as written") {
    const SceneDocument doc =
        loadSceneFile(std::filesystem::path(ZODAR_EXAMPLES_DIR) / "asset_loading" / "asset_loading.scene.json");
    REQUIRE(doc.materials.size() == 2);
    const auto find = [&](std::string_view id) -> const SceneMaterial& {
        for (const SceneMaterial& m : doc.materials) {
            if (m.id == id) {
                return m;
            }
        }
        FAIL("missing material");
        return doc.materials.front();
    };
    const SceneMaterial& rune = find("rune");
    CHECK(rune.source == "materials/rune.material.json");
    CHECK(rune.data.baseColorTexture.path == "../textures/rune.png");
    CHECK(rune.data.baseColorTexture.filter == MaterialTextureFilter::PixelArt);
    const SceneMaterial& crate = find("crateLinear");
    CHECK(crate.data.baseColorTexture.path == "textures/crate.png");
    CHECK(crate.data.baseColorTexture.filter == MaterialTextureFilter::Linear);
}
