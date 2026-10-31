#pragma once

#include "UI/DrawList.h"
#include "UI/Theme.h"
#include "UI/TextureStore.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zodar::ui {

// The four approved faces, baked into glyph atlases on demand for each pixel size actually
// used (any UI scale). Glyphs are rasterized at the exact pixel size and placed on whole
// pixels, so text stays sharp without a distance-field shader.
class FontLibrary {
public:
    // `resourceDir` is the UI resource directory (contains Fonts/...). Fails loudly if a
    // face is missing.
    FontLibrary(const std::filesystem::path& resourceDir, TextureStore& textures);
    ~FontLibrary();

    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    // Size of one line of `text` in physical pixels (width; height = line height).
    Size2 measure(const TextStyle& style, float scale, std::string_view text);
    // Draws one line; `topLeft` is the top of the line box in physical pixels. Returns the width.
    float draw(DrawList& list, Vec2 topLeft, const TextStyle& style, float scale, Color color, std::string_view text);

    size_t atlasCount() const { return m_atlases.size(); }
    // Vertical metrics (pixels) for a style at a scale: ascent above and descent below the baseline.
    float ascent(const TextStyle& style, float scale);
    float descent(const TextStyle& style, float scale);

private:
    struct Glyph {
        float x0, y0, x1, y1; // Quad relative to the pen on the baseline.
        float u0, v0, u1, v1;
        float advance;
    };
    struct Atlas {
        FontFace face;
        float pixelSize;
        float ascent;
        float descent;
        float kernScale;
        TextureId texture;
        std::unordered_map<uint32_t, Glyph> glyphs;
    };
    struct FaceData;

    Atlas& atlas(FontFace face, float pixelSize);
    template <typename Fn> float layout(const TextStyle& style, float scale, std::string_view text, Fn&& emit);

    TextureStore& m_textures;
    std::array<std::unique_ptr<FaceData>, kFontFaceCount> m_faces;
    std::vector<std::unique_ptr<Atlas>> m_atlases;
};

// UTF-8 decoding helper (invalid bytes become U+FFFD). Exposed for tests.
std::vector<uint32_t> decodeUtf8(std::string_view text);

} // namespace zodar::ui
