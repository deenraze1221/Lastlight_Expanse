#include "AssetManager/AssetRegistry.h"

#include "Assets/ImageData.h"
#include "Assets/ModelData.h"
#include "Core/Error.h"
#include "SceneFile/JsonRead.h"
#include "SceneFile/MaterialFile.h"
#include "SceneFile/SceneFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <fstream>
#include <set>
#include <system_error>

namespace zodar {

using detail::Json;

// --- Types -------------------------------------------------------------------------------

const char* toString(AssetType type) {
    switch (type) {
    case AssetType::Texture: return "texture";
    case AssetType::Model: return "model";
    case AssetType::Material: return "material";
    case AssetType::Scene: return "scene";
    case AssetType::Prefab: return "prefab";
    }
    return "texture";
}

std::optional<AssetType> parseAssetType(std::string_view name) {
    for (const AssetType t : {AssetType::Texture, AssetType::Model, AssetType::Material, AssetType::Scene,
                              AssetType::Prefab}) {
        if (name == toString(t)) {
            return t;
        }
    }
    return std::nullopt;
}

namespace {

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const char* samplingName(ModelSampling s) {
    switch (s) {
    case ModelSampling::PixelArt: return "pixelArt";
    case ModelSampling::Nearest: return "nearest";
    case ModelSampling::Linear: return "linear";
    case ModelSampling::File: break;
    }
    return "file";
}

std::optional<ModelSampling> parseSampling(std::string_view name) {
    for (const ModelSampling s : {ModelSampling::File, ModelSampling::PixelArt, ModelSampling::Nearest,
                                  ModelSampling::Linear}) {
        if (name == samplingName(s)) {
            return s;
        }
    }
    return std::nullopt;
}

std::optional<TextureUsage> parseUsage(std::string_view name) {
    for (const TextureUsage u : {TextureUsage::Color, TextureUsage::Data, TextureUsage::Normal}) {
        if (name == toString(u)) {
            return u;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<AssetType> detectAssetType(const std::filesystem::path& file) {
    const std::string name = lower(file.filename().string());
    if (name.ends_with(".material.json")) return AssetType::Material;
    if (name.ends_with(".scene.json")) return AssetType::Scene;
    if (name.ends_with(".prefab.json")) return AssetType::Prefab;
    const std::string ext = lower(file.extension().string());
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") return AssetType::Texture;
    if (ext == ".gltf" || ext == ".glb") return AssetType::Model;
    return std::nullopt;
}

std::string defaultAssetId(const std::filesystem::path& file) {
    std::string name = file.filename().string();
    for (const char* suffix : {".material.json", ".scene.json", ".prefab.json"}) {
        if (lower(name).ends_with(suffix)) {
            return name.substr(0, name.size() - std::string_view(suffix).size());
        }
    }
    return file.stem().string();
}

TextureLoadSettings textureLoadSettings(const AssetEntry& entry) {
    return {entry.usage, entry.mips};
}

ModelLoadSettings modelLoadSettings(const AssetEntry& entry) {
    ModelLoadSettings s;
    switch (entry.sampling) {
    case ModelSampling::PixelArt: s.samplerOverride = gfx::SamplerDesc::pixelArt(); break;
    case ModelSampling::Nearest: s.samplerOverride = gfx::SamplerDesc::nearest(); break;
    case ModelSampling::Linear: s.samplerOverride = gfx::SamplerDesc::linear(); break;
    case ModelSampling::File: break;
    }
    s.useCompiledTextures = entry.compile;
    return s;
}

// --- Registry file -----------------------------------------------------------------------

AssetRegistry AssetRegistry::parse(std::string_view json, const std::filesystem::path& file) {
    const std::string fileName = file.generic_string();
    const Json root = detail::parseJson(json, fileName);
    const detail::JsonLocator locator(json);
    detail::Issues issues(fileName, &locator);
    AssetRegistry registry(file.parent_path());
    if (!detail::requireObject(root, "", issues)) {
        issues.throwIfAny();
    }
    detail::checkKeys(root, "", {"schema", "version", "assets"}, issues);
    std::string schema;
    if (detail::readString(root, "schema", "", schema, issues, true) && schema != kSchema) {
        issues.add("/schema", "expected \"" + std::string(kSchema) + "\", got \"" + schema + "\"");
    }
    uint32_t version = 0;
    if (detail::readUint(root, "version", "", version, issues) && version != kVersion) {
        issues.add("/version", "unsupported version " + std::to_string(version) + " (expected 1)");
    }
    const auto assets = root.find("assets");
    if (assets == root.end() || !assets->is_array()) {
        issues.add("/assets", "expected an array of assets");
        issues.throwIfAny();
    }
    std::set<std::string> ids;
    for (std::size_t i = 0; i < assets->size(); ++i) {
        const Json& a = (*assets)[i];
        const std::string path = detail::child("/assets", i);
        if (!detail::requireObject(a, path, issues)) {
            continue;
        }
        AssetEntry e;
        detail::readString(a, "id", path, e.id, issues, true);
        std::string type;
        if (detail::readString(a, "type", path, type, issues, true)) {
            if (const auto t = parseAssetType(type)) {
                e.type = *t;
            } else {
                issues.add(detail::child(path, "type"),
                           "unknown asset type '" + type + "' (expected texture, model, material, scene or prefab)");
            }
        }
        if (e.type == AssetType::Texture) {
            detail::checkKeys(a, path, {"id", "type", "source", "usage", "mips", "hash", "dependencies", "outputs"},
                              issues);
        } else if (e.type == AssetType::Model) {
            detail::checkKeys(a, path,
                              {"id", "type", "source", "sampling", "compile", "hash", "dependencies", "outputs"}, issues);
        } else {
            detail::checkKeys(a, path, {"id", "type", "source", "hash", "dependencies", "outputs"}, issues);
        }
        detail::readString(a, "source", path, e.source, issues, true);
        std::string usage;
        if (detail::readString(a, "usage", path, usage, issues)) {
            if (const auto u = parseUsage(usage)) {
                e.usage = *u;
            } else {
                issues.add(detail::child(path, "usage"), "unknown usage '" + usage + "' (expected color, data or normal)");
            }
        }
        detail::readBool(a, "mips", path, e.mips, issues);
        std::string sampling;
        if (detail::readString(a, "sampling", path, sampling, issues)) {
            if (const auto s = parseSampling(sampling)) {
                e.sampling = *s;
            } else {
                issues.add(detail::child(path, "sampling"),
                           "unknown sampling '" + sampling + "' (expected file, pixelArt, nearest or linear)");
            }
        }
        detail::readBool(a, "compile", path, e.compile, issues);
        detail::readString(a, "hash", path, e.hash, issues);
        for (const char* key : {"dependencies", "outputs"}) {
            const auto it = a.find(key);
            if (it == a.end()) {
                continue;
            }
            std::vector<std::string>& out = std::string_view(key) == "dependencies" ? e.dependencies : e.outputs;
            if (!it->is_array()) {
                issues.add(detail::child(path, key), "expected an array of paths");
                continue;
            }
            for (std::size_t j = 0; j < it->size(); ++j) {
                if (!(*it)[j].is_string()) {
                    issues.add(detail::child(detail::child(path, key), j), "expected a path string");
                    continue;
                }
                out.push_back((*it)[j].get<std::string>());
            }
        }
        if (!e.id.empty() && !ids.insert(e.id).second) {
            issues.add(detail::child(path, "id"), "duplicate asset id '" + e.id + "'");
        }
        registry.m_entries.push_back(std::move(e));
    }
    issues.throwIfAny();
    std::ranges::sort(registry.m_entries, {}, &AssetEntry::id);
    return registry;
}

AssetRegistry AssetRegistry::load(const std::filesystem::path& file) {
    return parse(detail::readTextFile(file), file);
}

std::string AssetRegistry::serialize() const {
    Json root = Json::object();
    root["schema"] = kSchema;
    root["version"] = kVersion;
    Json assets = Json::array();
    for (const AssetEntry& e : m_entries) {
        Json a = Json::object();
        a["id"] = e.id;
        a["type"] = toString(e.type);
        a["source"] = e.source;
        if (e.type == AssetType::Texture) {
            a["usage"] = toString(e.usage);
            a["mips"] = e.mips;
        } else if (e.type == AssetType::Model) {
            a["sampling"] = samplingName(e.sampling);
            a["compile"] = e.compile;
        }
        if (!e.hash.empty()) {
            a["hash"] = e.hash;
        }
        a["dependencies"] = e.dependencies;
        if (!e.outputs.empty()) {
            a["outputs"] = e.outputs;
        }
        assets.push_back(std::move(a));
    }
    root["assets"] = std::move(assets);
    return root.dump(2) + "\n";
}

void AssetRegistry::save(const std::filesystem::path& file) const {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << serialize();
    if (!out) {
        throw Error("cannot write asset registry '" + file.generic_string() + "'");
    }
}

const AssetEntry* AssetRegistry::find(std::string_view id) const {
    const auto it = std::ranges::find(m_entries, id, &AssetEntry::id);
    return it == m_entries.end() ? nullptr : &*it;
}

const AssetEntry& AssetRegistry::at(std::string_view id) const {
    if (const AssetEntry* e = find(id)) {
        return *e;
    }
    throw Error("unknown asset id '" + std::string(id) + "'");
}

void AssetRegistry::upsert(AssetEntry entry) {
    const auto it = std::ranges::lower_bound(m_entries, entry.id, {}, &AssetEntry::id);
    if (it != m_entries.end() && it->id == entry.id) {
        *it = std::move(entry);
    } else {
        m_entries.insert(it, std::move(entry));
    }
}

bool AssetRegistry::remove(std::string_view id) {
    return std::erase_if(m_entries, [&](const AssetEntry& e) { return e.id == id; }) > 0;
}

// --- Dependencies and hash ---------------------------------------------------------------

namespace {

std::filesystem::path normal(const std::filesystem::path& p) {
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(p, ec);
    return (ec ? p : abs).lexically_normal();
}

std::string percentDecode(const std::string& uri) {
    std::string out;
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            out += static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += uri[i];
        }
    }
    return out;
}

std::vector<uint8_t> readBytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw Error("cannot open '" + file.generic_string() + "'");
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// JSON chunk of a .gltf or .glb file.
Json gltfJson(const std::filesystem::path& file) {
    const std::vector<uint8_t> bytes = readBytes(file);
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (bytes.size() >= 20 && text.substr(0, 4) == "glTF") {
        uint32_t length = 0;
        std::memcpy(&length, bytes.data() + 12, 4);
        if (20 + static_cast<std::size_t>(length) > bytes.size() || text.substr(16, 4) != "JSON") {
            throw Error("'" + file.generic_string() + "': malformed GLB header");
        }
        text = text.substr(20, length);
    }
    return detail::parseJson(text, file.generic_string());
}

void addMaterialTextures(const MaterialData& m, const std::filesystem::path& directory,
                         std::vector<std::filesystem::path>& out) {
    for (const MaterialTextureData* t : {&m.baseColorTexture, &m.metallicRoughnessTexture, &m.normalTexture,
                                         &m.occlusionTexture, &m.emissiveTexture}) {
        if (!t->empty()) {
            const std::filesystem::path p(t->path);
            out.push_back(p.is_absolute() ? p : directory / p);
        }
    }
}

// Prefab files referenced by a scene/prefab, recursively (cycle-safe).
void addPrefabs(const std::filesystem::path& file, std::set<std::filesystem::path>& seen,
                std::vector<std::filesystem::path>& out) {
    if (!std::filesystem::exists(file)) {
        return;
    }
    const Json root = detail::parseJson(detail::readTextFile(file), file.generic_string());
    const auto entities = root.find("entities");
    if (entities == root.end() || !entities->is_array()) {
        return;
    }
    for (const Json& e : *entities) {
        const auto prefab = e.is_object() ? e.find("prefab") : e.end();
        if (prefab == e.end() || !prefab->is_string()) {
            continue;
        }
        const std::filesystem::path p = normal(file.parent_path() / prefab->get<std::string>());
        out.push_back(p);
        if (seen.insert(p).second) {
            addPrefabs(p, seen, out);
        }
    }
}

std::vector<std::filesystem::path> dependenciesOf(AssetType type, const std::filesystem::path& source) {
    std::vector<std::filesystem::path> deps;
    const std::filesystem::path dir = source.parent_path();
    switch (type) {
    case AssetType::Texture: break;
    case AssetType::Model: {
        const Json root = gltfJson(source);
        for (const char* key : {"buffers", "images"}) {
            const auto list = root.find(key);
            if (list == root.end() || !list->is_array()) {
                continue;
            }
            for (const Json& item : *list) {
                const auto uri = item.is_object() ? item.find("uri") : item.end();
                if (uri != item.end() && uri->is_string() && !uri->get<std::string>().starts_with("data:")) {
                    deps.push_back(dir / percentDecode(uri->get<std::string>()));
                }
            }
        }
        break;
    }
    case AssetType::Material: addMaterialTextures(loadMaterialFile(source), dir, deps); break;
    case AssetType::Scene:
    case AssetType::Prefab: {
        std::set<std::filesystem::path> seen{normal(source)};
        addPrefabs(source, seen, deps);
        const SceneDocument doc = loadSceneFile(source);
        for (const SceneMaterial& m : doc.materials) {
            if (!m.source.empty()) {
                const std::filesystem::path file = dir / m.source;
                deps.push_back(file);
                addMaterialTextures(m.data, file.parent_path(), deps);
            } else {
                addMaterialTextures(m.data, sceneMaterialDirectory(m, dir), deps);
            }
        }
        break;
    }
    }
    for (std::filesystem::path& d : deps) {
        d = normal(d);
    }
    std::ranges::sort(deps);
    deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
    return deps;
}

struct Fnv {
    uint64_t h = 1469598103934665603ull;
    void add(std::string_view bytes) {
        for (const char c : bytes) {
            h ^= static_cast<uint8_t>(c);
            h *= 1099511628211ull;
        }
    }
};

std::string relativePath(const std::filesystem::path& file, const std::filesystem::path& base) {
    return normal(file).lexically_relative(normal(base)).generic_string();
}

} // namespace

AssetFingerprint fingerprintAsset(AssetType type, const std::filesystem::path& source) {
    const std::filesystem::path file = normal(source);
    AssetFingerprint f;
    f.dependencies = dependenciesOf(type, file);
    // Paths are hashed relative to the source's directory, so moving a whole asset folder
    // keeps its hashes.
    Fnv fnv;
    const auto addFile = [&](const std::filesystem::path& p) {
        fnv.add(relativePath(p, file.parent_path()));
        fnv.add(std::string_view("\0", 1));
        if (!std::filesystem::exists(p)) {
            fnv.add("<missing>");
            return;
        }
        const std::vector<uint8_t> bytes = readBytes(p);
        fnv.add(std::to_string(bytes.size()));
        fnv.add(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    };
    addFile(file);
    for (const std::filesystem::path& d : f.dependencies) {
        addFile(d);
        if (!std::filesystem::exists(d)) {
            f.missing.push_back(d);
        }
    }
    char text[32];
    std::snprintf(text, sizeof(text), "fnv1a64:%016llx", static_cast<unsigned long long>(fnv.h));
    f.hash = text;
    return f;
}

const AssetEntry& importAsset(AssetRegistry& registry, const std::filesystem::path& file,
                              const AssetImportOptions& options) {
    const std::filesystem::path abs = normal(file);
    if (!std::filesystem::is_regular_file(abs)) {
        throw Error("cannot import '" + file.generic_string() + "': file not found");
    }
    const std::string source = relativePath(abs, registry.directory());
    if (source.empty() || source.starts_with("..")) {
        throw Error("cannot import '" + file.generic_string() + "': outside the registry directory '" +
             normal(registry.directory()).generic_string() + "'");
    }
    const std::optional<AssetType> type = options.type ? options.type : detectAssetType(abs);
    if (!type) {
        throw Error("cannot import '" + file.generic_string() +
             "': unknown asset type (use --type texture|model|material|scene|prefab)");
    }
    if ((options.usage || options.mips) && *type != AssetType::Texture) {
        throw Error("--usage/--no-mips apply to textures only");
    }
    if ((options.sampling || options.compile) && *type != AssetType::Model) {
        throw Error("--sampling/--compile apply to models only");
    }

    // An existing entry for the same source keeps its id and settings.
    AssetEntry entry;
    const auto same = std::ranges::find(registry.entries(), source, &AssetEntry::source);
    if (!options.id.empty()) {
        if (const AssetEntry* e = registry.find(options.id)) {
            entry = *e;
        }
        entry.id = options.id;
    } else if (same != registry.entries().end()) {
        entry = *same;
    } else {
        entry.id = defaultAssetId(abs);
        if (const AssetEntry* e = registry.find(entry.id)) {
            throw Error("asset id '" + entry.id + "' is already used by '" + e->source + "' (pass --id)");
        }
    }
    entry.type = *type;
    entry.source = source;
    if (options.usage) entry.usage = *options.usage;
    if (options.mips) entry.mips = *options.mips;
    if (options.sampling) entry.sampling = *options.sampling;
    if (options.compile) entry.compile = *options.compile;

    const AssetFingerprint f = fingerprintAsset(*type, abs);
    if (!f.missing.empty()) {
        std::string list;
        for (const auto& m : f.missing) {
            list += (list.empty() ? "" : ", ") + relativePath(m, registry.directory());
        }
        throw Error("cannot import '" + source + "': missing dependencies: " + list);
    }
    entry.hash = f.hash;
    entry.dependencies.clear();
    for (const auto& d : f.dependencies) {
        entry.dependencies.push_back(relativePath(d, registry.directory()));
    }
    const std::string id = entry.id;
    registry.upsert(std::move(entry));
    return registry.at(id);
}

// --- Checks ------------------------------------------------------------------------------

namespace {

void addTexture(AssetCheckResult& r, const AssetCheckOptions& options, std::string name, uint32_t w, uint32_t h) {
    r.textures.push_back({name, w, h, classifyTextureSize(w, h)});
    if (options.checkTextureSizes) {
        if (auto warning = checkTextureSize(name, w, h)) {
            r.warnings.push_back(std::move(*warning));
        }
    }
}

void checkImageFile(AssetCheckResult& r, const AssetCheckOptions& options, const std::filesystem::path& file,
                    const std::filesystem::path& registryDir) {
    if (!std::filesystem::exists(file)) {
        return; // reported as a missing dependency
    }
    const ImageData image = loadImage(file, ColorSpace::Srgb);
    addTexture(r, options, relativePath(file, registryDir), image.width, image.height);
}

double texelDensity(const ModelData& data) {
    double texels = 0.0;
    double area = 0.0;
    for (const ModelNode& node : data.nodes) {
        if (node.mesh < 0 || static_cast<size_t>(node.mesh) >= data.meshes.size()) {
            continue;
        }
        const MeshData& mesh = data.meshes[static_cast<size_t>(node.mesh)];
        for (const Submesh& sub : mesh.submeshes) {
            if (sub.materialIndex >= data.materials.size()) {
                continue;
            }
            const int image = data.materials[sub.materialIndex].baseColor.image;
            if (image < 0 || static_cast<size_t>(image) >= data.images.size()) {
                continue;
            }
            const double size = double(data.images[static_cast<size_t>(image)].width) *
                                double(data.images[static_cast<size_t>(image)].height);
            for (uint32_t i = sub.firstIndex; i + 2 < sub.firstIndex + sub.indexCount; i += 3) {
                const Vertex& a = mesh.vertices[mesh.indices[i]];
                const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
                const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
                const glm::vec3 pa = glm::vec3(node.world * glm::vec4(a.position, 1.0f));
                const glm::vec3 pb = glm::vec3(node.world * glm::vec4(b.position, 1.0f));
                const glm::vec3 pc = glm::vec3(node.world * glm::vec4(c.position, 1.0f));
                area += 0.5 * glm::length(glm::cross(pb - pa, pc - pa));
                const glm::vec2 u = b.uv0 - a.uv0;
                const glm::vec2 v = c.uv0 - a.uv0;
                texels += 0.5 * std::abs(double(u.x) * v.y - double(u.y) * v.x) * size;
            }
        }
    }
    return area > 0.0 && texels > 0.0 ? std::sqrt(texels / area) : 0.0;
}

} // namespace

AssetCheckResult checkAsset(const AssetRegistry& registry, const AssetEntry& entry, const AssetCheckOptions& options) {
    AssetCheckResult r;
    r.id = entry.id;
    r.type = entry.type;
    r.source = entry.source;
    const std::filesystem::path dir = registry.directory();
    const std::filesystem::path file = normal(registry.sourcePath(entry));
    if (!std::filesystem::is_regular_file(file)) {
        r.errors.push_back("source missing: " + entry.source);
        return r;
    }
    try {
        const AssetFingerprint f = fingerprintAsset(entry.type, file);
        for (const auto& m : f.missing) {
            r.errors.push_back("dependency missing: " + relativePath(m, dir));
        }
        if (f.hash != entry.hash) {
            r.warnings.push_back("stale: source or dependencies changed since import (run: zodar.py import " +
                                 entry.source + ")");
        }
    } catch (const std::exception& e) {
        r.errors.push_back(e.what());
        return r;
    }
    try {
        switch (entry.type) {
        case AssetType::Texture: checkImageFile(r, options, file, dir); break;
        case AssetType::Model: {
            GltfLoadOptions load;
            load.useCompiledTextures = false;
            load.generateMips = false;
            const ModelData data = loadGltf(file, load);
            for (const std::string& w : data.warnings) {
                r.warnings.push_back("glTF: " + w);
            }
            r.meshes = static_cast<uint32_t>(data.meshes.size());
            r.materials = static_cast<uint32_t>(data.materials.size());
            for (const MeshData& m : data.meshes) {
                r.vertices += static_cast<uint32_t>(m.vertices.size());
                r.triangles += static_cast<uint32_t>(m.indices.size() / 3);
            }
            for (size_t i = 0; i < data.images.size(); ++i) {
                const ImageData& image = data.images[i];
                addTexture(r, options,
                           entry.source + "/" + (image.name.empty() ? "image" + std::to_string(i) : image.name),
                           image.width, image.height);
            }
            r.texelDensity = texelDensity(data);
            const double target = options.texelDensityTarget;
            const double tol = options.texelDensityTolerance;
            if (r.texelDensity > 0.0 && tol > 0.0 &&
                (r.texelDensity < target / tol || r.texelDensity > target * tol)) {
                char text[160];
                std::snprintf(text, sizeof(text), "texel density %.1f texels/m outside %.0f..%.0f (target %.0f)",
                              r.texelDensity, target / tol, target * tol, target);
                r.warnings.push_back(text);
            }
            if (entry.compile && entry.outputs.empty()) {
                r.warnings.push_back("compile is set but no compiled outputs are recorded (run: zodar.py import " +
                                     entry.source + " --compile)");
            }
            for (const std::string& out : entry.outputs) {
                if (!std::filesystem::exists(dir / out)) {
                    r.warnings.push_back("compiled output missing: " + out);
                }
            }
            break;
        }
        case AssetType::Material: {
            const MaterialData m = loadMaterialFile(file);
            r.materials = 1;
            std::vector<std::filesystem::path> textures;
            addMaterialTextures(m, file.parent_path(), textures);
            std::set<std::filesystem::path> seen;
            for (const auto& t : textures) {
                if (seen.insert(normal(t)).second) {
                    checkImageFile(r, options, normal(t), dir);
                }
            }
            break;
        }
        case AssetType::Scene:
        case AssetType::Prefab: {
            const SceneDocument doc = loadSceneFile(file);
            r.materials = static_cast<uint32_t>(doc.materials.size());
            std::set<std::filesystem::path> seen;
            for (const SceneMaterial& m : doc.materials) {
                std::vector<std::filesystem::path> textures;
                if (!m.source.empty()) {
                    addMaterialTextures(m.data, (file.parent_path() / m.source).parent_path(), textures);
                } else {
                    addMaterialTextures(m.data, sceneMaterialDirectory(m, file.parent_path()), textures);
                }
                for (const auto& t : textures) {
                    if (seen.insert(normal(t)).second) {
                        checkImageFile(r, options, normal(t), dir);
                    }
                }
            }
            break;
        }
        }
    } catch (const std::exception& e) {
        r.errors.push_back(e.what());
    }
    return r;
}

std::vector<AssetCheckResult> checkRegistry(const AssetRegistry& registry, const AssetCheckOptions& options) {
    std::vector<AssetCheckResult> results;
    results.reserve(registry.entries().size());
    for (const AssetEntry& e : registry.entries()) {
        results.push_back(checkAsset(registry, e, options));
    }
    return results;
}

std::string assetReportJson(const AssetRegistry& registry, const std::vector<AssetCheckResult>& results) {
    Json root = Json::object();
    root["schema"] = "zodar.asset_report";
    root["version"] = 1;
    root["registry"] = normal(registry.directory() / AssetRegistry::kFileName).generic_string();
    size_t ok = 0, warnings = 0, errors = 0;
    Json assets = Json::array();
    for (const AssetCheckResult& r : results) {
        const std::string status = r.status();
        ok += status == "ok";
        warnings += status == "warning";
        errors += status == "error";
        Json a = Json::object();
        a["id"] = r.id;
        a["type"] = toString(r.type);
        a["source"] = r.source;
        a["status"] = status;
        a["errors"] = r.errors;
        a["warnings"] = r.warnings;
        Json textures = Json::array();
        for (const AssetTextureInfo& t : r.textures) {
            textures.push_back(Json{{"name", t.name}, {"width", t.width}, {"height", t.height},
                                    {"sizeClass", toString(t.sizeClass)}});
        }
        a["textures"] = std::move(textures);
        a["meshes"] = r.meshes;
        a["vertices"] = r.vertices;
        a["triangles"] = r.triangles;
        a["materials"] = r.materials;
        a["texelDensity"] = std::round(r.texelDensity * 100.0) / 100.0;
        assets.push_back(std::move(a));
    }
    root["summary"] = Json{{"assets", results.size()}, {"ok", ok}, {"warnings", warnings}, {"errors", errors}};
    root["assets"] = std::move(assets);
    return root.dump(2) + "\n";
}

} // namespace zodar
