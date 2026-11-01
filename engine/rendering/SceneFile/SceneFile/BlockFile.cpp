#include "SceneFile/BlockFile.h"

#include "Core/Error.h"
#include "SceneFile/JsonRead.h"

#include <cmath>
#include <set>
#include <tuple>

namespace zodar {

namespace {

using detail::Json;

constexpr const char* kFaceKeys[3] = {"top", "side", "bottom"};
// Ids 0..65534: id 0xFFFF stays outside every registry, so BlockTable always has an
// out-of-range id (fallback cube, e.g. for bedrock when no block is a cube).
constexpr size_t kMaxIds = 0xFFFF;

bool validName(const std::string& name, const std::string& path, detail::Issues& issues) {
    if (name.find('@') != std::string::npos) {
        issues.add(path, "name '" + name + "' contains '@' (reserved for rotation variants)");
        return false;
    }
    return true;
}

bool readTexturePath(const Json& value, const std::string& path, std::string& out, detail::Issues& issues) {
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        issues.add(path, "expected a non-empty texture path, got " + detail::typeName(value));
        return false;
    }
    const std::string text = value.get<std::string>();
    if (std::filesystem::path(text).is_absolute()) {
        issues.add(path, "texture path '" + text + "' must be relative to the block file");
        return false;
    }
    out = text;
    return true;
}

void readTextures(const Json& m, const std::string& path, BlockMaterialData& out, detail::Issues& issues) {
    const auto it = m.find("textures");
    const std::string at = detail::child(path, "textures");
    if (it == m.end()) {
        issues.add(at, "required field is missing");
        return;
    }
    if (it->is_string()) {
        std::string all;
        if (readTexturePath(*it, at, all, issues)) out.textures = {all, all, all};
        return;
    }
    if (!it->is_object()) {
        issues.add(at, "expected a texture path or an object with all/top/side/bottom");
        return;
    }
    detail::checkKeys(*it, at, {"all", "top", "side", "bottom"}, issues);
    std::string all;
    if (const auto a = it->find("all"); a != it->end()) {
        readTexturePath(*a, detail::child(at, "all"), all, issues);
    }
    for (int f = 0; f < 3; ++f) {
        if (const auto face = it->find(kFaceKeys[f]); face != it->end()) {
            readTexturePath(*face, detail::child(at, kFaceKeys[f]), out.textures[static_cast<size_t>(f)], issues);
        } else if (!all.empty()) {
            out.textures[static_cast<size_t>(f)] = all;
        } else if (!it->contains("all")) {
            issues.add(detail::child(at, kFaceKeys[f]), "required field is missing (or give \"all\")");
        }
    }
}

Json floatJson(float v) { return static_cast<double>(v); }

} // namespace

BlockFileData parseBlockFile(std::string_view json, std::string_view file, const voxel::ShapeLibrary* shapes) {
    const std::string fileName(file);
    const Json root = detail::parseJson(json, fileName);
    const detail::JsonLocator locator(json);
    detail::Issues issues(fileName, &locator);
    BlockFileData data;
    if (!detail::requireObject(root, "", issues)) {
        issues.throwIfAny();
    }
    detail::checkKeys(root, "", {"schema", "version", "uvPerMetre", "materials", "blocks"}, issues);
    std::string schema;
    if (detail::readString(root, "schema", "", schema, issues, true) && schema != BlockFileData::kSchema) {
        issues.add("/schema", "expected \"" + std::string(BlockFileData::kSchema) + "\", got \"" + schema + "\"");
    }
    uint32_t version = 0;
    if (!root.contains("version")) {
        issues.add("/version", "required field is missing");
    } else if (detail::readUint(root, "version", "", version, issues) &&
               version != static_cast<uint32_t>(BlockFileData::kVersion)) {
        issues.add("/version", "unsupported version " + std::to_string(version) + " (expected 1)");
    }
    if (detail::readFloat(root, "uvPerMetre", "", data.uvPerMetre, issues) && !(data.uvPerMetre > 0.0f)) {
        issues.add("/uvPerMetre", "expected a positive number of texture tiles per metre");
    }

    std::map<std::string, size_t, std::less<>> materialIndex;
    const auto materials = root.find("materials");
    if (materials == root.end() || !materials->is_array()) {
        issues.add("/materials", "expected an array of materials");
    } else {
        if (materials->size() >= voxel::VoxelPalette::kFallbackMaterial) {
            issues.add("/materials", "at most 65534 materials");
        }
        for (size_t i = 0; i < materials->size(); ++i) {
            const Json& m = (*materials)[i];
            const std::string path = detail::child("/materials", i);
            if (!detail::requireObject(m, path, issues)) continue;
            detail::checkKeys(m, path, {"name", "textures", "tint", "roughness", "metallic", "emissive"}, issues);
            BlockMaterialData md;
            if (detail::readString(m, "name", path, md.name, issues, true)) {
                if (validName(md.name, detail::child(path, "name"), issues) &&
                    !materialIndex.emplace(md.name, i).second) {
                    issues.add(detail::child(path, "name"), "duplicate material name '" + md.name + "'");
                }
            }
            readTextures(m, path, md, issues);
            detail::readVec<3>(m, "tint", path, md.tint, issues, 0.0f, 1.0f);
            detail::readFloat(m, "roughness", path, md.roughness, issues, 0.0f, 1.0f);
            detail::readFloat(m, "metallic", path, md.metallic, issues, 0.0f, 1.0f);
            detail::readFloat(m, "emissive", path, md.emissive, issues, 0.0f, 1000.0f);
            data.materials.push_back(std::move(md));
        }
    }

    const auto blocks = root.find("blocks");
    if (blocks == root.end() || !blocks->is_array()) {
        issues.add("/blocks", "expected an array of blocks");
    } else {
        std::set<std::string, std::less<>> names;
        size_t ids = 1; // id 0 is empty
        for (size_t i = 0; i < blocks->size(); ++i) {
            const Json& b = (*blocks)[i];
            const std::string path = detail::child("/blocks", i);
            if (!detail::requireObject(b, path, issues)) continue;
            detail::checkKeys(b, path, {"name", "material", "shape", "rotations", "collision"}, issues);
            BlockDefinitionData bd;
            if (detail::readString(b, "name", path, bd.name, issues, true)) {
                if (validName(bd.name, detail::child(path, "name"), issues) && !names.insert(bd.name).second) {
                    issues.add(detail::child(path, "name"), "duplicate block name '" + bd.name + "'");
                }
            }
            if (detail::readString(b, "material", path, bd.material, issues, true) &&
                materials != root.end() && materials->is_array() && !materialIndex.contains(bd.material)) {
                issues.add(detail::child(path, "material"), "unknown material '" + bd.material + "'");
            }
            if (detail::readString(b, "shape", path, bd.shape, issues) && shapes && !shapes->find(bd.shape)) {
                issues.add(detail::child(path, "shape"), "unknown shape '" + bd.shape + "'");
            }
            uint32_t rotations = 1;
            if (detail::readUint(b, "rotations", path, rotations, issues)) {
                if (rotations != 1 && rotations != 4) {
                    issues.add(detail::child(path, "rotations"),
                               "rotations must be 1 or 4, got " + std::to_string(rotations));
                    rotations = 1;
                }
            }
            bd.rotations = static_cast<uint8_t>(rotations);
            detail::readBool(b, "collision", path, bd.collision, issues);
            ids += bd.rotations;
            if (ids > kMaxIds) {
                issues.add(path, "too many block ids (at most 65534 including rotation variants)");
                break;
            }
            data.blocks.push_back(std::move(bd));
        }
    }
    issues.throwIfAny();
    return data;
}

BlockFileData loadBlockFile(const std::filesystem::path& path, const voxel::ShapeLibrary* shapes) {
    return parseBlockFile(detail::readTextFile(path), path.generic_string(), shapes);
}

std::string serializeBlockFile(const BlockFileData& data) {
    Json root = Json::object();
    root["schema"] = BlockFileData::kSchema;
    root["version"] = BlockFileData::kVersion;
    root["uvPerMetre"] = floatJson(data.uvPerMetre);
    Json materials = Json::array();
    for (const BlockMaterialData& m : data.materials) {
        Json j = Json::object();
        j["name"] = m.name;
        if (m.textures[0] == m.textures[1] && m.textures[1] == m.textures[2]) {
            j["textures"] = m.textures[0];
        } else {
            j["textures"] = Json{{"top", m.textures[0]}, {"side", m.textures[1]}, {"bottom", m.textures[2]}};
        }
        j["tint"] = Json::array({floatJson(m.tint.x), floatJson(m.tint.y), floatJson(m.tint.z)});
        j["roughness"] = floatJson(m.roughness);
        j["metallic"] = floatJson(m.metallic);
        j["emissive"] = floatJson(m.emissive);
        materials.push_back(std::move(j));
    }
    root["materials"] = std::move(materials);
    Json blocks = Json::array();
    for (const BlockDefinitionData& b : data.blocks) {
        blocks.push_back(Json{{"name", b.name},
                              {"material", b.material},
                              {"shape", b.shape},
                              {"rotations", b.rotations},
                              {"collision", b.collision}});
    }
    root["blocks"] = std::move(blocks);
    return root.dump(2) + "\n";
}

std::vector<std::filesystem::path> blockFileTextures(const BlockFileData& data, const std::filesystem::path& directory) {
    std::set<std::filesystem::path> out;
    for (const BlockMaterialData& m : data.materials) {
        for (const std::string& t : m.textures) {
            if (!t.empty()) out.insert((directory / t).lexically_normal());
        }
    }
    return {out.begin(), out.end()};
}

std::optional<voxel::Voxel> BlockCatalogue::find(std::string_view name) const {
    const auto it = ids.find(name);
    if (it == ids.end()) return std::nullopt;
    return it->second;
}

BlockCatalogue buildBlockCatalogue(const BlockFileData& data, const std::filesystem::path& directory,
                                   std::shared_ptr<const voxel::ShapeLibrary> shapes, std::string_view file) {
    if (!shapes) {
        shapes = std::make_shared<const voxel::ShapeLibrary>(voxel::ShapeLibrary::builtins());
    }
    detail::Issues issues{std::string(file)};
    // Data may be built in code (not only by parseBlockFile): validate it again here.
    if (data.materials.size() >= voxel::VoxelPalette::kFallbackMaterial) {
        issues.add("/materials", "at most 65534 materials");
    }
    if (!std::isfinite(data.uvPerMetre) || !(data.uvPerMetre > 0.0f)) {
        issues.add("/uvPerMetre", "expected a positive number of texture tiles per metre");
    }
    {
        std::set<std::string, std::less<>> seen;
        for (size_t i = 0; i < data.materials.size(); ++i) {
            const BlockMaterialData& m = data.materials[i];
            const std::string path = "/materials/" + std::to_string(i);
            if (m.name.empty()) issues.add(path + "/name", "empty material name");
            else if (m.name.find('@') != std::string::npos) issues.add(path + "/name", "name '" + m.name + "' contains '@'");
            else if (!seen.insert(m.name).second) issues.add(path + "/name", "duplicate material name '" + m.name + "'");
            for (size_t f = 0; f < 3; ++f) {
                if (m.textures[f].empty()) {
                    issues.add(path + "/textures/" + kFaceKeys[f], "empty texture path");
                }
            }
            const auto range = [&](const char* key, float v, float lo, float hi) {
                if (!(v >= lo && v <= hi)) { // also rejects NaN
                    issues.add(path + "/" + key, "value " + std::to_string(v) + " is outside [" + std::to_string(lo) +
                                                     ", " + std::to_string(hi) + "]");
                }
            };
            range("roughness", m.roughness, 0.0f, 1.0f);
            range("metallic", m.metallic, 0.0f, 1.0f);
            range("emissive", m.emissive, 0.0f, 1000.0f);
            for (int k = 0; k < 3; ++k) {
                if (!(m.tint[k] >= 0.0f && m.tint[k] <= 1.0f)) {
                    issues.add(path + "/tint/" + std::to_string(k), "value is outside [0, 1]");
                }
            }
        }
        std::set<std::string, std::less<>> blockNames;
        size_t ids = 1;
        for (size_t i = 0; i < data.blocks.size(); ++i) {
            const BlockDefinitionData& b = data.blocks[i];
            const std::string path = "/blocks/" + std::to_string(i);
            if (b.name.empty()) issues.add(path + "/name", "empty block name");
            else if (b.name.find('@') != std::string::npos) issues.add(path + "/name", "name '" + b.name + "' contains '@'");
            else if (!blockNames.insert(b.name).second) issues.add(path + "/name", "duplicate block name '" + b.name + "'");
            if (b.rotations != 1 && b.rotations != 4) {
                issues.add(path + "/rotations", "rotations must be 1 or 4, got " + std::to_string(b.rotations));
            }
            ids += b.rotations;
        }
        if (ids > kMaxIds) {
            issues.add("/blocks", "too many block ids (" + std::to_string(ids - 1) +
                                      "; at most 65534 including rotation variants)");
        }
    }
    issues.throwIfAny();
    BlockCatalogue c;
    c.uvPerMetre = data.uvPerMetre;
    c.palette.shapes = shapes;
    using LayerKey = std::tuple<std::string, float, float, float>;
    std::map<LayerKey, uint16_t> layerIndex;
    std::map<std::string, uint16_t, std::less<>> materialIndex;
    for (size_t i = 0; i < data.materials.size(); ++i) {
        const BlockMaterialData& m = data.materials[i];
        materialIndex.emplace(m.name, static_cast<uint16_t>(i));
        c.palette.colors.emplace_back(m.tint, 1.0f);
        std::array<uint16_t, 3> faces{};
        for (size_t f = 0; f < 3; ++f) {
            const std::filesystem::path texture = (directory / m.textures[f]).lexically_normal();
            const LayerKey key{texture.generic_string(), m.roughness, m.metallic, m.emissive};
            auto it = layerIndex.find(key);
            if (it == layerIndex.end()) {
                if (c.layers.size() >= 0xFFFF) fail("block catalogue: more than 65535 texture layers");
                it = layerIndex.emplace(key, static_cast<uint16_t>(c.layers.size())).first;
                c.layers.push_back(texture);
                c.layerParams.push_back({m.roughness, m.metallic, m.emissive});
                c.layerOrigins.push_back("/materials/" + std::to_string(i) + "/textures/" + kFaceKeys[f]);
            }
            faces[f] = it->second;
        }
        c.palette.faceLayers.push_back(faces);
    }
    c.names.emplace_back();
    c.palette.blocks.emplace_back();
    for (size_t i = 0; i < data.blocks.size(); ++i) {
        const BlockDefinitionData& b = data.blocks[i];
        const std::string path = "/blocks/" + std::to_string(i);
        const auto shape = shapes->find(b.shape);
        const auto material = materialIndex.find(b.material);
        if (!shape) issues.add(path + "/shape", "unknown shape '" + b.shape + "'");
        if (material == materialIndex.end()) issues.add(path + "/material", "unknown material '" + b.material + "'");
        if (!shape || material == materialIndex.end()) continue;
        for (uint8_t r = 0; r < b.rotations; ++r) {
            const std::string name = r == 0 ? b.name : b.name + "@" + std::to_string(r);
            const auto id = static_cast<voxel::Voxel>(c.palette.blocks.size());
            if (!c.ids.emplace(name, id).second) issues.add(path + "/name", "duplicate block name '" + name + "'");
            c.names.push_back(name);
            c.palette.blocks.push_back(
                {material->second, *shape, r, b.collision ? uint8_t(0) : voxel::BlockState::kNoCollision});
        }
    }
    issues.throwIfAny();
    return c;
}

BlockCatalogue loadBlockCatalogue(const std::filesystem::path& path, std::shared_ptr<const voxel::ShapeLibrary> shapes) {
    if (!shapes) {
        shapes = std::make_shared<const voxel::ShapeLibrary>(voxel::ShapeLibrary::builtins());
    }
    const BlockFileData data = loadBlockFile(path, shapes.get());
    return buildBlockCatalogue(data, path.parent_path(), std::move(shapes), path.generic_string());
}

std::vector<DataIssue> checkBlockLayerSizes(const BlockCatalogue& catalogue, const std::vector<glm::uvec2>& sizes,
                                            std::string_view file) {
    if (sizes.size() != catalogue.layers.size() || catalogue.layerOrigins.size() != catalogue.layers.size()) {
        fail("checkBlockLayerSizes: " + std::to_string(sizes.size()) + " sizes and " +
             std::to_string(catalogue.layerOrigins.size()) + " origins for " + std::to_string(catalogue.layers.size()) +
             " layers");
    }
    std::vector<DataIssue> out;
    const auto text = [](const glm::uvec2& s) { return std::to_string(s.x) + "x" + std::to_string(s.y); };
    // Reference size: the first square layer (a non-square layer 0 is reported itself and does
    // not become the reference).
    size_t reference = sizes.size();
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i].x == sizes[i].y) {
            reference = i;
            break;
        }
    }
    for (size_t i = 0; i < sizes.size(); ++i) {
        const std::string name = catalogue.layers[i].filename().generic_string();
        DataIssue issue{std::string(file), 0, 0, catalogue.layerOrigins[i], {}};
        if (sizes[i].x != sizes[i].y) {
            issue.message = text(sizes[i]) + " (" + name + ") is not square (texture-array layers are square)";
        } else if (i != reference && sizes[i] != sizes[reference]) {
            issue.message = text(sizes[i]) + " (" + name + ") differs from the texture array size " +
                            text(sizes[reference]) + " (first layer: " + catalogue.layerOrigins[reference] + ")";
        } else {
            continue;
        }
        out.push_back(std::move(issue));
    }
    return out;
}

std::vector<DataIssue> checkBlockLayerCount(const BlockCatalogue& catalogue, uint32_t maxLayers, std::string_view file) {
    if (catalogue.layers.size() <= maxLayers) return {};
    return {DataIssue{std::string(file), 0, 0, "/materials",
                      std::to_string(catalogue.layers.size()) + " texture-array layers exceed the limit of " +
                          std::to_string(maxLayers) + " (maxImageArrayLayers; distinct textures and parameters per " +
                          "layer count separately)"}};
}

} // namespace zodar
