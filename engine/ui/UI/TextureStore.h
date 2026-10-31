#pragma once

#include <cstdint>
#include <vector>

namespace zodar::ui {

// CPU image, RGBA8, sRGB, straight alpha, tightly packed rows.
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

// 0 is "no texture" (solid colour geometry).
using TextureId = uint32_t;
inline constexpr TextureId kNoTexture = 0;

// Images the UI needs on the GPU (font atlases, rasterized SVGs). The UI only creates
// CPU images here; a backend uploads entries whose version changed since its last sync.
class TextureStore {
public:
    struct Entry {
        Image image;
        uint64_t version = 0;
        bool alive = false;
    };

    TextureId add(Image image);
    void update(TextureId id, Image image);
    void remove(TextureId id);

    const Entry* find(TextureId id) const;
    // Highest id ever handed out; backends iterate 1..maxId().
    TextureId maxId() const { return static_cast<TextureId>(m_entries.size()); }

private:
    std::vector<Entry> m_entries; // Index = id - 1.
};

} // namespace zodar::ui
