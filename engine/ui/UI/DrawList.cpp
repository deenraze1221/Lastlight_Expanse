#include "UI/DrawList.h"

#include "Core/Error.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace zodar::ui {

uint32_t packColor(Color c) {
    return uint32_t{c.r} | (uint32_t{c.g} << 8) | (uint32_t{c.b} << 16) | (uint32_t{c.a} << 24);
}

Color unpackColor(uint32_t p) {
    return {static_cast<uint8_t>(p & 0xFF), static_cast<uint8_t>((p >> 8) & 0xFF),
            static_cast<uint8_t>((p >> 16) & 0xFF), static_cast<uint8_t>((p >> 24) & 0xFF)};
}

DrawList::DrawList(Rect viewport) {
    reset(viewport);
}

void DrawList::reset(Rect viewport) {
    m_vertices.clear();
    m_indices.clear();
    m_commands.clear();
    m_clips.assign(1, viewport);
}

void DrawList::pushClip(Rect clip) {
    m_clips.push_back(m_clips.back().intersect(clip));
}

void DrawList::popClip() {
    ZODAR_ENSURE(m_clips.size() > 1, "popClip without pushClip");
    m_clips.pop_back();
}

void DrawList::beginBatch(TextureId texture) {
    const Rect clip = m_clips.back();
    if (!m_commands.empty()) {
        DrawCommand& last = m_commands.back();
        if (last.texture == texture && last.clip == clip) {
            return;
        }
        if (last.indexCount == 0) {
            last.texture = texture;
            last.clip = clip;
            return;
        }
    }
    m_commands.push_back({texture, clip, static_cast<uint32_t>(m_indices.size()), 0});
}

uint32_t DrawList::addVertex(float x, float y, float u, float v, uint32_t color) {
    m_vertices.push_back({x, y, u, v, color});
    return static_cast<uint32_t>(m_vertices.size() - 1);
}

void DrawList::addQuad(Rect r, Rect uv, uint32_t color) {
    const uint32_t a = addVertex(r.x, r.y, uv.x, uv.y, color);
    const uint32_t b = addVertex(r.right(), r.y, uv.right(), uv.y, color);
    const uint32_t c = addVertex(r.right(), r.bottom(), uv.right(), uv.bottom(), color);
    const uint32_t d = addVertex(r.x, r.bottom(), uv.x, uv.bottom(), color);
    m_indices.insert(m_indices.end(), {a, b, c, a, c, d});
    m_commands.back().indexCount += 6;
}

void DrawList::addConvexFan(const std::vector<Vec2>& points, uint32_t color) {
    if (points.size() < 3) {
        return;
    }
    const uint32_t first = static_cast<uint32_t>(m_vertices.size());
    for (const Vec2& p : points) {
        addVertex(p.x, p.y, 0.0f, 0.0f, color);
    }
    for (uint32_t i = 1; i + 1 < points.size(); ++i) {
        m_indices.insert(m_indices.end(), {first, first + i, first + i + 1});
        m_commands.back().indexCount += 3;
    }
}

void DrawList::fillRect(Rect r, Color c) {
    if (r.isEmpty() || c.a == 0) {
        return;
    }
    beginBatch(kNoTexture);
    addQuad(r, {}, packColor(c));
}

void DrawList::roundRectPoints(Rect r, float radius, std::vector<Vec2>& out) {
    constexpr int kSegments = 4;
    const float rad = std::min({radius, r.width * 0.5f, r.height * 0.5f});
    const Vec2 centers[4] = {{r.right() - rad, r.y + rad}, {r.right() - rad, r.bottom() - rad},
                             {r.x + rad, r.bottom() - rad}, {r.x + rad, r.y + rad}};
    const float start[4] = {-0.5f, 0.0f, 0.5f, 1.0f}; // Quarter turns, clockwise from top-right.
    out.clear();
    for (int corner = 0; corner < 4; ++corner) {
        for (int s = 0; s <= kSegments; ++s) {
            const float a = (start[corner] + 0.5f * static_cast<float>(s) / kSegments) * std::numbers::pi_v<float>;
            out.push_back({centers[corner].x + std::cos(a) * rad, centers[corner].y + std::sin(a) * rad});
        }
    }
}

void DrawList::fillRoundRect(Rect r, float radius, Color c) {
    if (r.isEmpty() || c.a == 0) {
        return;
    }
    if (radius < 0.5f) {
        fillRect(r, c);
        return;
    }
    beginBatch(kNoTexture);
    std::vector<Vec2> pts;
    roundRectPoints(r, radius, pts);
    addConvexFan(pts, packColor(c));
}

void DrawList::strokeRect(Rect r, float w, Color c) {
    if (r.isEmpty() || c.a == 0 || w <= 0.0f) {
        return;
    }
    if (2.0f * w >= r.width || 2.0f * w >= r.height) {
        fillRect(r, c); // Border thicker than the box: solid.
        return;
    }
    fillRect({r.x, r.y, r.width, w}, c);
    fillRect({r.x, r.bottom() - w, r.width, w}, c);
    fillRect({r.x, r.y + w, w, r.height - 2.0f * w}, c);
    fillRect({r.right() - w, r.y + w, w, r.height - 2.0f * w}, c);
}

void DrawList::strokeRoundRect(Rect r, float radius, float w, Color c) {
    if (r.isEmpty() || c.a == 0 || w <= 0.0f) {
        return;
    }
    if (radius < 0.5f) {
        strokeRect(r, w, c);
        return;
    }
    // Ring between the outer and inner outlines (same point count, joined as quads).
    std::vector<Vec2> outer;
    std::vector<Vec2> inner;
    roundRectPoints(r, radius, outer);
    roundRectPoints(r.inset(w), std::max(0.0f, radius - w), inner);
    beginBatch(kNoTexture);
    const uint32_t color = packColor(c);
    const uint32_t base = static_cast<uint32_t>(m_vertices.size());
    for (const Vec2& p : outer) addVertex(p.x, p.y, 0, 0, color);
    for (const Vec2& p : inner) addVertex(p.x, p.y, 0, 0, color);
    const uint32_t n = static_cast<uint32_t>(outer.size());
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        m_indices.insert(m_indices.end(), {base + i, base + j, base + n + j, base + i, base + n + j, base + n + i});
        m_commands.back().indexCount += 6;
    }
}

void DrawList::fillCircle(Vec2 center, float radius, Color c, int segments) {
    if (radius <= 0.0f || c.a == 0) {
        return;
    }
    beginBatch(kNoTexture);
    std::vector<Vec2> pts;
    for (int i = 0; i < segments; ++i) {
        const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
        pts.push_back({center.x + std::cos(a) * radius, center.y + std::sin(a) * radius});
    }
    addConvexFan(pts, packColor(c));
}

void DrawList::image(Rect r, TextureId texture, Color tint, Rect uv) {
    if (r.isEmpty() || tint.a == 0) {
        return;
    }
    beginBatch(texture);
    addQuad(r, uv, packColor(tint));
}

} // namespace zodar::ui
