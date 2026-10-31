#pragma once

#include "AssetManager/AssetManager.h"
#include "Assets/TextureCache.h"
#include "Assets/TextureStandards.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Asset registry (Phase C3): `assets.json` next to a game's assets lists every asset with a
// stable id, its source file, import settings, the files it depends on, a content hash and
// compiled outputs. CPU only (no device needed): used by the tool
// ZodarAssetTool (`zodar.py import` / `zodar.py assets list|check`) and by games to load assets by id.
// Schema and workflow: docs/guide/ASSETS.md.
//
//   AssetRegistry registry = AssetRegistry::load("assets/assets.json");
//   const AssetEntry& crate = registry.at("crate");
//   ModelHandle h = assets.loadModelAsync(registry.sourcePath(crate), modelLoadSettings(crate));
namespace zodar {

enum class AssetType : uint8_t { Texture, Model, Material, Scene, Prefab };
const char* toString(AssetType type);
std::optional<AssetType> parseAssetType(std::string_view name);
// From the file name: .png/.jpg/.jpeg/.tga/.bmp texture, .gltf/.glb model, *.material.json,
// *.scene.json, *.prefab.json. nullopt for anything else.
std::optional<AssetType> detectAssetType(const std::filesystem::path& file);

// Model sampler settings stored in the registry.
enum class ModelSampling : uint8_t { File, PixelArt, Nearest, Linear };

struct AssetEntry {
    std::string id;     // unique, stable; default: file name without asset extensions
    AssetType type = AssetType::Texture;
    std::string source; // relative to the registry directory, '/' separators
    // Import settings (only those of the entry's type are written).
    TextureUsage usage = TextureUsage::Color;    // texture
    bool mips = true;                            // texture
    ModelSampling sampling = ModelSampling::File; // model: file samplers or an override
    bool compile = false;                        // model: compiled KTX2 textures (ZodarAssetTool --compile)
    // Recorded at import: "fnv1a64:<hex>" over the source and every dependency (path + bytes).
    std::string hash;
    std::vector<std::string> dependencies; // relative to the registry directory, sorted
    std::vector<std::string> outputs;      // compiled files, relative to the registry directory

    bool operator==(const AssetEntry&) const = default;
};

class AssetRegistry {
public:
    static constexpr const char* kFileName = "assets.json";
    static constexpr const char* kSchema = "zodar.asset_registry";
    static constexpr int kVersion = 1;

    AssetRegistry() = default;
    // Empty registry whose relative paths resolve against `directory`.
    explicit AssetRegistry(std::filesystem::path directory) : m_directory(std::move(directory)) {}

    // Throws DataError listing every problem (syntax line:column, schema JSON path).
    [[nodiscard]] static AssetRegistry load(const std::filesystem::path& file);
    [[nodiscard]] static AssetRegistry parse(std::string_view json, const std::filesystem::path& file);
    // Deterministic, pretty-printed (entries sorted by id); parse(serialize()) == *this.
    [[nodiscard]] std::string serialize() const;
    void save(const std::filesystem::path& file) const;

    const std::filesystem::path& directory() const { return m_directory; }
    const std::vector<AssetEntry>& entries() const { return m_entries; }
    const AssetEntry* find(std::string_view id) const;
    // Throws zodar::Error for an unknown id.
    const AssetEntry& at(std::string_view id) const;
    std::filesystem::path sourcePath(const AssetEntry& entry) const { return m_directory / entry.source; }
    // Inserts or replaces (same id); keeps entries sorted by id.
    void upsert(AssetEntry entry);
    bool remove(std::string_view id);

    bool operator==(const AssetRegistry& other) const { return m_entries == other.m_entries; }

private:
    std::filesystem::path m_directory;
    std::vector<AssetEntry> m_entries;
};

// Loader settings from an entry's import settings.
TextureLoadSettings textureLoadSettings(const AssetEntry& entry);
ModelLoadSettings modelLoadSettings(const AssetEntry& entry);

// Files an asset depends on (glTF external buffers/images, material textures, scene material
// files and their textures, prefabs' inline material textures) and the content hash.
// `missing` lists referenced files that do not exist (they are hashed as absent).
struct AssetFingerprint {
    std::string hash;
    std::vector<std::filesystem::path> dependencies; // absolute, sorted, existing or not
    std::vector<std::filesystem::path> missing;
};
// Throws zodar::Error / DataError if the source cannot be read or parsed.
[[nodiscard]] AssetFingerprint fingerprintAsset(AssetType type, const std::filesystem::path& source);

struct AssetImportOptions {
    std::string id;                    // empty: default id from the file name
    std::optional<AssetType> type;     // default: detectAssetType
    std::optional<TextureUsage> usage; // texture
    std::optional<bool> mips;          // texture
    std::optional<ModelSampling> sampling; // model
    std::optional<bool> compile;       // model (outputs are filled by the caller that compiles)
};
// Adds or updates the entry for `file` (settings of an existing entry are kept unless given):
// type, relative source, dependencies and hash. Throws zodar::Error if the file is missing,
// outside the registry directory, of unknown type, unreadable, or references missing files.
const AssetEntry& importAsset(AssetRegistry& registry, const std::filesystem::path& file,
                              const AssetImportOptions& options = {});
std::string defaultAssetId(const std::filesystem::path& file);

// --- Checks and report -------------------------------------------------------------------

struct AssetTextureInfo {
    std::string name; // file or "<model>/<image>"
    uint32_t width = 0;
    uint32_t height = 0;
    TextureSizeClass sizeClass = TextureSizeClass::NonStandard;
};

struct AssetCheckResult {
    std::string id;
    AssetType type = AssetType::Texture;
    std::string source;
    std::vector<std::string> errors;   // unusable: missing/invalid source or dependency
    std::vector<std::string> warnings; // stale hash, size standards, texel density, importer notes
    std::vector<AssetTextureInfo> textures;
    uint32_t meshes = 0, vertices = 0, triangles = 0, materials = 0;
    // Models: texels per metre of base-colour textures (sqrt(texel area / surface area) over
    // every textured triangle); 0 when the model has no textured triangles.
    double texelDensity = 0.0;

    const char* status() const { return !errors.empty() ? "error" : !warnings.empty() ? "warning" : "ok"; }
};

struct AssetCheckOptions {
    bool checkTextureSizes = true;    // TextureStandards warnings
    double texelDensityTarget = 32.0; // docs/ART_DIRECTION.md: 32 texels per metre (32×32 per 1 m face)
    double texelDensityTolerance = 2.0; // warn outside [target / tol, target * tol]; <= 0 disables
};

// Checks one entry / every entry (registry order). Never throws: problems become errors.
AssetCheckResult checkAsset(const AssetRegistry& registry, const AssetEntry& entry, const AssetCheckOptions& options = {});
std::vector<AssetCheckResult> checkRegistry(const AssetRegistry& registry, const AssetCheckOptions& options = {});

// JSON report, schema "zodar.asset_report" v1: { schema, version, registry, summary {assets,
// ok, warnings, errors}, assets: [{ id, type, source, status, errors, warnings, textures,
// meshes, vertices, triangles, materials, texelDensity }] }.
std::string assetReportJson(const AssetRegistry& registry, const std::vector<AssetCheckResult>& results);

} // namespace zodar
