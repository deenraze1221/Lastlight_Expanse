#pragma once

#include "UI/DrawList.h"
#include "UI/TextureStore.h"

#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace zodar::sandbox {

// TEMPORARY backend: uploads TextureStore images and draws a ui::DrawList with
// SDL_RenderGeometryRaw. Replaced by the Vulkan UI render-graph pass in a later stage.
class SdlRendererBackend {
public:
    explicit SdlRendererBackend(SDL_Renderer* renderer) : m_renderer(renderer) {}
    ~SdlRendererBackend();

    SdlRendererBackend(const SdlRendererBackend&) = delete;
    SdlRendererBackend& operator=(const SdlRendererBackend&) = delete;

    void sync(const ui::TextureStore& store);
    void render(const ui::DrawList& list);

private:
    struct Slot {
        SDL_Texture* texture = nullptr;
        uint64_t version = 0;
    };
    SDL_Renderer* m_renderer;
    std::vector<Slot> m_slots; // Index = TextureId - 1.
};

} // namespace zodar::sandbox
