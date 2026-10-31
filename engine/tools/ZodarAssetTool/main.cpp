// ZodarAssetTool: asset registry tool (Phase C3). CPU only, no Vulkan device needed.
//
//   ZodarAssetTool import <file>... [--registry FILE] [--id ID] [--type texture|model|material|scene|prefab]
//       [--usage color|data|normal] [--no-mips] [--sampling file|pixelArt|nearest|linear] [--compile]
//       [--sub-palette NAME]
//       Adds or updates registry entries (dependencies + hash). --compile (models) bakes the
//       textures into <model>.zodar/*.ktx2 (ZodarAssetCompiler's cache) and records them.
//       Pixel-art textures (NEAREST glTF sampler, or --sampling pixelArt|nearest) compile
//       losslessly (RGBA8); others use BC7.
//       Default registry: the nearest assets.json in the file's directory or a parent, else
//       ./assets.json (created).
//   ZodarAssetTool list [--registry FILE] [--json]
//   ZodarAssetTool check [--registry FILE] [--report FILE.json] [--strict] [--density-target N]
//       [--palette swatch.png|#rrggbb,#rrggbb...]
//       Validates every entry (missing/stale/invalid, texture sizes, texel density, palette).
//       Prints one line per asset with problems and a summary; --report writes
//       zodar.asset_report JSON. --palette replaces the registry's master palette for this run
//       (entries' sub-palettes still apply); off-palette texels are warnings (--strict: errors).
//
// Exit codes: 0 ok, 1 errors (check: any asset error; --strict: warnings too), 2 usage.
// Guide: docs/guide/ASSETS.md.

#include "AssetManager/AssetRegistry.h"
#include "AssetPipeline/ModelCompiler.h"
#include "Core/Error.h"
#include "Core/Log.h"
#include "SceneFile/DataError.h"

#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace zodar;

namespace {

constexpr const char* kUsage =
    "usage: ZodarAssetTool import <file>... [--registry FILE] [--id ID] [--type T] [--usage color|data|normal]\n"
    "                   [--no-mips] [--sampling file|pixelArt|nearest|linear] [--compile] [--sub-palette NAME]\n"
    "       ZodarAssetTool list [--registry FILE] [--json]\n"
    "       ZodarAssetTool check [--registry FILE] [--report FILE.json] [--strict] [--density-target N]\n"
    "                   [--palette swatch.png|#rrggbb,#rrggbb...]\n";

struct Args {
    std::string command;
    std::vector<std::string> files;
    std::string registry;
    AssetImportOptions import;
    bool json = false;
    bool strict = false;
    std::string report;
    AssetCheckOptions check;
};

[[noreturn]] void usage(const std::string& message) {
    std::fprintf(stderr, "ZodarAssetTool: %s\n%s", message.c_str(), kUsage);
    std::exit(2);
}

Args parse(int argc, char** argv) {
    if (argc < 2) {
        usage("missing command");
    }
    Args a;
    a.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) usage("missing value for " + std::string(arg));
            return argv[++i];
        };
        if (arg == "--registry") a.registry = next();
        else if (arg == "--id") a.import.id = next();
        else if (arg == "--type") {
            const std::string t = next();
            a.import.type = parseAssetType(t);
            if (!a.import.type) usage("unknown --type '" + t + "'");
        } else if (arg == "--usage") {
            const std::string u = next();
            if (u == "color") a.import.usage = TextureUsage::Color;
            else if (u == "data") a.import.usage = TextureUsage::Data;
            else if (u == "normal") a.import.usage = TextureUsage::Normal;
            else usage("unknown --usage '" + u + "'");
        } else if (arg == "--no-mips") a.import.mips = false;
        else if (arg == "--sampling") {
            const std::string s = next();
            if (s == "file") a.import.sampling = ModelSampling::File;
            else if (s == "pixelArt") a.import.sampling = ModelSampling::PixelArt;
            else if (s == "nearest") a.import.sampling = ModelSampling::Nearest;
            else if (s == "linear") a.import.sampling = ModelSampling::Linear;
            else usage("unknown --sampling '" + s + "'");
        } else if (arg == "--compile") a.import.compile = true;
        else if (arg == "--json") a.json = true;
        else if (arg == "--strict") a.strict = true;
        else if (arg == "--report") a.report = next();
        else if (arg == "--density-target") a.check.texelDensityTarget = std::stod(next());
        else if (arg == "--palette") {
            PaletteSource palette = parsePaletteArgument(next());
            for (const std::string& c : palette.colors) {
                if (!parseHexColor(c)) usage("malformed --palette colour '" + c + "' (expected #rrggbb)");
            }
            if (!palette.swatch.empty()) palette.swatch = std::filesystem::absolute(palette.swatch).generic_string();
            a.check.palette = std::move(palette);
        } else if (arg == "--sub-palette") a.import.palette = next();
        else if (arg == "--help" || arg == "-h") {
            std::fputs(kUsage, stdout);
            std::exit(0);
        } else if (arg.starts_with("--")) usage("unknown option '" + std::string(arg) + "'");
        else a.files.emplace_back(arg);
    }
    return a;
}

std::filesystem::path findRegistry(const Args& a) {
    if (!a.registry.empty()) {
        return a.registry;
    }
    if (a.command == "import" && !a.files.empty()) {
        std::filesystem::path dir = std::filesystem::absolute(a.files.front()).parent_path();
        for (;;) {
            if (std::filesystem::exists(dir / AssetRegistry::kFileName)) {
                return dir / AssetRegistry::kFileName;
            }
            if (!dir.has_parent_path() || dir.parent_path() == dir) {
                break;
            }
            dir = dir.parent_path();
        }
    }
    return std::filesystem::absolute(AssetRegistry::kFileName);
}

int cmdImport(const Args& a) {
    if (a.files.empty()) usage("import needs at least one file");
    if (!a.import.id.empty() && a.files.size() > 1) usage("--id needs a single file");
    const std::filesystem::path path = findRegistry(a);
    AssetRegistry registry = std::filesystem::exists(path) ? AssetRegistry::load(path)
                                                           : AssetRegistry(std::filesystem::absolute(path).parent_path());
    for (const std::string& file : a.files) {
        AssetEntry entry = importAsset(registry, file, a.import); // copy: upsert below may move entries
        if (entry.type == AssetType::Model && entry.compile) {
            assets::ModelCompileOptions compile;
            compile.pixelArt = entry.sampling == ModelSampling::PixelArt || entry.sampling == ModelSampling::Nearest;
            const auto compiled = assets::compileModelTextures(registry.sourcePath(entry), compile);
            entry.outputs.clear();
            const std::filesystem::path dir = std::filesystem::absolute(registry.directory()).lexically_normal();
            for (const auto& t : compiled) {
                entry.outputs.push_back(
                    std::filesystem::absolute(t.path).lexically_normal().lexically_relative(dir).generic_string());
            }
            std::ranges::sort(entry.outputs);
            registry.upsert(entry);
        }
        std::printf("imported %s (%s) %s %s, %zu dependencies%s\n", entry.id.c_str(), toString(entry.type),
                    entry.source.c_str(), entry.hash.c_str(), entry.dependencies.size(),
                    entry.outputs.empty() ? "" : (", " + std::to_string(entry.outputs.size()) + " compiled").c_str());
    }
    registry.save(path);
    std::printf("registry: %s (%zu assets)\n", path.generic_string().c_str(), registry.entries().size());
    return 0;
}

int cmdList(const Args& a) {
    const std::filesystem::path path = findRegistry(a);
    const AssetRegistry registry = AssetRegistry::load(path);
    if (a.json) {
        std::fputs(registry.serialize().c_str(), stdout);
        return 0;
    }
    for (const AssetEntry& e : registry.entries()) {
        std::printf("%-24s %-9s %s\n", e.id.c_str(), toString(e.type), e.source.c_str());
    }
    std::printf("%zu assets in %s\n", registry.entries().size(), path.generic_string().c_str());
    return 0;
}

int cmdCheck(const Args& a) {
    const std::filesystem::path path = findRegistry(a);
    const AssetRegistry registry = AssetRegistry::load(path);
    const std::vector<AssetCheckResult> results = checkRegistry(registry, a.check);
    size_t errors = 0, warnings = 0;
    for (const AssetCheckResult& r : results) {
        for (const std::string& e : r.errors) {
            std::printf("error   %s: %s\n", r.id.c_str(), e.c_str());
        }
        for (const std::string& w : r.warnings) {
            std::printf("warning %s: %s\n", r.id.c_str(), w.c_str());
        }
        errors += !r.errors.empty();
        warnings += r.errors.empty() && !r.warnings.empty();
    }
    if (!a.report.empty()) {
        std::ofstream(a.report, std::ios::binary) << assetReportJson(registry, results);
        std::printf("report: %s\n", a.report.c_str());
    }
    uint64_t paletteTexels = 0, offPalette = 0;
    size_t paletteAssets = 0;
    for (const AssetCheckResult& r : results) {
        if (r.paletteDeviation) {
            ++paletteAssets;
            paletteTexels += r.paletteDeviation->texels;
            offPalette += r.paletteDeviation->offPalette;
        }
    }
    if (paletteAssets > 0) {
        std::printf("palette: %zu assets, %llu texels checked, %llu off-palette (%.2f%%)\n", paletteAssets,
                    static_cast<unsigned long long>(paletteTexels), static_cast<unsigned long long>(offPalette),
                    paletteTexels ? 100.0 * double(offPalette) / double(paletteTexels) : 0.0);
    }
    std::printf("checked %zu assets: %zu ok, %zu with warnings, %zu with errors\n", results.size(),
                results.size() - errors - warnings, warnings, errors);
    return errors > 0 || (a.strict && warnings > 0) ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    const Args a = parse(argc, argv);
    try {
        if (a.command == "import") return cmdImport(a);
        if (a.command == "list") return cmdList(a);
        if (a.command == "check") return cmdCheck(a);
        usage("unknown command '" + a.command + "'");
    } catch (const DataError& e) {
        std::fprintf(stderr, "ZodarAssetTool: %s\n", e.what());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ZodarAssetTool: %s\n", e.what());
    }
    return 1;
}
