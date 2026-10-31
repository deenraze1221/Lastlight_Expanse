#include "SceneFile/MaterialFile.h"

#include "SceneFile/JsonRead.h"

namespace zodar {

namespace detail {

namespace {

constexpr float kMax = std::numeric_limits<float>::max();

const char* alphaModeName(gfx::AlphaMode mode) {
    switch (mode) {
    case gfx::AlphaMode::Mask:
        return "mask";
    case gfx::AlphaMode::Blend:
        return "blend";
    case gfx::AlphaMode::Opaque:
        break;
    }
    return "opaque";
}

} // namespace

void readMaterialObject(const Json& object, const std::string& path, const std::string& defaultName,
                        MaterialData& out, Issues& issues) {
    if (!requireObject(object, path, issues)) {
        return;
    }
    checkKeys(object, path,
              {"name", "shader", "alphaMode", "alphaCutoff", "doubleSided", "baseColor", "metallic", "roughness",
               "normalScale", "occlusionStrength", "emissive", "custom0", "custom1"},
              issues);
    MaterialData m;
    m.name = defaultName;
    readString(object, "name", path, m.name, issues);
    readString(object, "shader", path, m.shader, issues);
    std::string alpha;
    if (readString(object, "alphaMode", path, alpha, issues)) {
        if (alpha == "opaque") {
            m.alphaMode = gfx::AlphaMode::Opaque;
        } else if (alpha == "mask") {
            m.alphaMode = gfx::AlphaMode::Mask;
        } else if (alpha == "blend") {
            m.alphaMode = gfx::AlphaMode::Blend;
        } else {
            issues.add(child(path, "alphaMode"), "unknown alpha mode '" + alpha + "' (expected opaque, mask or blend)");
        }
    }
    readFloat(object, "alphaCutoff", path, m.alphaCutoff, issues, 0.0f, 1.0f);
    readBool(object, "doubleSided", path, m.doubleSided, issues);
    readVec<4>(object, "baseColor", path, m.baseColor, issues, 0.0f, 1.0f);
    readFloat(object, "metallic", path, m.metallic, issues, 0.0f, 1.0f);
    readFloat(object, "roughness", path, m.roughness, issues, 0.0f, 1.0f);
    readFloat(object, "normalScale", path, m.normalScale, issues);
    readFloat(object, "occlusionStrength", path, m.occlusionStrength, issues, 0.0f, 1.0f);
    readVec<3>(object, "emissive", path, m.emissive, issues, 0.0f, kMax);
    readVec<4>(object, "custom0", path, m.custom0, issues);
    readVec<4>(object, "custom1", path, m.custom1, issues);
    if (m.name.empty()) {
        issues.add(child(path, "name"), "material has no name");
    }
    out = std::move(m);
}

Json materialJson(const MaterialData& m) {
    Json j = Json::object();
    j["name"] = m.name;
    j["shader"] = m.shader;
    j["alphaMode"] = alphaModeName(m.alphaMode);
    j["alphaCutoff"] = m.alphaCutoff;
    j["doubleSided"] = m.doubleSided;
    j["baseColor"] = vecJson(m.baseColor);
    j["metallic"] = m.metallic;
    j["roughness"] = m.roughness;
    j["normalScale"] = m.normalScale;
    j["occlusionStrength"] = m.occlusionStrength;
    j["emissive"] = vecJson(m.emissive);
    j["custom0"] = vecJson(m.custom0);
    j["custom1"] = vecJson(m.custom1);
    return j;
}

} // namespace detail

MaterialData parseMaterial(std::string_view json, std::string_view file, std::string_view defaultName) {
    const std::string fileName(file);
    const detail::Json root = detail::parseJson(json, fileName);
    detail::Issues issues(fileName);
    MaterialData material;
    detail::readMaterialObject(root, "", std::string(defaultName), material, issues);
    issues.throwIfAny();
    return material;
}

MaterialData loadMaterialFile(const std::filesystem::path& path) {
    std::string stem = path.filename().string();
    if (stem.ends_with(".material.json")) {
        stem.resize(stem.size() - std::string_view(".material.json").size());
    } else {
        stem = path.stem().string();
    }
    return parseMaterial(detail::readTextFile(path), path.generic_string(), stem);
}

std::string serializeMaterial(const MaterialData& material) {
    return detail::materialJson(material).dump(2) + "\n";
}

gfx::MaterialDesc toMaterialDesc(const MaterialData& m, gfx::ShaderHandle shader) {
    gfx::MaterialDesc d;
    d.name = m.name;
    d.shader = shader;
    d.alphaMode = m.alphaMode;
    d.alphaCutoff = m.alphaCutoff;
    d.doubleSided = m.doubleSided;
    d.baseColorFactor = m.baseColor;
    d.metallic = m.metallic;
    d.roughness = m.roughness;
    d.normalScale = m.normalScale;
    d.occlusionStrength = m.occlusionStrength;
    d.emissive = m.emissive;
    d.custom0 = m.custom0;
    d.custom1 = m.custom1;
    return d;
}

gfx::MaterialHandle createMaterial(gfx::MaterialSystem& materials, const MaterialData& material,
                                   std::string_view file) {
    gfx::ShaderHandle shader = 0;
    try {
        shader = materials.findShader(material.shader);
    } catch (const Error&) {
        throw DataError({{std::string(file), 0, 0, "/shader",
                          "material '" + material.name + "': unknown shader '" + material.shader +
                              "' (register it with MaterialSystem::registerShader first)"}});
    }
    return materials.createMaterial(toMaterialDesc(material, shader));
}

} // namespace zodar
