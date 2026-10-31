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

const char* filterName(MaterialTextureFilter filter) {
    switch (filter) {
    case MaterialTextureFilter::PixelArt: return "pixelArt";
    case MaterialTextureFilter::Nearest: return "nearest";
    case MaterialTextureFilter::Linear: return "linear";
    case MaterialTextureFilter::Default: break;
    }
    return "default";
}

const char* wrapName(MaterialTextureWrap wrap) {
    switch (wrap) {
    case MaterialTextureWrap::Clamp: return "clamp";
    case MaterialTextureWrap::Mirror: return "mirror";
    case MaterialTextureWrap::Repeat: break;
    }
    return "repeat";
}

struct TextureSlot {
    const char* key;
    MaterialTextureData MaterialData::*member;
};
constexpr TextureSlot kTextureSlots[] = {
    {"baseColorTexture", &MaterialData::baseColorTexture},
    {"metallicRoughnessTexture", &MaterialData::metallicRoughnessTexture},
    {"normalTexture", &MaterialData::normalTexture},
    {"occlusionTexture", &MaterialData::occlusionTexture},
    {"emissiveTexture", &MaterialData::emissiveTexture},
};

void readTexture(const Json& object, std::string_view key, const std::string& parent, MaterialTextureData& out,
                 Issues& issues) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return;
    }
    const std::string path = child(parent, key);
    MaterialTextureData t;
    if (it->is_string()) {
        t.path = it->get<std::string>();
        if (t.path.empty()) {
            issues.add(path, "texture path is empty");
        }
    } else if (it->is_object()) {
        checkKeys(*it, path, {"path", "filter", "wrap"}, issues);
        readString(*it, "path", path, t.path, issues, true);
        std::string filter;
        if (readString(*it, "filter", path, filter, issues)) {
            if (filter == "default") {
                t.filter = MaterialTextureFilter::Default;
            } else if (filter == "pixelArt") {
                t.filter = MaterialTextureFilter::PixelArt;
            } else if (filter == "nearest") {
                t.filter = MaterialTextureFilter::Nearest;
            } else if (filter == "linear") {
                t.filter = MaterialTextureFilter::Linear;
            } else {
                issues.add(child(path, "filter"),
                           "unknown texture filter '" + filter + "' (expected default, pixelArt, nearest or linear)");
            }
        }
        std::string wrap;
        if (readString(*it, "wrap", path, wrap, issues)) {
            if (wrap == "repeat") {
                t.wrap = MaterialTextureWrap::Repeat;
            } else if (wrap == "clamp") {
                t.wrap = MaterialTextureWrap::Clamp;
            } else if (wrap == "mirror") {
                t.wrap = MaterialTextureWrap::Mirror;
            } else {
                issues.add(child(path, "wrap"), "unknown texture wrap '" + wrap + "' (expected repeat, clamp or mirror)");
            }
        }
    } else {
        issues.add(path, "expected a texture path string or object, got " + typeName(*it));
        return;
    }
    out = std::move(t);
}

} // namespace

void readMaterialObject(const Json& object, const std::string& path, const std::string& defaultName,
                        MaterialData& out, Issues& issues) {
    if (!requireObject(object, path, issues)) {
        return;
    }
    checkKeys(object, path,
              {"name", "shader", "alphaMode", "alphaCutoff", "doubleSided", "baseColor", "metallic", "roughness",
               "normalScale", "occlusionStrength", "emissive", "custom0", "custom1", "baseColorTexture",
               "metallicRoughnessTexture", "normalTexture", "occlusionTexture", "emissiveTexture"},
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
    for (const TextureSlot& slot : kTextureSlots) {
        readTexture(object, slot.key, path, m.*slot.member, issues);
    }
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
    for (const TextureSlot& slot : kTextureSlots) {
        const MaterialTextureData& t = m.*slot.member;
        if (!t.empty()) {
            j[slot.key] = Json{{"path", t.path}, {"filter", filterName(t.filter)}, {"wrap", wrapName(t.wrap)}};
        }
    }
    return j;
}

} // namespace detail

MaterialData parseMaterial(std::string_view json, std::string_view file, std::string_view defaultName) {
    const std::string fileName(file);
    const detail::Json root = detail::parseJson(json, fileName);
    const detail::JsonLocator locator(json);
    detail::Issues issues(fileName, &locator);
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

gfx::SamplerDesc samplerFor(const MaterialTextureData& texture, const gfx::SamplerDesc& defaultSampler) {
    gfx::SamplerDesc d;
    switch (texture.filter) {
    case MaterialTextureFilter::PixelArt: d = gfx::SamplerDesc::pixelArt(); break;
    case MaterialTextureFilter::Nearest: d = gfx::SamplerDesc::nearest(); break;
    case MaterialTextureFilter::Linear: d = gfx::SamplerDesc::linear(); break;
    case MaterialTextureFilter::Default: d = defaultSampler; break;
    }
    const gfx::AddressMode mode = texture.wrap == MaterialTextureWrap::Clamp    ? gfx::AddressMode::ClampToEdge
                                  : texture.wrap == MaterialTextureWrap::Mirror ? gfx::AddressMode::MirroredRepeat
                                                                                : gfx::AddressMode::Repeat;
    d.addressU = d.addressV = mode;
    return d;
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

namespace {

gfx::ShaderHandle resolveShader(gfx::MaterialSystem& materials, const MaterialData& material, std::string_view file) {
    try {
        return materials.findShader(material.shader);
    } catch (const Error&) {
        throw DataError({{std::string(file), 0, 0, "/shader",
                          "material '" + material.name + "': unknown shader '" + material.shader +
                              "' (register it with MaterialSystem::registerShader first)"}});
    }
}

} // namespace

gfx::MaterialHandle createMaterial(gfx::MaterialSystem& materials, const MaterialData& material,
                                   std::string_view file) {
    return materials.createMaterial(toMaterialDesc(material, resolveShader(materials, material, file)));
}

void updateMaterial(gfx::MaterialSystem& materials, gfx::MaterialHandle handle, const MaterialData& material,
                    std::string_view file) {
    const gfx::MaterialDesc desc = toMaterialDesc(material, resolveShader(materials, material, file));
    if (handle >= materials.materialCount()) {
        throw DataError({{std::string(file), 0, 0, "", "unknown material handle " + std::to_string(handle)}});
    }
    const gfx::MaterialDesc& current = materials.material(handle);
    if (desc.shader != current.shader || desc.alphaMode != current.alphaMode || desc.doubleSided != current.doubleSided) {
        throw DataError({{std::string(file), 0, 0, "",
                          "material '" + material.name +
                              "': shader, alphaMode and doubleSided cannot change on a live material "
                              "(create a new material)"}});
    }
    materials.updateMaterial(handle, desc);
}

} // namespace zodar
