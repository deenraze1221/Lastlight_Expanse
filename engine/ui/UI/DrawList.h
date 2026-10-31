#pragma once

#include "UI/Geometry.h"
#include "UI/Theme.h"
#include "UI/TextureStore.h"

#include <cstdint>
#include <vector>

namespace zodar::ui {

// One vertex in physical pixels. `color` is sRGB with straight alpha, packed as
// 0xAABBGGRR (bytes R, G, B, A in memory). Texture coordinates are normalized.
struct DrawVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    uint32_t color = 0;
};

// A run of triangles sharing one texture and clip rectangle (physical pixels).
struct DrawCommand {
    TextureId texture = kNoTexture;
    Rect clip;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

uint32_t packColor(Color c);
Color unpackColor(uint32_t packed);

// Renderer-independent output of the UI: indexed triangles grouped into commands.
// Colours are sRGB; a backend writing to an sRGB-encoding target must linearize them
// (the swapchain is B8G8R8A8_SRGB, see ARCHITECTURE.md), blending is straight alpha.
class DrawList {
public:
    explicit DrawList(Rect viewport = {0, 0, 1e6f, 1e6f});

    void reset(Rect viewport);

    void pushClip(Rect clip); // Intersected with the current clip.
    void popClip();
    Rect currentClip() const { return m_clips.back(); }

    void fillRect(Rect r, Color c);
    // Rounded rectangle; `radius` in pixels. Corners use a few segments (radius is small).
    void fillRoundRect(Rect r, float radius, Color c);
    // Border drawn inside `r` (outer edge = r).
    void strokeRect(Rect r, float width, Color c);
    void strokeRoundRect(Rect r, float radius, float width, Color c);
    void fillCircle(Vec2 center, float radius, Color c, int segments = 24);
    void image(Rect r, TextureId texture, Color tint = Color::hex(0xFFFFFF), Rect uv = {0, 0, 1, 1});

    const std::vector<DrawVertex>& vertices() const { return m_vertices; }
    const std::vector<uint32_t>& indices() const { return m_indices; }
    const std::vector<DrawCommand>& commands() const { return m_commands; }

private:
    void beginBatch(TextureId texture);
    uint32_t addVertex(float x, float y, float u, float v, uint32_t color);
    void addQuad(Rect r, Rect uv, uint32_t color);
    void addConvexFan(const std::vector<Vec2>& points, uint32_t color);
    static void roundRectPoints(Rect r, float radius, std::vector<Vec2>& out);

    std::vector<DrawVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<DrawCommand> m_commands;
    std::vector<Rect> m_clips;
};

} // namespace zodar::ui
