#include "AssetPipeline/TextureCompiler.h"

#include "Core/Error.h"

#include <bc7enc.h>
#include <rgbcx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

namespace zodar::assets {
namespace {

void initEncoders() {
    static std::once_flag once;
    std::call_once(once, [] {
        rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
        bc7enc_compress_block_init();
    });
}

size_t blockBytes(PixelFormat f) {
    return f == PixelFormat::Bc1 || f == PixelFormat::Bc4 ? 8 : 16;
}

// 4x4 RGBA block at (bx, by), replicating edge texels for partial blocks.
void fetchBlock(const std::vector<uint8_t>& src, uint32_t w, uint32_t h, uint32_t bx, uint32_t by, uint8_t* out) {
    for (uint32_t y = 0; y < 4; ++y) {
        const uint32_t sy = std::min(by * 4 + y, h - 1);
        for (uint32_t x = 0; x < 4; ++x) {
            const uint32_t sx = std::min(bx * 4 + x, w - 1);
            std::copy_n(&src[(size_t(sy) * w + sx) * 4], 4, &out[(y * 4 + x) * 4]);
        }
    }
}

struct Encoder {
    PixelFormat format;
    EncodeQuality quality;
    bc7enc_compress_block_params bc7{};

    Encoder(PixelFormat f, EncodeQuality q, bool perceptual) : format(f), quality(q) {
        bc7enc_compress_block_params_init(&bc7);
        if (!perceptual) bc7enc_compress_block_params_init_linear_weights(&bc7);
        switch (q) {
        case EncodeQuality::Fast: bc7.m_max_partitions = 0; bc7.m_uber_level = 0; bc7.m_try_least_squares = false; break;
        case EncodeQuality::Normal: bc7.m_max_partitions = 16; bc7.m_uber_level = 0; break;
        case EncodeQuality::High: bc7.m_max_partitions = BC7ENC_MAX_PARTITIONS; bc7.m_uber_level = 2; break;
        }
    }

    void encode(const uint8_t* pixels, uint8_t* dst) const {
        const uint32_t level = quality == EncodeQuality::Fast ? 2 : quality == EncodeQuality::Normal ? 10 : 18;
        switch (format) {
        // No 3-colour BC1 blocks: their 4th index decodes to transparent black in RGBA formats.
        case PixelFormat::Bc1: rgbcx::encode_bc1(level, dst, pixels, false, false); break;
        case PixelFormat::Bc3: rgbcx::encode_bc3(level, dst, pixels); break;
        case PixelFormat::Bc4: rgbcx::encode_bc4_hq(dst, pixels, 4); break;
        case PixelFormat::Bc5: rgbcx::encode_bc5_hq(dst, pixels, 0, 1, 4); break;
        case PixelFormat::Bc7: bc7enc_compress_block(dst, pixels, &bc7); break;
        case PixelFormat::Rgba8:
        case PixelFormat::Rgba16f: break;
        }
    }
};

} // namespace

TextureCompileOptions optionsFor(TextureUsage usage) {
    TextureCompileOptions o;
    o.format = PixelFormat::Bc7;
    o.colorSpace = colorSpaceFor(usage);
    o.mipFilter = mipFilterFor(usage);
    return o;
}

ImageData encodeImage(const ImageData& rgba8, PixelFormat format, EncodeQuality quality, uint32_t threads) {
    ZODAR_ENSURE(format != PixelFormat::Rgba16f, "encodeImage: float output is not supported");
    ZODAR_ENSURE(rgba8.format == PixelFormat::Rgba8, "encodeImage: source '" + rgba8.name + "' is not RGBA8");
    ZODAR_ENSURE(!(rgba8.colorSpace == ColorSpace::Srgb && (format == PixelFormat::Bc4 || format == PixelFormat::Bc5)),
                 "encodeImage: BC4/BC5 store linear data only");
    if (format == PixelFormat::Rgba8) {
        return rgba8;
    }
    initEncoders();
    const Encoder encoder(format, quality, rgba8.colorSpace == ColorSpace::Srgb);
    ImageData out;
    out.name = rgba8.name;
    out.width = rgba8.width;
    out.height = rgba8.height;
    out.colorSpace = rgba8.colorSpace;
    out.format = format;
    const uint32_t workers = std::max(1u, threads ? threads : std::thread::hardware_concurrency());
    for (size_t level = 0; level < rgba8.levels.size(); ++level) {
        const uint32_t w = rgba8.levelWidth(level), h = rgba8.levelHeight(level);
        const std::vector<uint8_t>& src = rgba8.levels[level];
        ZODAR_ENSURE(src.size() == size_t(w) * h * 4, "encodeImage: level size mismatch");
        const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        std::vector<uint8_t> dst(size_t(bw) * bh * blockBytes(format));
        // Rows of blocks are independent: any thread count gives the same bytes.
        std::atomic<uint32_t> nextRow{0};
        const auto work = [&] {
            uint8_t pixels[64];
            for (uint32_t by = nextRow++; by < bh; by = nextRow++) {
                for (uint32_t bx = 0; bx < bw; ++bx) {
                    fetchBlock(src, w, h, bx, by, pixels);
                    encoder.encode(pixels, &dst[(size_t(by) * bw + bx) * blockBytes(format)]);
                }
            }
        };
        const uint32_t count = std::min(workers, bh);
        std::vector<std::thread> pool;
        for (uint32_t t = 1; t < count; ++t) pool.emplace_back(work);
        work();
        for (auto& t : pool) t.join();
        out.levels.push_back(std::move(dst));
    }
    return out;
}

ImageData compileTexture(const ImageData& source, const TextureCompileOptions& options) {
    ZODAR_ENSURE(source.format == PixelFormat::Rgba8 && !source.levels.empty(),
                 "compileTexture: source '" + source.name + "' must be decoded RGBA8");
    ImageData image = source;
    image.colorSpace = options.colorSpace;
    image.levels.resize(1);
    if (options.mips) {
        image.generateMips(options.mipFilter);
    }
    return encodeImage(image, options.format, options.quality, options.threads);
}

double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t channels) {
    ZODAR_ENSURE(a.size() == b.size() && a.size() % 4 == 0, "psnr: size mismatch");
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i += 4) {
        for (uint32_t c = 0; c < channels; ++c) {
            const double d = double(a[i + c]) - double(b[i + c]);
            sum += d * d;
            ++n;
        }
    }
    if (sum == 0.0) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / (sum / double(n)));
}

std::map<std::string, std::string> compilerKeyValues(const TextureCompileOptions& options) {
    const char* quality = options.quality == EncodeQuality::Fast ? "fast"
                          : options.quality == EncodeQuality::Normal ? "normal" : "high";
    return {{"KTXwriter", "ZodarAssetCompiler " + std::to_string(kTextureCompilerVersion)},
            {"zodar.encoder", std::string(toString(options.format)) + "/" + quality},
            {"zodar.mipFilter", options.mipFilter == MipFilter::NormalMap ? "normal" : "box"}};
}

PixelFormat parsePixelFormat(const std::string& name) {
    for (PixelFormat f : {PixelFormat::Rgba8, PixelFormat::Bc1, PixelFormat::Bc3, PixelFormat::Bc4, PixelFormat::Bc5,
                          PixelFormat::Bc7}) {
        if (name == toString(f)) return f;
    }
    fail("Unknown texture format '" + name + "' (rgba8, bc1, bc3, bc4, bc5, bc7)");
}

} // namespace zodar::assets
