#include <doctest/doctest.h>

#include "AssetPipeline/ModelCompiler.h"
#include "AssetPipeline/TextureCompiler.h"
#include "Assets/Ktx2.h"
#include "Assets/ModelData.h"
#include "Assets/TextureCache.h"
#include "Core/Error.h"
#include "../Support/TempFile.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace zodar;
using namespace zodar::assets;

namespace {

// Smooth gradients, edges and mild noise: a stand-in for real texture content.
ImageData testImage(uint32_t w, uint32_t h, ColorSpace space, uint32_t seed = 1) {
    ImageData img;
    img.name = "test";
    img.width = w;
    img.height = h;
    img.colorSpace = space;
    std::vector<uint8_t> px(size_t(w) * h * 4);
    uint32_t x32 = seed * 747796405u + 1;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            x32 ^= x32 << 13;
            x32 ^= x32 >> 17;
            x32 ^= x32 << 5;
            const int noise = int(x32 % 9) - 4;
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            const float u = float(x) / float(w), v = float(y) / float(h);
            p[0] = uint8_t(std::clamp(int(255 * u) + noise, 0, 255));
            p[1] = uint8_t(std::clamp(int(255 * v) + noise, 0, 255));
            p[2] = uint8_t(((x / 8 + y / 8) % 2) ? 200 : 40);
            p[3] = uint8_t(std::clamp(int(255 * (0.5f + 0.5f * std::sin(u * 6.0f))), 0, 255));
        }
    }
    img.levels.push_back(std::move(px));
    return img;
}

uint32_t read32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}
uint64_t read64(const std::vector<uint8_t>& b, size_t at) {
    uint64_t v;
    std::memcpy(&v, b.data() + at, 8);
    return v;
}
void write32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }

std::vector<uint8_t> png(uint32_t w, uint32_t h, uint8_t seed) {
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = uint8_t(i * 7 + seed);
    std::vector<uint8_t> out;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* o = static_cast<std::vector<uint8_t>*>(ctx);
            o->insert(o->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &out, int(w), int(h), 4, rgba.data(), int(w * 4));
    return out;
}

// Minimal glTF: one triangle, a material with base colour and normal textures that point
// to external PNG files next to the .gltf.
void writeModel(const std::filesystem::path& gltf) {
    std::ofstream(gltf) << R"({
  "asset": {"version": "2.0"},
  "buffers": [{"byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}],
  "bufferViews": [{"buffer": 0, "byteLength": 36}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0]}],
  "images": [{"uri": "albedo.png"}, {"uri": "normal.png"}],
  "textures": [{"source": 0}, {"source": 1}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}, "normalTexture": {"index": 1}}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
  "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0
})";
}

void writeBytes(const std::filesystem::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

} // namespace

TEST_CASE("ktx2: round trip of every format with baked mips and key/value data") {
    for (PixelFormat format : {PixelFormat::Rgba8, PixelFormat::Bc1, PixelFormat::Bc3, PixelFormat::Bc4,
                               PixelFormat::Bc5, PixelFormat::Bc7}) {
        const std::string formatName = toString(format);
        CAPTURE(formatName);
        const ColorSpace space =
            format == PixelFormat::Bc4 || format == PixelFormat::Bc5 ? ColorSpace::Linear : ColorSpace::Srgb;
        TextureCompileOptions o;
        o.format = format;
        o.colorSpace = space;
        o.quality = EncodeQuality::Fast;
        const ImageData compiled = compileTexture(testImage(37, 21, space), o); // Not a multiple of 4.
        CHECK(compiled.levels.size() == 6);                                       // 37x21 ... 1x1
        const std::vector<uint8_t> file = writeKtx2(compiled, {{"zodar.test", "value"}});
        const Ktx2File back = readKtx2(file, "roundtrip");
        CHECK((back.image.format == format));
        CHECK(back.image.colorSpace == space);
        CHECK(back.image.width == 37);
        CHECK(back.image.height == 21);
        CHECK(back.image.levels == compiled.levels);
        CHECK(back.keyValues.at("zodar.test") == "value");
        // Levels are stored smallest first, each aligned to the block size.
        uint64_t previous = 0;
        for (size_t i = compiled.levels.size(); i-- > 0;) {
            const uint64_t offset = read64(file, 80 + 24 * i);
            CHECK(offset > previous);
            CHECK(offset % (format == PixelFormat::Rgba8 ? 4 : format == PixelFormat::Bc1 || format == PixelFormat::Bc4 ? 8 : 16) == 0);
            previous = offset;
        }
        CHECK(read32(file, 12) == ktx2VkFormat(format, space));
    }
}

TEST_CASE("ktx2: corrupt and unsupported files are rejected with a reason") {
    TextureCompileOptions o;
    o.quality = EncodeQuality::Fast;
    const std::vector<uint8_t> good = writeKtx2(compileTexture(testImage(16, 16, ColorSpace::Srgb), o));
    CHECK_NOTHROW(readKtx2(good, "good"));

    auto expectError = [](std::vector<uint8_t> bytes, const char* what) {
        try {
            readKtx2(bytes, "bad.ktx2");
            FAIL("accepted a corrupt file: " << what);
        } catch (const Error& e) {
            CHECK(std::string(e.what()).find("bad.ktx2") != std::string::npos);
        }
    };
    auto bad = good;
    bad[5] = 'X';
    expectError(bad, "identifier");
    expectError(std::vector<uint8_t>(good.begin(), good.begin() + 100), "truncated");
    expectError(std::vector<uint8_t>(good.begin(), good.end() - 1), "truncated level data");
    bad = good;
    write32(bad, 12, 100); // VK_FORMAT_ASTC_*: unsupported
    expectError(bad, "vkFormat");
    bad = good;
    write32(bad, 40, 0); // levelCount 0 = "generate mips at load time"
    expectError(bad, "levelCount 0");
    bad = good;
    write32(bad, 44, 2); // Zstandard
    expectError(bad, "supercompression");
    bad = good;
    write32(bad, 80 + 8, 17); // level 0 byteLength
    expectError(bad, "level size");
    bad = good;
    write32(bad, 12, ktx2_vk::BC7_UNORM); // DFD still says sRGB
    expectError(bad, "transfer function");
}

TEST_CASE("bcn encoders: quality thresholds and deterministic output") {
    const ImageData color = testImage(128, 96, ColorSpace::Srgb);
    const ImageData data = testImage(128, 96, ColorSpace::Linear, 2);
    struct Case {
        PixelFormat format;
        const ImageData* source;
        uint32_t channels;
        double minPsnr;
    };
    // Thresholds are well below what the encoders reach; they catch broken encoding.
    const Case cases[] = {{PixelFormat::Bc7, &color, 4, 40.0}, {PixelFormat::Bc1, &color, 3, 30.0},
                          {PixelFormat::Bc3, &color, 4, 30.0}, {PixelFormat::Bc4, &data, 1, 40.0},
                          {PixelFormat::Bc5, &data, 2, 38.0}};
    for (const Case& c : cases) {
        const std::string formatName = toString(c.format);
        CAPTURE(formatName);
        const ImageData encoded = encodeImage(*c.source, c.format, EncodeQuality::Normal, 4);
        CHECK(encoded.levels[0].size() == pixelFormatLevelSize(c.format, 128, 96));
        const double quality = psnr(decompressToRgba8(encoded).levels[0], c.source->levels[0], c.channels);
        MESSAGE(formatName << " PSNR " << quality << " dB");
        CHECK(quality >= c.minPsnr);
        // Same bytes regardless of the number of threads.
        CHECK(encodeImage(*c.source, c.format, EncodeQuality::Normal, 1).levels == encoded.levels);
    }
    CHECK_THROWS(encodeImage(color, PixelFormat::Bc5, EncodeQuality::Fast)); // BC5 has no sRGB variant.
}

TEST_CASE("compiled textures: baked mips match the engine's mip filter") {
    TextureCompileOptions o;
    o.format = PixelFormat::Rgba8; // Exact comparison.
    o.colorSpace = ColorSpace::Linear;
    o.mipFilter = MipFilter::NormalMap;
    ImageData source = testImage(32, 32, ColorSpace::Linear);
    const ImageData compiled = compileTexture(source, o);
    source.generateMips(MipFilter::NormalMap);
    CHECK(compiled.levels == source.levels);
    o.mips = false;
    CHECK(compileTexture(source, o).levels.size() == 1);
}

TEST_CASE("model texture cache: compiled when fresh, ignored when the source changes") {
    const zodar::test::TempFile model("zodar_cache_test.gltf");
    const std::filesystem::path dir = model.path().parent_path();
    const std::filesystem::path cacheDir = textureCacheDirectory(model.path());
    std::filesystem::remove_all(cacheDir);
    writeModel(model.path());
    writeBytes(dir / "albedo.png", png(20, 12, 1));
    writeBytes(dir / "normal.png", png(20, 12, 2));

    // Not compiled: source images, runtime mips.
    ModelData raw = loadGltf(model.path());
    CHECK(raw.textureStats.source == 2);
    CHECK(raw.textureStats.runtimeMips == 2);
    CHECK(raw.textureStats.compiled == 0);

    ModelCompileOptions co;
    co.quality = EncodeQuality::Fast;
    const auto results = compileModelTextures(model.path(), co);
    REQUIRE(results.size() == 2);
    CHECK(std::filesystem::exists(textureCachePath(model.path(), 0, TextureUsage::Color)));
    CHECK(std::filesystem::exists(textureCachePath(model.path(), 1, TextureUsage::Normal)));
    CHECK(compileModelTextures(model.path(), co)[0].upToDate); // Second run skips.

    ModelData compiled = loadGltf(model.path());
    CHECK(compiled.textureStats.compiled == 2);
    CHECK(compiled.textureStats.runtimeMips == 0);
    REQUIRE(compiled.images.size() == 2);
    CHECK((compiled.images[0].format == PixelFormat::Bc7));
    CHECK(compiled.images[0].colorSpace == ColorSpace::Srgb);
    CHECK(compiled.images[1].colorSpace == ColorSpace::Linear);
    CHECK(compiled.images[0].levels.size() == 5); // 20x12 ... 1x1, baked
    CHECK(compiled.materials[0].baseColor.image == 0);
    CHECK(compiled.materials[0].normal.image == 1);

    // Edited source image: the stale cache entry is ignored.
    writeBytes(dir / "albedo.png", png(20, 12, 9));
    ModelData edited = loadGltf(model.path());
    CHECK(edited.textureStats.stale == 1);
    CHECK(edited.textureStats.compiled == 1);
    CHECK((edited.images[0].format == PixelFormat::Rgba8));

    GltfLoadOptions sourceOnly;
    sourceOnly.useCompiledTextures = false;
    CHECK(loadGltf(model.path(), sourceOnly).textureStats.compiled == 0);

    std::filesystem::remove_all(cacheDir);
    std::filesystem::remove(dir / "albedo.png");
    std::filesystem::remove(dir / "normal.png");
}
