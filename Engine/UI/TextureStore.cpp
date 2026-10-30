#include "UI/TextureStore.h"

#include "Core/Error.h"

namespace zodar::ui {

TextureId TextureStore::add(Image image) {
    ZODAR_ENSURE(image.pixels.size() == size_t{image.width} * image.height * 4, "image size mismatch");
    m_entries.push_back({std::move(image), 1, true});
    return static_cast<TextureId>(m_entries.size());
}

void TextureStore::update(TextureId id, Image image) {
    ZODAR_ENSURE(id != kNoTexture && id <= m_entries.size() && m_entries[id - 1].alive, "unknown texture");
    ZODAR_ENSURE(image.pixels.size() == size_t{image.width} * image.height * 4, "image size mismatch");
    Entry& e = m_entries[id - 1];
    e.image = std::move(image);
    ++e.version;
}

void TextureStore::remove(TextureId id) {
    if (id == kNoTexture || id > m_entries.size()) {
        return;
    }
    Entry& e = m_entries[id - 1];
    e.alive = false;
    e.image = {};
    ++e.version;
}

const TextureStore::Entry* TextureStore::find(TextureId id) const {
    if (id == kNoTexture || id > m_entries.size()) {
        return nullptr;
    }
    return &m_entries[id - 1];
}

} // namespace zodar::ui
