#pragma once

#include <algorithm>
#include <cmath>

namespace zodar::ui {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

struct Size2 {
    float width = 0.0f;
    float height = 0.0f;
    friend constexpr bool operator==(const Size2&, const Size2&) = default;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr float right() const { return x + width; }
    constexpr float bottom() const { return y + height; }
    constexpr bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < right() && p.y < bottom(); }
    constexpr bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }
    constexpr Rect scaled(float s) const { return {x * s, y * s, width * s, height * s}; }
    constexpr Rect inset(float d) const { return {x + d, y + d, width - 2.0f * d, height - 2.0f * d}; }
    Rect intersect(const Rect& o) const {
        const float l = std::max(x, o.x);
        const float t = std::max(y, o.y);
        const float r = std::min(right(), o.right());
        const float b = std::min(bottom(), o.bottom());
        return {l, t, std::max(0.0f, r - l), std::max(0.0f, b - t)};
    }
    // Snaps edges to whole pixels (crisp 1 px borders at any scale).
    Rect snapped() const {
        const float l = std::round(x);
        const float t = std::round(y);
        return {l, t, std::round(right()) - l, std::round(bottom()) - t};
    }
    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

} // namespace zodar::ui
