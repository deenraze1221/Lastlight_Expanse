#pragma once

#include "AssetManager/AssetManager.h"
#include "Assets/TextureCache.h"
#include "Assets/TextureStandards.h"

#include <cstdint>
#include <filesystem>
#include <map>
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

// --- Palettes (Phase C7) -------------------------------------------------------------------
// A game's master palette (docs/ART_DIRECTION.md: "consistent palette") and optional named
// sub-palettes (per biome or faction). A sub-palette extends the master palette: an entry with
// "palette": "<sub>" may use the colours of both. Colours compare as RGB; alpha is ignored and
// fully transparent texels are skipped.

// A PNG swatch (every RGB colour of its non-transparent texels; any layout or size) or a list
// of "#rrggbb" colours (exactly one of the two).
struct PaletteSource {
    std::string swatch;              // PNG, relative to the registry directory (or absolute)
    std::vector<std::string> colors; // "#rrggbb", as written
    bool empty() const { return swatch.empty() && colors.empty(); }
    bool operator==(const PaletteSource&) const = default;
};

struct PaletteConfig {
    PaletteSource master;
    std::map<std::string, PaletteSource> subPalettes;
    bool operator==(const PaletteConfig&) const = default;
};

// Resolved colours (0xRRGGBB, sorted, unique).
struct Palette {
    std::string name; // "master" or "master+<sub>"
    std::vector<uint32_t> colors;
    bool contains(uint32_t rgb) const;
};

// "#rrggbb" (case-insensitive) -> 0xRRGGBB; nullopt if malformed.
std::optional<uint32_t> parseHexColor(std::string_view text);
std::string hexColor(uint32_t rgb); // "#rrggbb"
// Loads a palette's colours; a relative swatch path resolves against `directory`. Throws
// zodar::Error for an unreadable swatch, a malformed colour or an empty palette.
[[nodiscard]] Palette loadPalette(const PaletteSource& source, const std::filesystem::path& directory,
                                  std::string name);
// Command-line form (--palette): a comma-separated "#rrggbb" list, otherwise a PNG path.
[[nodiscard]] PaletteSource parsePaletteArgument(std::string_view text);

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
    // Sub-palette this asset may use in addition to the master palette (Phase C7); empty: the
    // master palette only. Must name a sub-palette of the registry's palette.
    std::string palette;

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

    // Optional palette (Phase C7); written as "palette" only when set.
    const std::optional<PaletteConfig>& palette() const { return m_palette; }
    void setPalette(std::optional<PaletteConfig> palette) { m_palette = std::move(palette); }

    bool operator==(const AssetRegistry& other) const {
        return m_entries == other.m_entries && m_palette == other.m_palette;
    }

private:
    std::filesystem::path m_directory;
    std::vector<AssetEntry> m_entries;
    std::optional<PaletteConfig> m_palette;
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
    std::optional<std::string> palette; // sub-palette name ("" clears); must exist in the registry
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
    // Palette check (colour textures only, when a palette applies).
    bool paletteChecked = false;
    uint64_t paletteTexels = 0;    // non-transparent texels (level 0)
    uint64_t offPaletteTexels = 0; // ... whose colour is not in the palette
};

// Palette deviation of one asset over all its colour textures (base colour, emissive; texture
// entries with usage color).
struct PaletteDeviation {
    std::string palette;           // "master" or "master+<sub>"
    uint64_t texels = 0;           // non-transparent texels checked
    uint64_t offPalette = 0;       // texels whose RGB is not in the palette
    std::vector<uint32_t> examples; // up to 4 distinct off-palette colours (0xRRGGBB)
    double percent() const { return texels ? 100.0 * double(offPalette) / double(texels) : 0.0; }
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
    // Set when a palette applies and at least one colour texture was checked.
    std::optional<PaletteDeviation> paletteDeviation;

    const char* status() const { return !errors.empty() ? "error" : !warnings.empty() ? "warning" : "ok"; }
};

struct AssetCheckOptions {
    bool checkTextureSizes = true;    // TextureStandards warnings
    double texelDensityTarget = 32.0; // docs/ART_DIRECTION.md: 32 texels per metre (32×32 per 1 m face)
    double texelDensityTolerance = 2.0; // warn outside [target / tol, target * tol]; <= 0 disables
    // Master palette for this check instead of the registry's (ZodarAssetTool --palette).
    // Without either, no palette check runs (and reports stay as before Phase C7).
    std::optional<PaletteSource> palette;
};

// Checks one entry / every entry (registry order). Never throws: problems become errors.
AssetCheckResult checkAsset(const AssetRegistry& registry, const AssetEntry& entry, const AssetCheckOptions& options = {});
std::vector<AssetCheckResult> checkRegistry(const AssetRegistry& registry, const AssetCheckOptions& options = {});

// JSON report, schema "zodar.asset_report" v1: { schema, version, registry, summary {assets,
// ok, warnings, errors}, assets: [{ id, type, source, status, errors, warnings, textures,
// meshes, vertices, triangles, materials, texelDensity }] }. With a palette check, assets also
// get "paletteDeviation": { palette, texels, offPalette, percent, examples ["#rrggbb"] } and
// textures get "paletteTexels" / "offPaletteTexels" (keys absent without a palette).
std::string assetReportJson(const AssetRegistry& registry, const std::vector<AssetCheckResult>& results);

} // namespace zodar
