#include "AssetManager/AssetRegistry.h"

#include "Assets/ImageData.h"
#include "Assets/ModelData.h"
#include "Core/Error.h"
#include "SceneFile/BlockFile.h"
#include "SceneFile/JsonRead.h"
#include "SceneFile/MaterialFile.h"
#include "SceneFile/SceneFile.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
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
    case AssetType::Blocks: return "blocks";
    }
    return "texture";
}

std::optional<AssetType> parseAssetType(std::string_view name) {
    for (const AssetType t : {AssetType::Texture, AssetType::Model, AssetType::Material, AssetType::Scene,
                              AssetType::Prefab, AssetType::Blocks}) {
        if (name == toString(t)) {
            return t;
        }
    }
    return std::nullopt;
}

namespace {

// printf-style formatting into a std::string (no fixed buffer, no truncation).
std::string formatText(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int size = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string text(size > 0 ? static_cast<size_t>(size) : 0, '\0');
    if (size > 0) {
        std::vsnprintf(text.data(), text.size() + 1, format, args);
    }
    va_end(args);
    return text;
}

// True for absolute paths and paths whose first component is "..".
bool escapesDirectory(const std::string& source) {
    const std::filesystem::path p(source);
    return p.is_absolute() || p.has_root_name() || (!p.empty() && *p.begin() == "..");
}


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
    if (name.ends_with(".blocks.json")) return AssetType::Blocks;
    const std::string ext = lower(file.extension().string());
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") return AssetType::Texture;
    if (ext == ".gltf" || ext == ".glb") return AssetType::Model;
    return std::nullopt;
}

std::string defaultAssetId(const std::filesystem::path& file) {
    std::string name = file.filename().string();
    for (const char* suffix : {".material.json", ".scene.json", ".prefab.json", ".blocks.json"}) {
        if (lower(name).ends_with(suffix)) {
            return name.substr(0, name.size() - std::string_view(suffix).size());
        }
    }
    return file.stem().string();
}

// --- Palettes ----------------------------------------------------------------------------

bool Palette::contains(uint32_t rgb) const {
    return std::ranges::binary_search(colors, rgb);
}

std::optional<uint32_t> parseHexColor(std::string_view text) {
    if (text.size() != 7 || text[0] != '#') {
        return std::nullopt;
    }
    uint32_t value = 0;
    for (const char c : text.substr(1)) {
        uint32_t digit = 0;
        if (c >= '0' && c <= '9') digit = uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') digit = uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = uint32_t(c - 'A' + 10);
        else return std::nullopt;
        value = value * 16 + digit;
    }
    return value;
}

std::string hexColor(uint32_t rgb) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%06x", rgb & 0xFFFFFFu);
    return text;
}

Palette loadPalette(const PaletteSource& source, const std::filesystem::path& directory, std::string name) {
    Palette p;
    p.name = std::move(name);
    if (!source.swatch.empty()) {
        const std::filesystem::path file =
            std::filesystem::path(source.swatch).is_absolute() ? std::filesystem::path(source.swatch)
                                                               : directory / source.swatch;
        const ImageData image = loadImage(file, ColorSpace::Srgb); // throws with the file name
        const std::vector<uint8_t>& px = image.levels.at(0);
        for (size_t i = 0; i + 3 < px.size(); i += 4) {
            if (px[i + 3] != 0) {
                p.colors.push_back(uint32_t(px[i]) << 16 | uint32_t(px[i + 1]) << 8 | px[i + 2]);
            }
        }
    }
    for (const std::string& c : source.colors) {
        const auto rgb = parseHexColor(c);
        if (!rgb) {
            throw Error("palette '" + p.name + "': malformed colour '" + c + "' (expected #rrggbb)");
        }
        p.colors.push_back(*rgb);
    }
    std::ranges::sort(p.colors);
    p.colors.erase(std::unique(p.colors.begin(), p.colors.end()), p.colors.end());
    if (p.colors.empty()) {
        throw Error("palette '" + p.name + "' has no colours");
    }
    return p;
}

PaletteSource parsePaletteArgument(std::string_view text) {
    PaletteSource s;
    if (text.starts_with('#')) {
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t end = std::min(text.find(',', start), text.size());
            s.colors.emplace_back(text.substr(start, end - start));
            start = end + 1;
        }
    } else {
        s.swatch = std::string(text);
    }
    return s;
}

namespace {

// "palette" values: "file.png" | ["#rrggbb", ...].
bool readPaletteSource(const Json& value, const std::string& path, PaletteSource& out, detail::Issues& issues) {
    PaletteSource s;
    if (value.is_string() && !value.get<std::string>().empty()) {
        s.swatch = value.get<std::string>();
    } else if (value.is_array() && !value.empty()) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            const Json& c = value[i];
            if (!c.is_string() || !parseHexColor(c.get<std::string>())) {
                issues.add(detail::child(path, i), "expected a colour \"#rrggbb\", got " +
                                                       (c.is_string() ? "\"" + c.get<std::string>() + "\""
                                                                      : detail::typeName(c)));
                continue;
            }
            s.colors.push_back(c.get<std::string>());
        }
    } else {
        issues.add(path, "expected a PNG swatch path or a non-empty array of \"#rrggbb\" colours");
        return false;
    }
    out = std::move(s);
    return true;
}

// "" when `assetClass` names a budget, otherwise the problem.
std::string classProblem(const std::string& assetClass, const std::map<std::string, uint32_t>& budgets) {
    if (budgets.empty()) {
        return "class '" + assetClass + "' but the registry has no \"budgets\"";
    }
    if (budgets.contains(assetClass)) {
        return {};
    }
    std::string known;
    for (const auto& [name, max] : budgets) {
        known += (known.empty() ? "" : ", ") + name;
    }
    return "unknown asset class '" + assetClass + "' (budgets: " + known + ")";
}

// "" when `textureClass` names a class of the registry's texture standard, otherwise the problem.
std::string textureClassProblem(const std::string& textureClass, const std::optional<TextureStandard>& standard) {
    if (!standard) {
        return "textureClass '" + textureClass + "' but the registry has no \"textureStandards\"";
    }
    if (standard->find(textureClass)) {
        return {};
    }
    std::string known;
    for (const TextureClassRule& c : standard->classes) {
        known += (known.empty() ? "" : ", ") + c.name;
    }
    return "unknown texture class '" + textureClass + "' (classes: " + known + ")";
}

// "textureStandards": { "classes": { name: [sizes] }, "defaultClass", "prohibitedSize",
// "densityTarget", "densityTolerance" }. Problems are reported with their JSON path.
std::optional<TextureStandard> readTextureStandards(const Json& value, detail::Issues& issues) {
    const std::string path = "/textureStandards";
    if (!detail::requireObject(value, path, issues)) {
        return std::nullopt;
    }
    detail::checkKeys(value, path, {"classes", "defaultClass", "prohibitedSize", "densityTarget", "densityTolerance"},
                      issues);
    TextureStandard s;
    bool ok = true;
    detail::readUint(value, "prohibitedSize", path, s.prohibitedSize, issues);
    if (s.prohibitedSize == 0 || s.prohibitedSize > kTextureSizeProhibited) {
        issues.add(detail::child(path, "prohibitedSize"),
                   "prohibitedSize " + std::to_string(s.prohibitedSize) + " must be in 1..256");
        ok = false;
    }
    const auto classes = value.find("classes");
    if (classes == value.end()) {
        issues.add(detail::child(path, "classes"), "required field is missing");
        ok = false;
    } else if (!classes->is_object() || classes->empty()) {
        issues.add(detail::child(path, "classes"), "expected a non-empty object of texture class -> allowed sizes");
        ok = false;
    } else {
        for (const auto& [name, sizes] : classes->items()) {
            const std::string at = detail::child(detail::child(path, "classes"), name);
            if (!sizes.is_array() || sizes.empty()) {
                issues.add(at, "expected a non-empty array of square texture sizes");
                ok = false;
                continue;
            }
            TextureClassRule rule{name, {}};
            for (std::size_t i = 0; i < sizes.size(); ++i) {
                const Json& v = sizes[i];
                const std::string sizePath = detail::child(at, i);
                if (!v.is_number_unsigned() || v.get<uint64_t>() == 0 ||
                    v.get<uint64_t>() > std::numeric_limits<uint32_t>::max()) {
                    issues.add(sizePath, "expected a positive integer size, got " + detail::typeName(v) + " " + v.dump());
                    ok = false;
                    continue;
                }
                const auto size = static_cast<uint32_t>(v.get<uint64_t>());
                if (!std::has_single_bit(size)) {
                    issues.add(sizePath, "size " + std::to_string(size) + " is not a power of two");
                    ok = false;
                } else if (size >= s.prohibitedSize) {
                    issues.add(sizePath, "size " + std::to_string(size) + " is at or above the prohibited size " +
                                             std::to_string(s.prohibitedSize));
                    ok = false;
                } else if (!rule.sizes.empty() && size <= rule.sizes.back()) {
                    issues.add(sizePath, "sizes must be ascending and unique");
                    ok = false;
                }
                rule.sizes.push_back(size);
            }
            s.classes.push_back(std::move(rule));
        }
    }
    if (detail::readString(value, "defaultClass", path, s.defaultClass, issues, true)) {
        if (ok && !s.find(s.defaultClass)) {
            issues.add(detail::child(path, "defaultClass"), "unknown texture class '" + s.defaultClass + "'");
            ok = false;
        }
    } else {
        ok = false;
    }
    // Engine JSON numbers are float (JsonRead.h); setTextureStandards rounds to float too, so
    // parse(serialize()) == registry holds.
    const auto readDouble = [&](const char* key, double& out) {
        const auto it = value.find(key);
        if (it == value.end()) return true;
        if (!it->is_number() || !std::isfinite(it->get<double>())) {
            issues.add(detail::child(path, key), "expected a finite number, got " + detail::typeName(*it));
            return false;
        }
        out = it->get<double>();
        return true;
    };
    ok = readDouble("densityTarget", s.densityTarget) && ok;
    if (!(s.densityTarget > 0.0)) {
        issues.add(detail::child(path, "densityTarget"), "expected a positive number of texels per metre");
        ok = false;
    }
    ok = readDouble("densityTolerance", s.densityTolerance) && ok;
    if (!ok) {
        return std::nullopt;
    }
    std::ranges::sort(s.classes, {}, &TextureClassRule::name); // deterministic, as setTextureStandards
    try {
        s.validate();
    } catch (const std::exception& e) {
        issues.add(path, e.what());
        return std::nullopt;
    }
    return s;
}

Json textureStandardsJson(const TextureStandard& s) {
    Json j = Json::object();
    Json classes = Json::object();
    for (const TextureClassRule& c : s.classes) {
        classes[c.name] = c.sizes;
    }
    j["classes"] = std::move(classes);
    j["defaultClass"] = s.defaultClass;
    j["prohibitedSize"] = s.prohibitedSize;
    j["densityTarget"] = s.densityTarget;
    j["densityTolerance"] = s.densityTolerance;
    return j;
}

Json paletteSourceJson(const PaletteSource& s) {
    return s.swatch.empty() ? Json(s.colors) : Json(s.swatch);
}

} // namespace

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
    detail::checkKeys(root, "", {"schema", "version", "palette", "defaults", "budgets", "textureStandards", "assets"}, issues);
    if (const auto palette = root.find("palette"); palette != root.end()) {
        PaletteConfig config;
        bool ok = true;
        if (palette->is_object()) {
            detail::checkKeys(*palette, "/palette", {"colors", "subPalettes"}, issues);
            if (const auto colors = palette->find("colors"); colors != palette->end()) {
                ok = readPaletteSource(*colors, "/palette/colors", config.master, issues);
            } else {
                issues.add("/palette/colors", "required field is missing");
                ok = false;
            }
            if (const auto subs = palette->find("subPalettes"); subs != palette->end()) {
                if (!subs->is_object()) {
                    issues.add("/palette/subPalettes", "expected an object of named sub-palettes");
                } else {
                    for (const auto& [name, value] : subs->items()) {
                        PaletteSource sub;
                        if (readPaletteSource(value, detail::child("/palette/subPalettes", name), sub, issues)) {
                            config.subPalettes[name] = std::move(sub);
                        }
                    }
                }
            }
        } else {
            ok = readPaletteSource(*palette, "/palette", config.master, issues);
        }
        if (ok) {
            registry.m_palette = std::move(config);
        }
    }
    if (const auto budgets = root.find("budgets"); budgets != root.end()) {
        if (!budgets->is_object()) {
            issues.add("/budgets", "expected an object of asset class -> maximum triangles");
        } else {
            for (const auto& [name, value] : budgets->items()) {
                if (!value.is_number_unsigned() || value.get<uint64_t>() == 0 ||
                    value.get<uint64_t>() > std::numeric_limits<uint32_t>::max()) {
                    issues.add(detail::child("/budgets", name), "expected a positive triangle count, got " +
                                                                    detail::typeName(value) + " " + value.dump());
                    continue;
                }
                registry.m_budgets[name] = static_cast<uint32_t>(value.get<uint64_t>());
            }
        }
    }
    // Before defaults and entries: they validate "textureClass" against it.
    bool standardsInvalid = false;
    if (const auto standards = root.find("textureStandards"); standards != root.end()) {
        registry.m_textureStandards = readTextureStandards(*standards, issues);
        standardsInvalid = !registry.m_textureStandards;
    }
    // A "textureClass" problem; none while the declared standard itself is invalid (reported once).
    const auto textureClassIssue = [&](const std::string& textureClass) -> std::string {
        return standardsInvalid ? std::string() : textureClassProblem(textureClass, registry.m_textureStandards);
    };
    if (const auto defaults = root.find("defaults"); defaults != root.end()) {
        if (!defaults->is_object()) {
            issues.add("/defaults", "expected an object of per-type import defaults (texture, model)");
        } else {
            for (const auto& [name, value] : defaults->items()) {
                const std::string path = detail::child("/defaults", name);
                const std::optional<AssetType> type = parseAssetType(name);
                if (!type || (*type != AssetType::Texture && *type != AssetType::Model)) {
                    issues.add(path, "defaults apply to texture and model only");
                    continue;
                }
                if (!detail::requireObject(value, path, issues)) {
                    continue;
                }
                AssetDefaults d;
                if (*type == AssetType::Texture) {
                    detail::checkKeys(value, path, {"usage", "mips", "textureClass"}, issues);
                    std::string usage;
                    if (detail::readString(value, "usage", path, usage, issues)) {
                        d.usage = parseUsage(usage);
                        if (!d.usage) {
                            issues.add(detail::child(path, "usage"),
                                       "unknown usage '" + usage + "' (expected color, data or normal)");
                        }
                    }
                    bool mips = true;
                    if (detail::readBool(value, "mips", path, mips, issues)) d.mips = mips;
                } else {
                    detail::checkKeys(value, path, {"sampling", "compile", "class", "textureClass"}, issues);
                    std::string sampling;
                    if (detail::readString(value, "sampling", path, sampling, issues)) {
                        d.sampling = parseSampling(sampling);
                        if (!d.sampling) {
                            issues.add(detail::child(path, "sampling"),
                                       "unknown sampling '" + sampling + "' (expected file, pixelArt, nearest or linear)");
                        }
                    }
                    bool compile = false;
                    if (detail::readBool(value, "compile", path, compile, issues)) d.compile = compile;
                    std::string assetClass;
                    if (detail::readString(value, "class", path, assetClass, issues)) {
                        if (const std::string problem = classProblem(assetClass, registry.m_budgets); !problem.empty()) {
                            issues.add(detail::child(path, "class"), problem);
                        }
                        d.assetClass = assetClass;
                    }
                }
                std::string textureClass;
                if (detail::readString(value, "textureClass", path, textureClass, issues)) {
                    if (const std::string problem = textureClassIssue(textureClass); !problem.empty()) {
                        issues.add(detail::child(path, "textureClass"), problem);
                    }
                    d.textureClass = textureClass;
                }
                registry.m_defaults[*type] = std::move(d);
            }
        }
    }
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
                           "unknown asset type '" + type + "' (expected texture, model, material, scene, prefab or blocks)");
            }
        }
        if (e.type == AssetType::Texture) {
            detail::checkKeys(a, path,
                              {"id", "type", "source", "usage", "mips", "textureClass", "hash", "dependencies", "outputs",
                               "palette"},
                              issues);
        } else if (e.type == AssetType::Model) {
            detail::checkKeys(
                a, path,
                {"id", "type", "source", "sampling", "compile", "class", "textureClass", "hash", "dependencies",
                 "outputs", "palette"},
                issues);
            if (detail::readString(a, "class", path, e.assetClass, issues)) {
                if (const std::string problem = classProblem(e.assetClass, registry.m_budgets); !problem.empty()) {
                    issues.add(detail::child(path, "class"), problem);
                }
            }
        } else if (e.type == AssetType::Material || e.type == AssetType::Blocks) {
            detail::checkKeys(a, path,
                              {"id", "type", "source", "textureClass", "hash", "dependencies", "outputs", "palette"},
                              issues);
        } else {
            detail::checkKeys(a, path, {"id", "type", "source", "hash", "dependencies", "outputs", "palette"}, issues);
        }
        if (detail::readString(a, "textureClass", path, e.textureClass, issues)) {
            if (const std::string problem = textureClassIssue(e.textureClass); !problem.empty()) {
                issues.add(detail::child(path, "textureClass"), problem);
            }
        }
        if (detail::readString(a, "palette", path, e.palette, issues)) {
            if (!registry.m_palette) {
                issues.add(detail::child(path, "palette"),
                           "sub-palette '" + e.palette + "' but the registry has no \"palette\"");
            } else if (!registry.m_palette->subPalettes.contains(e.palette)) {
                issues.add(detail::child(path, "palette"), "unknown sub-palette '" + e.palette + "'");
            }
        }
        if (detail::readString(a, "source", path, e.source, issues, true) && escapesDirectory(e.source)) {
            issues.add(detail::child(path, "source"),
                       "source '" + e.source + "' must be a relative path inside the registry directory");
        }
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
    if (m_palette) {
        Json palette = Json::object();
        palette["colors"] = paletteSourceJson(m_palette->master);
        if (!m_palette->subPalettes.empty()) {
            Json subs = Json::object();
            for (const auto& [name, sub] : m_palette->subPalettes) {
                subs[name] = paletteSourceJson(sub);
            }
            palette["subPalettes"] = std::move(subs);
        }
        root["palette"] = std::move(palette);
    }
    if (!m_defaults.empty()) {
        Json defaults = Json::object();
        for (const auto& [type, d] : m_defaults) {
            Json t = Json::object();
            if (d.usage) t["usage"] = toString(*d.usage);
            if (d.mips) t["mips"] = *d.mips;
            if (d.sampling) t["sampling"] = samplingName(*d.sampling);
            if (d.compile) t["compile"] = *d.compile;
            if (d.assetClass) t["class"] = *d.assetClass;
            if (d.textureClass) t["textureClass"] = *d.textureClass;
            defaults[toString(type)] = std::move(t);
        }
        root["defaults"] = std::move(defaults);
    }
    if (!m_budgets.empty()) {
        root["budgets"] = m_budgets;
    }
    if (m_textureStandards) {
        root["textureStandards"] = textureStandardsJson(*m_textureStandards);
    }
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
            if (!e.assetClass.empty()) {
                a["class"] = e.assetClass;
            }
        }
        if (!e.textureClass.empty()) {
            a["textureClass"] = e.textureClass;
        }
        if (!e.hash.empty()) {
            a["hash"] = e.hash;
        }
        a["dependencies"] = e.dependencies;
        if (!e.outputs.empty()) {
            a["outputs"] = e.outputs;
        }
        if (!e.palette.empty()) {
            a["palette"] = e.palette;
        }
        assets.push_back(std::move(a));
    }
    root["assets"] = std::move(assets);
    return root.dump(2) + "\n";
}

void AssetRegistry::setTextureStandards(std::optional<TextureStandard> standard) {
    if (standard) {
        standard->validate();
        std::ranges::sort(standard->classes, {}, &TextureClassRule::name);
        standard->engineRule = false;
        // Stored as float in assets.json (engine JSON numbers are float): round now so the
        // registry compares equal after save + load.
        standard->densityTarget = static_cast<float>(standard->densityTarget);
        standard->densityTolerance = static_cast<float>(standard->densityTolerance);
    }
    m_textureStandards = std::move(standard);
}

void AssetRegistry::save(const std::filesystem::path& file) const {
    // Write a temporary file and rename it over the registry: a failed write (disk full,
    // interrupted tool) never leaves a truncated assets.json behind.
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << serialize();
        out.close();
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            throw Error("cannot write asset registry '" + file.generic_string() + "'");
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        throw Error("cannot write asset registry '" + file.generic_string() + "': " + ec.message());
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
        const auto hex = [](char c) -> int {
            return c >= '0' && c <= '9' ? c - '0'
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                          : -1;
        };
        // Exactly two hex digits; anything else stays literal (like cgltf's URI decoding).
        if (uri[i] == '%' && i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0) {
            out += static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
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

std::vector<std::filesystem::path> dependenciesOf(AssetType type, const std::filesystem::path& source);

// External buffers/images of a .gltf/.glb file.
void addGltfUris(const std::filesystem::path& model, std::vector<std::filesystem::path>& deps) {
    const Json root = gltfJson(model);
    for (const char* key : {"buffers", "images"}) {
        const auto list = root.find(key);
        if (list == root.end() || !list->is_array()) {
            continue;
        }
        for (const Json& item : *list) {
            const auto uri = item.is_object() ? item.find("uri") : item.end();
            if (uri != item.end() && uri->is_string() && !uri->get<std::string>().starts_with("data:")) {
                deps.push_back(model.parent_path() / percentDecode(uri->get<std::string>()));
            }
        }
    }
}

std::vector<std::filesystem::path> dependenciesOf(AssetType type, const std::filesystem::path& source) {
    std::vector<std::filesystem::path> deps;
    const std::filesystem::path dir = source.parent_path();
    switch (type) {
    case AssetType::Texture: break;
    case AssetType::Model: addGltfUris(source, deps); break;
    case AssetType::Material: addMaterialTextures(loadMaterialFile(source), dir, deps); break;
    case AssetType::Blocks:
        for (const std::filesystem::path& t : blockFileTextures(loadBlockFile(source), dir)) deps.push_back(t);
        break;
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
        // Model files referenced by path (registry ids have their own entries and hashes).
        for (const SceneEntity& e : doc.entities) {
            if (e.modelRenderer && isModelFileReference(e.modelRenderer->model)) {
                const std::filesystem::path model = dir / e.modelRenderer->model;
                deps.push_back(model);
                if (std::filesystem::exists(model)) {
                    addGltfUris(model, deps);
                }
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

std::vector<std::filesystem::path> assetDependencies(AssetType type, const std::filesystem::path& source) {
    return dependenciesOf(type, normal(source));
}

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
    if (source.empty() || escapesDirectory(source)) {
        throw Error("cannot import '" + file.generic_string() + "': outside the registry directory '" +
             normal(registry.directory()).generic_string() + "'");
    }
    const std::optional<AssetType> type = options.type ? options.type : detectAssetType(abs);
    if (!type) {
        throw Error("cannot import '" + file.generic_string() +
             "': unknown asset type (use --type texture|model|material|scene|prefab|blocks)");
    }
    if ((options.usage || options.mips) && *type != AssetType::Texture) {
        throw Error("--usage/--no-mips apply to textures only");
    }
    if ((options.sampling || options.compile || options.assetClass) && *type != AssetType::Model) {
        throw Error("--sampling/--compile/--class apply to models only");
    }
    if (options.textureClass && *type != AssetType::Texture && *type != AssetType::Model &&
        *type != AssetType::Material && *type != AssetType::Blocks) {
        throw Error("--texture-class applies to textures, models, materials and blocks only");
    }

    // An existing entry for the same source keeps its id and settings.
    AssetEntry entry;
    bool fresh = true; // new entry: starts from the registry defaults
    const auto same = std::ranges::find(registry.entries(), source, &AssetEntry::source);
    if (!options.id.empty()) {
        if (same != registry.entries().end() && same->id != options.id) {
            throw Error("cannot import '" + source + "' as '" + options.id + "': already imported as '" + same->id +
                        "'");
        }
        if (const AssetEntry* e = registry.find(options.id)) {
            // Settings carry over only for the same file and type (e.g. re-import with --id).
            if (e->source == source && e->type == (options.type ? *options.type : e->type)) {
                entry = *e;
                fresh = false;
            }
        }
        entry.id = options.id;
    } else if (same != registry.entries().end()) {
        entry = *same;
        fresh = false;
    } else {
        entry.id = defaultAssetId(abs);
        if (const AssetEntry* e = registry.find(entry.id)) {
            throw Error("asset id '" + entry.id + "' is already used by '" + e->source + "' (pass --id)");
        }
    }
    entry.type = *type;
    entry.source = source;
    if (fresh) {
        if (const auto d = registry.defaults().find(*type); d != registry.defaults().end()) {
            const AssetDefaults& defaults = d->second;
            if (*type == AssetType::Texture) {
                if (defaults.usage) entry.usage = *defaults.usage;
                if (defaults.mips) entry.mips = *defaults.mips;
            } else if (*type == AssetType::Model) {
                if (defaults.sampling) entry.sampling = *defaults.sampling;
                if (defaults.compile) entry.compile = *defaults.compile;
                if (defaults.assetClass) entry.assetClass = *defaults.assetClass;
            }
            if (defaults.textureClass) entry.textureClass = *defaults.textureClass;
        }
    }
    if (options.assetClass) entry.assetClass = *options.assetClass;
    if (!entry.assetClass.empty()) {
        if (const std::string problem = classProblem(entry.assetClass, registry.budgets()); !problem.empty()) {
            throw Error("cannot import '" + source + "': " + problem);
        }
    }
    if (options.textureClass) entry.textureClass = *options.textureClass;
    if (!entry.textureClass.empty()) {
        if (const std::string problem = textureClassProblem(entry.textureClass, registry.textureStandards());
            !problem.empty()) {
            throw Error("cannot import '" + source + "': " + problem);
        }
    }
    if (options.usage) entry.usage = *options.usage;
    if (options.mips) entry.mips = *options.mips;
    if (options.sampling) entry.sampling = *options.sampling;
    if (options.compile) entry.compile = *options.compile;
    if (options.palette) {
        if (!options.palette->empty() &&
            (!registry.palette() || !registry.palette()->subPalettes.contains(*options.palette))) {
            throw Error("unknown sub-palette '" + *options.palette + "' (define it in the registry's \"palette\")");
        }
        entry.palette = *options.palette;
    }

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

std::vector<std::filesystem::path> findAssetFiles(const AssetRegistry& registry,
                                                 const std::filesystem::path& directory) {
    const std::filesystem::path root = normal(directory);
    if (!std::filesystem::is_directory(root)) {
        throw Error("cannot import '" + directory.generic_string() + "': not a directory");
    }
    std::vector<std::filesystem::path> found;
    for (auto it = std::filesystem::recursive_directory_iterator(root); it != std::filesystem::recursive_directory_iterator();
         ++it) {
        const std::string name = it->path().filename().string();
        if (name.starts_with(".") || name.ends_with(".zodar")) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && detectAssetType(it->path())) {
            found.push_back(normal(it->path()));
        }
    }
    std::set<std::filesystem::path> skip;
    if (const auto& palette = registry.palette()) {
        std::vector<const PaletteSource*> sources{&palette->master};
        for (const auto& [subName, sub] : palette->subPalettes) sources.push_back(&sub);
        for (const PaletteSource* p : sources) {
            if (!p->swatch.empty()) skip.insert(normal(registry.directory() / p->swatch));
        }
    }
    for (const auto& file : found) {
        for (const auto& dep : assetDependencies(*detectAssetType(file), file)) {
            skip.insert(normal(dep));
        }
    }
    std::erase_if(found, [&](const std::filesystem::path& f) { return skip.contains(f); });
    std::ranges::sort(found, {}, [](const std::filesystem::path& p) { return p.generic_string(); });
    return found;
}

namespace {

// Size check against the registry's texture standard and the entry's texture class: a
// Violation is a warning (fails --strict), the prohibited size an error.
struct SizeCheck {
    const AssetCheckOptions& options;
    TextureStandard standard;
    std::string textureClass; // empty: the standard's default class
};

void addTexture(AssetCheckResult& r, const SizeCheck& check, std::string name, uint32_t w, uint32_t h) {
    r.textures.push_back({name, w, h, classifyTextureSize(w, h)});
    if (check.options.checkTextureSizes) {
        if (auto issue = checkTextureSize(check.standard, name, w, h, check.textureClass)) {
            (issue->verdict == TextureSizeVerdict::Prohibited ? r.errors : r.warnings).push_back(std::move(issue->message));
        }
    }
}

// Counts level-0 texels of an RGBA8 colour texture against `palette` (transparent skipped).
void countPalette(const ImageData& image, const Palette& palette, AssetTextureInfo& info, PaletteDeviation& dev) {
    if (image.format != PixelFormat::Rgba8 || image.levels.empty()) {
        return;
    }
    info.paletteChecked = true;
    const std::vector<uint8_t>& px = image.levels[0];
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        if (px[i + 3] == 0) {
            continue;
        }
        ++info.paletteTexels;
        const uint32_t rgb = uint32_t(px[i]) << 16 | uint32_t(px[i + 1]) << 8 | px[i + 2];
        if (!palette.contains(rgb)) {
            ++info.offPaletteTexels;
            if (dev.examples.size() < 4 && std::ranges::find(dev.examples, rgb) == dev.examples.end()) {
                dev.examples.push_back(rgb);
            }
        }
    }
    dev.texels += info.paletteTexels;
    dev.offPalette += info.offPaletteTexels;
}

// Active palette of a check: nullptr when none applies.
struct PaletteCheck {
    std::optional<Palette> palette;
    PaletteDeviation deviation;
    bool used = false;
};

// `palette` is null for non-colour textures (normal, data maps) or without a palette.
void checkImageFile(AssetCheckResult& r, const SizeCheck& options, const std::filesystem::path& file,
                    const std::filesystem::path& registryDir, PaletteCheck* palette = nullptr) {
    if (!std::filesystem::exists(file)) {
        return; // reported as a missing dependency
    }
    const ImageData image = loadImage(file, ColorSpace::Srgb);
    addTexture(r, options, relativePath(file, registryDir), image.width, image.height);
    if (palette && palette->palette) {
        countPalette(image, *palette->palette, r.textures.back(), palette->deviation);
        palette->used = true;
    }
}

// Base colour and emissive textures (colour data) of a material.
std::set<std::filesystem::path> colourTextures(const MaterialData& m, const std::filesystem::path& directory) {
    std::set<std::filesystem::path> out;
    for (const MaterialTextureData* t : {&m.baseColorTexture, &m.emissiveTexture}) {
        if (!t->empty()) {
            const std::filesystem::path p(t->path);
            out.insert(normal(p.is_absolute() ? p : directory / p));
        }
    }
    return out;
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
            // Malformed files (index values beyond the vertex count) are skipped, not read.
            const uint64_t end = uint64_t(sub.firstIndex) + sub.indexCount;
            if (end > mesh.indices.size() ||
                std::any_of(mesh.indices.begin() + sub.firstIndex, mesh.indices.begin() + static_cast<std::ptrdiff_t>(end),
                            [&](uint32_t index) { return index >= mesh.vertices.size(); })) {
                continue;
            }
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
    // Palette: --palette / options override the master palette; entries may add a sub-palette.
    PaletteCheck palette;
    try {
        const std::optional<PaletteConfig>& config = registry.palette();
        const PaletteSource* master = options.palette ? &*options.palette : config ? &config->master : nullptr;
        if (!entry.palette.empty() && !(config && config->subPalettes.contains(entry.palette))) {
            throw Error("unknown sub-palette '" + entry.palette + "'");
        }
        if (master && !master->empty()) {
            Palette p = loadPalette(*master, dir, "master");
            if (!entry.palette.empty()) {
                const Palette sub = loadPalette(config->subPalettes.at(entry.palette), dir, entry.palette);
                p.colors.insert(p.colors.end(), sub.colors.begin(), sub.colors.end());
                std::ranges::sort(p.colors);
                p.colors.erase(std::unique(p.colors.begin(), p.colors.end()), p.colors.end());
                p.name = "master+" + entry.palette;
            }
            palette.deviation.palette = p.name;
            palette.palette = std::move(p);
        }
    } catch (const std::exception& e) {
        r.errors.push_back(std::string("palette: ") + e.what());
    }
    r.textureClass = entry.textureClass;
    const SizeCheck sizes{options, registry.textureStandard(), entry.textureClass};
    try {
        switch (entry.type) {
        case AssetType::Texture:
            checkImageFile(r, sizes, file, dir, entry.usage == TextureUsage::Color ? &palette : nullptr);
            break;
        case AssetType::Model: {
            GltfLoadOptions load;
            load.useCompiledTextures = false;
            load.generateMips = false;
            const ModelData data = loadGltf(file, load);
            for (const std::string& w : data.warnings) {
                r.warnings.push_back("glTF: " + w);
            }
            r.notes = data.notes;
            r.sourceTool = toString(data.sourceTool);
            r.meshes = static_cast<uint32_t>(data.meshes.size());
            r.materials = static_cast<uint32_t>(data.materials.size());
            for (const MeshData& m : data.meshes) {
                r.vertices += static_cast<uint32_t>(m.vertices.size());
                r.triangles += static_cast<uint32_t>(m.indices.size() / 3);
            }
            std::set<int> colourImages; // base colour + emissive images
            for (const ModelMaterial& m : data.materials) {
                colourImages.insert(m.baseColor.image);
                colourImages.insert(m.emissiveTexture.image);
            }
            for (size_t i = 0; i < data.images.size(); ++i) {
                const ImageData& image = data.images[i];
                addTexture(r, sizes,
                           entry.source + "/" + (image.name.empty() ? "image" + std::to_string(i) : image.name),
                           image.width, image.height);
                if (palette.palette && colourImages.contains(static_cast<int>(i))) {
                    countPalette(image, *palette.palette, r.textures.back(), palette.deviation);
                    palette.used = true;
                }
            }
            if (!data.bounds.isEmpty()) {
                r.size = data.bounds.extent();
                const double extent = std::max({r.size.x, r.size.y, r.size.z});
                if ((options.minModelSize > 0.0 && extent < options.minModelSize) ||
                    (options.maxModelSize > 0.0 && extent > options.maxModelSize)) {
                    r.warnings.push_back(formatText("bounds: largest extent %.3f m is outside %.4g..%.4g m; check the "
                                                    "export scale (Blockbench: 16 units = 1 m; Blender: 1 unit = 1 m, "
                                                    "transforms applied)",
                                                    extent, options.minModelSize, options.maxModelSize));
                }
            }
            if (!entry.assetClass.empty()) {
                r.assetClass = entry.assetClass;
                if (const auto b = registry.budgets().find(entry.assetClass); b != registry.budgets().end()) {
                    r.triangleBudget = b->second;
                    if (r.triangles > b->second) {
                        r.warnings.push_back(formatText("triangles: %u triangles exceed the '%s' budget of %u",
                                                        r.triangles, entry.assetClass.c_str(), b->second));
                    }
                } else {
                    r.warnings.push_back("triangles: " + classProblem(entry.assetClass, registry.budgets()));
                }
            }
            r.texelDensity = texelDensity(data);
            const double target = options.texelDensityTarget.value_or(sizes.standard.densityTarget);
            const double tol = options.texelDensityTolerance.value_or(sizes.standard.densityTolerance);
            if (r.texelDensity > 0.0 && tol > 0.0 &&
                (r.texelDensity < target / tol || r.texelDensity > target * tol)) {
                r.warnings.push_back(formatText("texel density %.1f texels/m outside %.0f..%.0f (target %.0f)",
                                                r.texelDensity, target / tol, target * tol, target));
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
            const std::set<std::filesystem::path> colour = colourTextures(m, file.parent_path());
            std::set<std::filesystem::path> seen;
            for (const auto& t : textures) {
                if (seen.insert(normal(t)).second) {
                    checkImageFile(r, sizes, normal(t), dir, colour.contains(normal(t)) ? &palette : nullptr);
                }
            }
            break;
        }
        case AssetType::Blocks: {
            const std::shared_ptr<const voxel::ShapeLibrary> shapes =
                options.blockShapes ? options.blockShapes
                                    : std::make_shared<const voxel::ShapeLibrary>(voxel::ShapeLibrary::builtins());
            const BlockFileData data = loadBlockFile(file, shapes.get());
            const BlockCatalogue c = buildBlockCatalogue(data, file.parent_path(), shapes, entry.source);
            r.materials = static_cast<uint32_t>(data.materials.size());
            r.blocks = static_cast<uint32_t>(c.palette.blocks.size() - 1);
            r.layers = static_cast<uint32_t>(c.layers.size());
            // Every texture once: size per texture class, palette (colour data).
            std::map<std::filesystem::path, glm::uvec2> dims;
            bool complete = true;
            for (const std::filesystem::path& layer : c.layers) {
                const std::filesystem::path t = normal(layer);
                if (dims.contains(t)) continue;
                if (!std::filesystem::exists(t)) {
                    complete = false; // reported as a missing dependency
                    continue;
                }
                // One unreadable texture must not hide the problems of the others.
                try {
                    checkImageFile(r, sizes, t, dir, &palette);
                } catch (const std::exception& e) {
                    r.errors.push_back("texture " + relativePath(t, dir) + ": " + e.what());
                    complete = false;
                    continue;
                }
                dims[t] = {r.textures.back().width, r.textures.back().height};
            }
            for (const DataIssue& i : checkBlockLayerCount(c, options.maxTextureArrayLayers, entry.source)) {
                r.errors.push_back("texture array: " + i.toString());
            }
            if (complete && !c.layers.empty()) {
                // One texture array: square layers of one size (errors: the array cannot be built).
                std::vector<glm::uvec2> layerSizes;
                for (const std::filesystem::path& layer : c.layers) layerSizes.push_back(dims.at(normal(layer)));
                const std::vector<DataIssue> sizeIssues = checkBlockLayerSizes(c, layerSizes, entry.source);
                for (const DataIssue& i : sizeIssues) r.errors.push_back("texture array: " + i.toString());
                // Texel density: layer size x tiles per metre.
                if (sizeIssues.empty()) {
                    r.texelDensity = static_cast<double>(layerSizes[0].x) * static_cast<double>(c.uvPerMetre);
                    const double target = options.texelDensityTarget.value_or(sizes.standard.densityTarget);
                    const double tol = options.texelDensityTolerance.value_or(sizes.standard.densityTolerance);
                    if (tol > 0.0 && (r.texelDensity < target / tol || r.texelDensity > target * tol)) {
                        r.warnings.push_back(formatText("texel density %.1f texels/m outside %.0f..%.0f (target %.0f)",
                                                        r.texelDensity, target / tol, target * tol, target));
                    }
                }
            }
            break;
        }
        case AssetType::Scene:
        case AssetType::Prefab: {
            const SceneDocument doc = loadSceneFile(file);
            r.materials = static_cast<uint32_t>(doc.materials.size());
            std::set<std::string> models;
            for (const SceneEntity& e : doc.entities) {
                if (!e.modelRenderer || isModelFileReference(e.modelRenderer->model) ||
                    !models.insert(e.modelRenderer->model).second) {
                    continue;
                }
                const AssetEntry* m = registry.find(e.modelRenderer->model);
                if (!m) {
                    r.errors.push_back("modelRenderer: unknown asset id '" + e.modelRenderer->model + "'");
                } else if (m->type != AssetType::Model) {
                    r.errors.push_back("modelRenderer: asset '" + m->id + "' is a " + toString(m->type) +
                                       ", not a model");
                }
            }
            // A texture used as colour by any material is palette-checked (once), even if another
            // material uses it as a normal/data map first.
            std::vector<std::filesystem::path> textures;
            std::set<std::filesystem::path> colour;
            for (const SceneMaterial& m : doc.materials) {
                const std::filesystem::path base = !m.source.empty() ? (file.parent_path() / m.source).parent_path()
                                                                     : sceneMaterialDirectory(m, file.parent_path());
                addMaterialTextures(m.data, base, textures);
                colour.merge(colourTextures(m.data, base));
            }
            std::set<std::filesystem::path> seen;
            for (const auto& t : textures) {
                if (seen.insert(normal(t)).second) {
                    checkImageFile(r, sizes, normal(t), dir, colour.contains(normal(t)) ? &palette : nullptr);
                }
            }
            break;
        }
        }
    } catch (const std::exception& e) {
        r.errors.push_back(e.what());
    }
    if (palette.used) {
        const PaletteDeviation& d = palette.deviation;
        if (d.offPalette > 0) {
            std::string message = formatText("palette: %llu of %llu texels (%.2f%%) not in palette '",
                                             static_cast<unsigned long long>(d.offPalette),
                                             static_cast<unsigned long long>(d.texels), d.percent()) +
                                  d.palette + "' (e.g. ";
            for (size_t i = 0; i < d.examples.size(); ++i) {
                message += (i ? ", " : "") + hexColor(d.examples[i]);
            }
            r.warnings.push_back(message + ")");
        }
        r.paletteDeviation = d;
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
            Json tj = Json{{"name", t.name}, {"width", t.width}, {"height", t.height},
                           {"sizeClass", toString(t.sizeClass)}};
            if (t.paletteChecked) {
                tj["paletteTexels"] = t.paletteTexels;
                tj["offPaletteTexels"] = t.offPaletteTexels;
            }
            textures.push_back(std::move(tj));
        }
        a["textures"] = std::move(textures);
        a["meshes"] = r.meshes;
        a["vertices"] = r.vertices;
        a["triangles"] = r.triangles;
        a["materials"] = r.materials;
        a["texelDensity"] = std::round(r.texelDensity * 100.0) / 100.0;
        if (!r.textureClass.empty()) {
            a["textureClass"] = r.textureClass;
        }
        if (r.type == AssetType::Blocks) {
            a["blocks"] = r.blocks;
            a["layers"] = r.layers;
        }
        if (r.type == AssetType::Model && !r.sourceTool.empty()) {
            a["sourceTool"] = r.sourceTool;
            a["notes"] = r.notes;
            a["size"] = Json::array({std::round(r.size.x * 1000.0) / 1000.0, std::round(r.size.y * 1000.0) / 1000.0,
                                     std::round(r.size.z * 1000.0) / 1000.0});
            if (!r.assetClass.empty()) {
                a["class"] = r.assetClass;
                a["triangleBudget"] = r.triangleBudget;
            }
        }
        if (r.paletteDeviation) {
            const PaletteDeviation& d = *r.paletteDeviation;
            Json examples = Json::array();
            for (const uint32_t c : d.examples) {
                examples.push_back(hexColor(c));
            }
            a["paletteDeviation"] = Json{{"palette", d.palette},
                                         {"texels", d.texels},
                                         {"offPalette", d.offPalette},
                                         {"percent", std::round(d.percent() * 100.0) / 100.0},
                                         {"examples", std::move(examples)}};
        }
        assets.push_back(std::move(a));
    }
    root["summary"] = Json{{"assets", results.size()}, {"ok", ok}, {"warnings", warnings}, {"errors", errors}};
    root["assets"] = std::move(assets);
    return root.dump(2) + "\n";
}

} // namespace zodar
