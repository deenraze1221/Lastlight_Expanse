// ZodarAssetCompiler: offline texture compilation (baked mips + BCn + KTX2).
//
//   ZodarAssetCompiler texture <input.png|jpg|tga|bmp> <output.ktx2>
//       [--format bc7|bc1|bc3|bc4|bc5|rgba8] [--srgb|--linear] [--normal] [--no-mips]
//       [--quality fast|normal|high] [--threads N] [--pixel-art]
//   ZodarAssetCompiler model <model.gltf|glb> [--quality fast|normal|high] [--threads N] [--force]
//       [--report report.json] [--pixel-art]
//       Compiles every texture of the model into <model>.zodar/ (loadGltf uses it automatically
//       while the source images are unchanged).
//   --pixel-art: lossless output only (RGBA8 KTX2; a BCn --format is an error). Model textures
//       with a NEAREST magnification sampler are pixel art without the flag.
//   ZodarAssetCompiler info <file.ktx2>
//
// Exit codes: 0 ok, 1 error.

#include "AssetPipeline/ModelCompiler.h"
#include "AssetPipeline/TextureCompiler.h"
#include "Assets/Ktx2.h"
#include "Core/Error.h"
#include "Core/Log.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace zodar;
using namespace zodar::assets;

namespace {

struct Args {
    std::vector<std::string> positional;
    TextureCompileOptions texture;
    bool srgbSet = false, normal = false, formatSet = false;
    ModelCompileOptions model;
    std::string report;
};

EncodeQuality parseQuality(const std::string& q) {
    if (q == "fast") return EncodeQuality::Fast;
    if (q == "normal") return EncodeQuality::Normal;
    if (q == "high") return EncodeQuality::High;
    fail("--quality must be fast, normal or high");
}

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) fail("Missing value for " + std::string(arg));
            return argv[++i];
        };
        if (arg == "--format") { a.texture.format = parsePixelFormat(next()); a.formatSet = true; }
        else if (arg == "--srgb") { a.texture.colorSpace = ColorSpace::Srgb; a.srgbSet = true; }
        else if (arg == "--linear") { a.texture.colorSpace = ColorSpace::Linear; a.srgbSet = true; }
        else if (arg == "--normal") a.normal = true;
        else if (arg == "--no-mips") a.texture.mips = false;
        else if (arg == "--quality") a.model.quality = a.texture.quality = parseQuality(next());
        else if (arg == "--threads") a.model.threads = a.texture.threads = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "--force") a.model.force = true;
        else if (arg == "--pixel-art") { a.texture.pixelArt = a.model.pixelArt = true; }
        else if (arg == "--report") a.report = next();
        else if (arg.starts_with("--")) fail("Unknown option '" + std::string(arg) + "'");
        else a.positional.emplace_back(arg);
    }
    if (a.texture.pixelArt && !a.formatSet) {
        a.texture.format = PixelFormat::Rgba8;
    }
    if (a.normal) {
        a.texture.mipFilter = MipFilter::NormalMap;
        if (!a.srgbSet) a.texture.colorSpace = ColorSpace::Linear;
    }
    if ((a.texture.format == PixelFormat::Bc4 || a.texture.format == PixelFormat::Bc5) && !a.srgbSet) {
        a.texture.colorSpace = ColorSpace::Linear;
    }
    return a;
}

double mib(size_t bytes) { return double(bytes) / 1048576.0; }

int textureCommand(const Args& a) {
    if (a.positional.size() != 2) fail("usage: texture <input> <output.ktx2> [options]");
    const auto start = std::chrono::steady_clock::now();
    const ImageData source = loadImage(a.positional[0], a.texture.colorSpace);
    const ImageData compiled = compileTexture(source, a.texture);
    saveKtx2(a.positional[1], compiled, compilerKeyValues(a.texture));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ImageData level0 = compiled;
    level0.levels.resize(1);
    const double quality = psnr(decompressToRgba8(level0).levels[0], source.levels[0], a.normal ? 3 : 4);
    std::printf("%s -> %s: %ux%u %s %s, %zu levels, %.2f MiB (RGBA8 level 0 %.2f MiB), PSNR %.2f dB, %.2f s\n",
                a.positional[0].c_str(), a.positional[1].c_str(), compiled.width, compiled.height,
                toString(compiled.format), compiled.colorSpace == ColorSpace::Srgb ? "sRGB" : "linear",
                compiled.levels.size(), mib(compiled.byteSize()), mib(source.levels[0].size()), quality, seconds);
    return 0;
}

int modelCommand(const Args& a) {
    if (a.positional.size() != 1) fail("usage: model <model.gltf|glb> [options]");
    const auto start = std::chrono::steady_clock::now();
    const std::vector<CompiledTexture> results = compileModelTextures(a.positional[0], a.model);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    size_t rgba = 0, compiled = 0, compiledCount = 0;
    for (const CompiledTexture& r : results) {
        std::printf("  %-48s %-6s %4ux%-4u %s %2u levels %8.2f MiB  %s\n", r.path.filename().string().c_str(),
                    toString(r.usage), r.width, r.height, toString(r.format), r.levels, mib(r.compiledBytes),
                    r.upToDate ? "up to date" : (std::to_string(r.psnr).substr(0, 5) + " dB PSNR, " +
                                                 std::to_string(r.seconds).substr(0, 5) + " s").c_str());
        if (!r.upToDate) {
            rgba += r.rgba8Bytes;
            compiled += r.compiledBytes;
            ++compiledCount;
        }
    }
    std::printf("%zu textures (%zu compiled, %zu up to date) in %.2f s; compiled %.2f MiB vs RGBA8+mips %.2f MiB\n",
                results.size(), compiledCount, results.size() - compiledCount, seconds, mib(compiled), mib(rgba));
    if (!a.report.empty()) {
        std::ostringstream j;
        j << "{\n  \"model\": \"" << a.positional[0] << "\", \"seconds\": " << seconds << ",\n  \"textures\": [";
        for (size_t i = 0; i < results.size(); ++i) {
            const CompiledTexture& r = results[i];
            j << (i ? ",\n" : "\n") << "    {\"file\": \"" << r.path.filename().string() << "\", \"usage\": \""
              << toString(r.usage) << "\", \"format\": \"" << toString(r.format) << "\", \"width\": " << r.width
              << ", \"height\": " << r.height << ", \"levels\": " << r.levels << ", \"sourceBytes\": " << r.sourceBytes
              << ", \"rgba8MipBytes\": " << r.rgba8Bytes << ", \"compiledBytes\": " << r.compiledBytes
              << ", \"upToDate\": " << (r.upToDate ? "true" : "false") << ", \"psnr\": " << r.psnr
              << ", \"seconds\": " << r.seconds << "}";
        }
        j << "\n  ]\n}\n";
        std::ofstream(a.report) << j.str();
    }
    return 0;
}

int infoCommand(const Args& a) {
    if (a.positional.size() != 1) fail("usage: info <file.ktx2>");
    const Ktx2File file = loadKtx2(a.positional[0]);
    const ImageData& i = file.image;
    std::printf("%s: %ux%u %s %s, %zu levels, %.2f MiB texel data\n", a.positional[0].c_str(), i.width, i.height,
                toString(i.format), i.colorSpace == ColorSpace::Srgb ? "sRGB" : "linear", i.levels.size(),
                mib(i.byteSize()));
    for (size_t l = 0; l < i.levels.size(); ++l) {
        std::printf("  level %2zu: %5ux%-5u %10zu bytes\n", l, i.levelWidth(l), i.levelHeight(l), i.levels[l].size());
    }
    for (const auto& [k, v] : file.keyValues) std::printf("  %s = %s\n", k.c_str(), v.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) fail("usage: ZodarAssetCompiler texture|model|info ... (see source header)");
        const std::string command = argv[1];
        const Args args = parse(argc, argv);
        if (command == "texture") return textureCommand(args);
        if (command == "model") return modelCommand(args);
        if (command == "info") return infoCommand(args);
        fail("Unknown command '" + command + "'");
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
        return 1;
    }
}
