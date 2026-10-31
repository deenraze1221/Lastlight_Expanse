#include "UI/Font.h"

#include "Core/Error.h"
#include "Core/Log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace zodar::ui {

struct FontLibrary::FaceData {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
};

namespace {

std::vector<unsigned char> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fail("UI font not found: " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Characters baked into every atlas: printable ASCII, Latin-1 and common punctuation
// used by the designs (middle dot, dashes, curly quotes, ellipsis, bullet, check mark).
const std::vector<int>& extraCodepoints() {
    static const std::vector<int> cps{0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2713, 0xFFFD};
    return cps;
}

float pixelKey(float px) {
    return std::round(px * 4.0f) / 4.0f;
}

} // namespace

std::vector<uint32_t> decodeUtf8(std::string_view s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    // A sequence is accepted only if every continuation byte is 10xxxxxx and the result is
    // not overlong, a surrogate or beyond U+10FFFF; otherwise one byte becomes U+FFFD.
    auto cont = [&](size_t at) { return at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0u) == 0x80u; };
    auto bits = [&](size_t at) { return static_cast<uint32_t>(static_cast<unsigned char>(s[at]) & 0x3Fu); };
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && cont(i + 1)) {
            const uint32_t v = ((c & 0x1Fu) << 6) | bits(i + 1);
            if (v >= 0x80) { cp = v; len = 2; }
        } else if ((c >> 4) == 0xE && cont(i + 1) && cont(i + 2)) {
            const uint32_t v = ((c & 0x0Fu) << 12) | (bits(i + 1) << 6) | bits(i + 2);
            if (v >= 0x800 && (v < 0xD800 || v > 0xDFFF)) { cp = v; len = 3; }
        } else if ((c >> 3) == 0x1E && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
            const uint32_t v = ((c & 0x07u) << 18) | (bits(i + 1) << 12) | (bits(i + 2) << 6) | bits(i + 3);
            if (v >= 0x10000 && v <= 0x10FFFF) { cp = v; len = 4; }
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

FontLibrary::FontLibrary(const std::filesystem::path& resourceDir, TextureStore& textures) : m_textures(textures) {
    for (size_t i = 0; i < kFontFaceCount; ++i) {
        const auto face = static_cast<FontFace>(i);
        auto data = std::make_unique<FaceData>();
        data->bytes = readFile(resourceDir / std::filesystem::path(std::string(fontFile(face))));
        if (!stbtt_InitFont(&data->info, data->bytes.data(), stbtt_GetFontOffsetForIndex(data->bytes.data(), 0))) {
            fail("UI font could not be parsed: " + std::string(fontFile(face)));
        }
        m_faces[i] = std::move(data);
    }
}

FontLibrary::~FontLibrary() = default;

FontLibrary::Atlas& FontLibrary::atlas(FontFace face, float pixelSize) {
    const float px = pixelKey(pixelSize);
    for (auto& a : m_atlases) {
        if (a->face == face && a->pixelSize == px) {
            return *a;
        }
    }
    FaceData& fd = *m_faces[static_cast<size_t>(face)];
    const float emScale = stbtt_ScaleForMappingEmToPixels(&fd.info, px);

    stbtt_pack_range ranges[3]{};
    std::vector<stbtt_packedchar> ascii(95), latin(96), extra(extraCodepoints().size());
    ranges[0] = {STBTT_POINT_SIZE(px), 32, nullptr, 95, ascii.data(), 0, 0};
    ranges[1] = {STBTT_POINT_SIZE(px), 160, nullptr, 96, latin.data(), 0, 0};
    ranges[2] = {STBTT_POINT_SIZE(px), 0, const_cast<int*>(extraCodepoints().data()),
                 static_cast<int>(extraCodepoints().size()), extra.data(), 0, 0};

    int size = 256;
    std::vector<unsigned char> alpha;
    for (;; size *= 2) {
        ZODAR_ENSURE(size <= 4096, "glyph atlas too large");
        alpha.assign(static_cast<size_t>(size) * size, 0);
        stbtt_pack_context ctx;
        if (!stbtt_PackBegin(&ctx, alpha.data(), size, size, 0, 1, nullptr)) {
            fail("stbtt_PackBegin failed");
        }
        stbtt_PackSetOversampling(&ctx, 1, 1);
        const int ok = stbtt_PackFontRanges(&ctx, fd.bytes.data(), 0, ranges, 3);
        stbtt_PackEnd(&ctx);
        if (ok) {
            break;
        }
    }

    Image image{static_cast<uint32_t>(size), static_cast<uint32_t>(size), {}};
    image.pixels.resize(alpha.size() * 4);
    for (size_t i = 0; i < alpha.size(); ++i) {
        image.pixels[i * 4 + 0] = 255;
        image.pixels[i * 4 + 1] = 255;
        image.pixels[i * 4 + 2] = 255;
        image.pixels[i * 4 + 3] = alpha[i];
    }

    auto a = std::make_unique<Atlas>();
    a->face = face;
    a->pixelSize = px;
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&fd.info, &asc, &desc, &gap);
    a->ascent = static_cast<float>(asc) * emScale;
    a->descent = static_cast<float>(-desc) * emScale;
    a->kernScale = emScale;
    const float inv = 1.0f / static_cast<float>(size);
    auto addRange = [&](const std::vector<stbtt_packedchar>& chars, int first, const int* list) {
        for (size_t i = 0; i < chars.size(); ++i) {
            const stbtt_packedchar& pc = chars[i];
            const uint32_t cp = static_cast<uint32_t>(list ? list[i] : first + static_cast<int>(i));
            a->glyphs[cp] = {pc.xoff, pc.yoff, pc.xoff2, pc.yoff2,
                             pc.x0 * inv, pc.y0 * inv, pc.x1 * inv, pc.y1 * inv, pc.xadvance};
        }
    };
    addRange(ascii, 32, nullptr);
    addRange(latin, 160, nullptr);
    addRange(extra, 0, extraCodepoints().data());
    a->texture = m_textures.add(std::move(image));
    ZODAR_LOG_DEBUG("UI glyph atlas: {} {}px ({}x{})", fontFaceName(face), px, size, size);
    m_atlases.push_back(std::move(a));
    return *m_atlases.back();
}

template <typename Fn>
float FontLibrary::layout(const TextStyle& style, float scale, std::string_view text, Fn&& emit) {
    Atlas& a = atlas(style.face, style.size * scale);
    const FaceData& fd = *m_faces[static_cast<size_t>(style.face)];
    std::vector<uint32_t> cps = decodeUtf8(text);
    if (style.uppercase) {
        for (uint32_t& cp : cps) {
            if (cp >= 'a' && cp <= 'z') cp -= 32;
        }
    }
    const float spacing = style.letterSpacing * a.pixelSize;
    float pen = 0.0f;
    for (size_t i = 0; i < cps.size(); ++i) {
        auto it = a.glyphs.find(cps[i]);
        if (it == a.glyphs.end()) {
            it = a.glyphs.find(0xFFFD);
        }
        const Glyph& g = it->second;
        emit(a, g, std::round(pen));
        pen += g.advance;
        if (i + 1 < cps.size()) {
            pen += spacing;
            pen += static_cast<float>(stbtt_GetCodepointKernAdvance(&fd.info, static_cast<int>(cps[i]),
                                                                    static_cast<int>(cps[i + 1]))) * a.kernScale;
        }
    }
    return std::ceil(pen);
}

Size2 FontLibrary::measure(const TextStyle& style, float scale, std::string_view text) {
    const float width = layout(style, scale, text, [](const Atlas&, const Glyph&, float) {});
    return {width, std::round(style.size * style.lineHeight * scale)};
}

float FontLibrary::ascent(const TextStyle& style, float scale) {
    return atlas(style.face, style.size * scale).ascent;
}

float FontLibrary::descent(const TextStyle& style, float scale) {
    return atlas(style.face, style.size * scale).descent;
}

float FontLibrary::draw(DrawList& list, Vec2 topLeft, const TextStyle& style, float scale, Color color,
                        std::string_view text) {
    const float lineHeight = std::round(style.size * style.lineHeight * scale);
    Atlas& a = atlas(style.face, style.size * scale);
    // Centre the face's ascent+descent inside the line box; baseline on a whole pixel.
    const float baseline = std::round(topLeft.y + (lineHeight - (a.ascent + a.descent)) * 0.5f + a.ascent);
    const float originX = std::round(topLeft.x);
    return layout(style, scale, text, [&](const Atlas& at, const Glyph& g, float penX) {
        if (g.x1 <= g.x0) {
            return; // Space.
        }
        const Rect r{originX + penX + g.x0, baseline + g.y0, g.x1 - g.x0, g.y1 - g.y0};
        list.image(r, at.texture, color, {g.u0, g.v0, g.u1 - g.u0, g.v1 - g.v0});
    });
}

} // namespace zodar::ui
