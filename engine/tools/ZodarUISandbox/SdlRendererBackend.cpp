#include "SdlRendererBackend.h"

#include "Core/Error.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace zodar::sandbox {

SdlRendererBackend::~SdlRendererBackend() {
    for (Slot& s : m_slots) {
        if (s.texture) SDL_DestroyTexture(s.texture);
    }
}

void SdlRendererBackend::sync(const ui::TextureStore& store) {
    m_slots.resize(store.maxId());
    for (ui::TextureId id = 1; id <= store.maxId(); ++id) {
        const ui::TextureStore::Entry* e = store.find(id);
        Slot& slot = m_slots[id - 1];
        if (!e || e->version == slot.version) {
            continue;
        }
        if (slot.texture) {
            SDL_DestroyTexture(slot.texture);
            slot.texture = nullptr;
        }
        slot.version = e->version;
        if (!e->alive || e->image.width == 0) {
            continue;
        }
        slot.texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                         static_cast<int>(e->image.width), static_cast<int>(e->image.height));
        if (!slot.texture) {
            fail(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        }
        SDL_UpdateTexture(slot.texture, nullptr, e->image.pixels.data(), static_cast<int>(e->image.width * 4));
        SDL_SetTextureBlendMode(slot.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(slot.texture, SDL_SCALEMODE_LINEAR);
    }
}

void SdlRendererBackend::render(const ui::DrawList& list) {
    const auto& verts = list.vertices();
    if (verts.empty()) {
        return;
    }
    std::vector<float> xy(verts.size() * 2);
    std::vector<float> uv(verts.size() * 2);
    std::vector<SDL_FColor> colors(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        xy[i * 2] = verts[i].x;
        xy[i * 2 + 1] = verts[i].y;
        uv[i * 2] = verts[i].u;
        uv[i * 2 + 1] = verts[i].v;
        const ui::Color c = ui::unpackColor(verts[i].color);
        colors[i] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    }
    for (const ui::DrawCommand& cmd : list.commands()) {
        if (cmd.indexCount == 0) continue;
        const SDL_Rect clip{static_cast<int>(std::floor(cmd.clip.x)), static_cast<int>(std::floor(cmd.clip.y)),
                            static_cast<int>(std::ceil(cmd.clip.width)), static_cast<int>(std::ceil(cmd.clip.height))};
        SDL_SetRenderClipRect(m_renderer, &clip);
        SDL_Texture* tex = nullptr;
        if (cmd.texture != ui::kNoTexture && cmd.texture <= m_slots.size()) {
            tex = m_slots[cmd.texture - 1].texture;
        }
        SDL_RenderGeometryRaw(m_renderer, tex, xy.data(), sizeof(float) * 2, colors.data(), sizeof(SDL_FColor),
                              uv.data(), sizeof(float) * 2, static_cast<int>(verts.size()),
                              list.indices().data() + cmd.indexOffset, static_cast<int>(cmd.indexCount),
                              sizeof(uint32_t));
    }
    SDL_SetRenderClipRect(m_renderer, nullptr);
}

} // namespace zodar::sandbox
