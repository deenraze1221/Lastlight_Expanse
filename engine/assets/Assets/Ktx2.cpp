#include "Assets/Ktx2.h"

#include "Core/Error.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace zodar {
namespace {

constexpr uint8_t kIdentifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr size_t kHeaderBytes = 80; // Identifier + header + index.
constexpr size_t kLevelIndexBytes = 24;

// Data Format Descriptor constants (Khronos Data Format Specification 1.3).
constexpr uint32_t kModelRgbsda = 1, kModelBc1a = 128, kModelBc3 = 130, kModelBc4 = 131, kModelBc5 = 132,
                   kModelBc7 = 134;
constexpr uint32_t kPrimariesBt709 = 1, kTransferLinear = 1, kTransferSrgb = 2;
constexpr uint32_t kQualifierLinear = 1u << 28;

struct Format {
    PixelFormat format;
    ColorSpace colorSpace;
};

bool fromVkFormat(uint32_t vk, Format& out) {
    using namespace ktx2_vk;
    switch (vk) {
    case R8G8B8A8_UNORM: out = {PixelFormat::Rgba8, ColorSpace::Linear}; return true;
    case R8G8B8A8_SRGB: out = {PixelFormat::Rgba8, ColorSpace::Srgb}; return true;
    case BC1_RGBA_UNORM: out = {PixelFormat::Bc1, ColorSpace::Linear}; return true;
    case BC1_RGBA_SRGB: out = {PixelFormat::Bc1, ColorSpace::Srgb}; return true;
    case BC3_UNORM: out = {PixelFormat::Bc3, ColorSpace::Linear}; return true;
    case BC3_SRGB: out = {PixelFormat::Bc3, ColorSpace::Srgb}; return true;
    case BC4_UNORM: out = {PixelFormat::Bc4, ColorSpace::Linear}; return true;
    case BC5_UNORM: out = {PixelFormat::Bc5, ColorSpace::Linear}; return true;
    case BC7_UNORM: out = {PixelFormat::Bc7, ColorSpace::Linear}; return true;
    case BC7_SRGB: out = {PixelFormat::Bc7, ColorSpace::Srgb}; return true;
    default: return false;
    }
}

uint32_t blockBytes(PixelFormat f) {
    return f == PixelFormat::Rgba8 ? 4 : (f == PixelFormat::Bc1 || f == PixelFormat::Bc4) ? 8 : 16;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void set32(std::vector<uint8_t>& out, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) out[at + size_t(i)] = static_cast<uint8_t>(v >> (8 * i));
}
void set64(std::vector<uint8_t>& out, size_t at, uint64_t v) {
    for (int i = 0; i < 8; ++i) out[at + size_t(i)] = static_cast<uint8_t>(v >> (8 * i));
}
void pad(std::vector<uint8_t>& out, size_t alignment) {
    while (out.size() % alignment != 0) out.push_back(0);
}

struct Sample {
    uint32_t channel;
    uint32_t bitOffset;
    uint32_t bitLength;
    uint32_t qualifiers;
    uint32_t upper;
};

std::vector<uint8_t> makeDfd(PixelFormat format, ColorSpace space) {
    uint32_t model = 0;
    std::vector<Sample> samples;
    const uint32_t alphaQualifier = space == ColorSpace::Srgb ? kQualifierLinear : 0;
    switch (format) {
    case PixelFormat::Rgba8:
        model = kModelRgbsda;
        samples = {{0, 0, 8, 0, 255}, {1, 8, 8, 0, 255}, {2, 16, 8, 0, 255}, {15, 24, 8, alphaQualifier, 255}};
        break;
    case PixelFormat::Bc1: model = kModelBc1a; samples = {{1, 0, 64, 0, 0xFFFFFFFFu}}; break;
    case PixelFormat::Bc3:
        model = kModelBc3;
        samples = {{15, 0, 64, alphaQualifier, 0xFFFFFFFFu}, {0, 64, 64, 0, 0xFFFFFFFFu}};
        break;
    case PixelFormat::Bc4: model = kModelBc4; samples = {{0, 0, 64, 0, 0xFFFFFFFFu}}; break;
    case PixelFormat::Bc5:
        model = kModelBc5;
        samples = {{0, 0, 64, 0, 0xFFFFFFFFu}, {1, 64, 64, 0, 0xFFFFFFFFu}};
        break;
    case PixelFormat::Bc7: model = kModelBc7; samples = {{0, 0, 128, 0, 0xFFFFFFFFu}}; break;
    case PixelFormat::Rgba16f: break; // Rejected by writeKtx2.
    }
    const uint32_t blockSize = 24 + 16 * static_cast<uint32_t>(samples.size());
    const bool compressed = isBlockCompressed(format);
    std::vector<uint8_t> dfd;
    put32(dfd, 4 + blockSize);                          // dfdTotalSize
    put32(dfd, 0);                                      // vendorId 0 (Khronos), descriptorType 0 (basic)
    put32(dfd, 2u | (blockSize << 16));                 // versionNumber 2, descriptorBlockSize
    put32(dfd, model | (kPrimariesBt709 << 8) |
                   ((space == ColorSpace::Srgb ? kTransferSrgb : kTransferLinear) << 16)); // flags 0: straight alpha
    put32(dfd, compressed ? (3u | (3u << 8)) : 0u);     // texelBlockDimension - 1
    put32(dfd, blockBytes(format));                     // bytesPlane0
    put32(dfd, 0);                                      // bytesPlane4..7
    for (const Sample& s : samples) {
        put32(dfd, s.bitOffset | ((s.bitLength - 1) << 16) | (s.channel << 24) | s.qualifiers);
        put32(dfd, 0); // samplePosition
        put32(dfd, 0); // sampleLower
        put32(dfd, s.upper);
    }
    return dfd;
}

class Reader {
public:
    Reader(std::span<const uint8_t> bytes, const std::string& name) : m_bytes(bytes), m_name(name) {}
    uint32_t u32(size_t at) const {
        check(at, 4);
        uint32_t v;
        std::memcpy(&v, m_bytes.data() + at, 4);
        return v;
    }
    uint64_t u64(size_t at) const {
        check(at, 8);
        uint64_t v;
        std::memcpy(&v, m_bytes.data() + at, 8);
        return v;
    }
    void check(uint64_t at, uint64_t size) const {
        if (at > m_bytes.size() || size > m_bytes.size() - at) error("data out of range (truncated or corrupt file)");
    }
    [[noreturn]] void error(const std::string& what) const { fail("KTX2 '" + m_name + "': " + what); }
    std::span<const uint8_t> bytes() const { return m_bytes; }

private:
    std::span<const uint8_t> m_bytes;
    const std::string& m_name;
};

} // namespace

uint32_t ktx2VkFormat(PixelFormat format, ColorSpace space) {
    using namespace ktx2_vk;
    const bool srgb = space == ColorSpace::Srgb;
    switch (format) {
    case PixelFormat::Rgba8: return srgb ? R8G8B8A8_SRGB : R8G8B8A8_UNORM;
    case PixelFormat::Bc1: return srgb ? BC1_RGBA_SRGB : BC1_RGBA_UNORM;
    case PixelFormat::Bc3: return srgb ? BC3_SRGB : BC3_UNORM;
    case PixelFormat::Bc4: return BC4_UNORM;
    case PixelFormat::Bc5: return BC5_UNORM;
    case PixelFormat::Bc7: return srgb ? BC7_SRGB : BC7_UNORM;
    case PixelFormat::Rgba16f: return 0; // Not supported in KTX2 files yet.
    }
    return 0;
}

std::vector<uint8_t> writeKtx2(const ImageData& image, const std::map<std::string, std::string>& keyValues) {
    ZODAR_ENSURE(image.format != PixelFormat::Rgba16f, "writeKtx2: float formats are not supported yet");
    ZODAR_ENSURE(image.width > 0 && image.height > 0 && !image.levels.empty(),
                 "writeKtx2: image '" + image.name + "' is empty");
    ZODAR_ENSURE(image.levels.size() <= image.fullMipCount(), "writeKtx2: too many levels");
    ZODAR_ENSURE(!(image.colorSpace == ColorSpace::Srgb &&
                   (image.format == PixelFormat::Bc4 || image.format == PixelFormat::Bc5)),
                 "writeKtx2: BC4/BC5 have no sRGB variant");
    for (size_t i = 0; i < image.levels.size(); ++i) {
        ZODAR_ENSURE(image.levels[i].size() ==
                         pixelFormatLevelSize(image.format, image.levelWidth(i), image.levelHeight(i)),
                     "writeKtx2: level " + std::to_string(i) + " size mismatch in '" + image.name + "'");
    }
    const auto levelCount = static_cast<uint32_t>(image.levels.size());
    std::vector<uint8_t> out(kIdentifier, kIdentifier + 12);
    put32(out, ktx2VkFormat(image.format, image.colorSpace));
    put32(out, 1); // typeSize
    put32(out, image.width);
    put32(out, image.height);
    put32(out, 0); // pixelDepth
    put32(out, 0); // layerCount
    put32(out, 1); // faceCount
    put32(out, levelCount);
    put32(out, 0); // supercompressionScheme: none
    const size_t indexAt = out.size();
    out.resize(kHeaderBytes, 0);
    const size_t levelIndexAt = out.size();
    out.resize(levelIndexAt + kLevelIndexBytes * levelCount, 0);

    const std::vector<uint8_t> dfd = makeDfd(image.format, image.colorSpace);
    const size_t dfdAt = out.size();
    out.insert(out.end(), dfd.begin(), dfd.end());

    const size_t kvdAt = out.size();
    for (const auto& [key, value] : keyValues) { // std::map: sorted by key, as required.
        ZODAR_ENSURE(!key.empty() && key.find('\0') == std::string::npos, "writeKtx2: invalid key");
        put32(out, static_cast<uint32_t>(key.size() + 1 + value.size() + 1));
        out.insert(out.end(), key.begin(), key.end());
        out.push_back(0);
        out.insert(out.end(), value.begin(), value.end());
        out.push_back(0);
        pad(out, 4);
    }
    const size_t kvdLength = out.size() - kvdAt;

    set32(out, indexAt + 0, static_cast<uint32_t>(dfdAt));
    set32(out, indexAt + 4, static_cast<uint32_t>(dfd.size()));
    set32(out, indexAt + 8, kvdLength ? static_cast<uint32_t>(kvdAt) : 0);
    set32(out, indexAt + 12, static_cast<uint32_t>(kvdLength));
    // sgdByteOffset / sgdByteLength stay 0 (no supercompression global data).

    // Level data, smallest level first; each level aligned to lcm(texel block size, 4).
    const size_t alignment = blockBytes(image.format) == 8 ? 8 : blockBytes(image.format) == 16 ? 16 : 4;
    for (uint32_t i = levelCount; i-- > 0;) {
        pad(out, alignment);
        const size_t at = out.size();
        out.insert(out.end(), image.levels[i].begin(), image.levels[i].end());
        const size_t entry = levelIndexAt + kLevelIndexBytes * i;
        set64(out, entry + 0, at);
        set64(out, entry + 8, image.levels[i].size());
        set64(out, entry + 16, image.levels[i].size());
    }
    return out;
}

void saveKtx2(const std::filesystem::path& path, const ImageData& image,
              const std::map<std::string, std::string>& keyValues) {
    const std::vector<uint8_t> bytes = writeKtx2(image, keyValues);
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) fail("Cannot write '" + path.string() + "'");
}

Ktx2File readKtx2(std::span<const uint8_t> bytes, const std::string& name) {
    const Reader r(bytes, name);
    if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kIdentifier, 12) != 0) {
        r.error("not a KTX 2.0 file");
    }
    const uint32_t vkFormat = r.u32(12), typeSize = r.u32(16), width = r.u32(20), height = r.u32(24);
    const uint32_t depth = r.u32(28), layers = r.u32(32), faces = r.u32(36), levelCount = r.u32(40);
    const uint32_t supercompression = r.u32(44);
    Format format{};
    if (!fromVkFormat(vkFormat, format)) r.error("unsupported vkFormat " + std::to_string(vkFormat));
    if (typeSize != 1) r.error("unexpected typeSize " + std::to_string(typeSize));
    if (width == 0 || height == 0 || depth != 0) r.error("only 2D textures are supported");
    if (layers > 1 || faces != 1) r.error("arrays and cube maps are not supported");
    if (supercompression != 0) r.error("supercompression scheme " + std::to_string(supercompression) + " is not supported");
    if (levelCount == 0) r.error("levelCount 0 (runtime mip generation) is not supported; bake the mips");

    Ktx2File out;
    ImageData& image = out.image;
    image.name = name;
    image.width = width;
    image.height = height;
    image.format = format.format;
    image.colorSpace = format.colorSpace;
    if (levelCount > image.fullMipCount()) r.error("levelCount exceeds the full mip chain");

    // Data format descriptor: the transfer function must agree with the vkFormat.
    const uint32_t dfdAt = r.u32(48), dfdLength = r.u32(52);
    r.check(dfdAt, dfdLength);
    if (dfdLength < 28 || r.u32(dfdAt) != dfdLength) r.error("invalid data format descriptor");
    const uint32_t transfer = (r.u32(dfdAt + 12) >> 16) & 0xFF;
    if ((transfer == kTransferSrgb) != (format.colorSpace == ColorSpace::Srgb)) {
        r.error("data format descriptor transfer function does not match vkFormat");
    }

    // Key/value data.
    const uint32_t kvdAt = r.u32(56), kvdLength = r.u32(60);
    r.check(kvdAt, kvdLength);
    for (size_t at = kvdAt; at + 4 <= size_t(kvdAt) + kvdLength;) {
        const uint32_t length = r.u32(at);
        r.check(at + 4, length);
        if (at + 4 + length > size_t(kvdAt) + kvdLength) r.error("key/value entry out of range");
        const auto* entry = reinterpret_cast<const char*>(bytes.data() + at + 4);
        const auto keyLength = static_cast<size_t>(std::find(entry, entry + length, '\0') - entry);
        if (keyLength == 0 || keyLength >= length) r.error("invalid key/value entry");
        std::string value(entry + keyLength + 1, length - keyLength - 1);
        if (!value.empty() && value.back() == '\0') value.pop_back();
        out.keyValues.emplace(std::string(entry, keyLength), std::move(value));
        at += 4 + ((length + 3) & ~3u);
    }

    for (uint32_t i = 0; i < levelCount; ++i) {
        const size_t entry = kHeaderBytes + kLevelIndexBytes * i;
        const uint64_t offset = r.u64(entry), length = r.u64(entry + 8), uncompressed = r.u64(entry + 16);
        const size_t expected = pixelFormatLevelSize(image.format, image.levelWidth(i), image.levelHeight(i));
        if (length != expected || uncompressed != expected) {
            r.error("level " + std::to_string(i) + " has " + std::to_string(length) + " bytes, expected " +
                    std::to_string(expected));
        }
        r.check(offset, length);
        image.levels.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    }
    return out;
}

Ktx2File loadKtx2(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) fail("Cannot open '" + path.string() + "'");
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return readKtx2(bytes, path.string());
}

} // namespace zodar
