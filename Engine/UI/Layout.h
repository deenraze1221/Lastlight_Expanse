#pragma once

#include "UI/Geometry.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace zodar::ui {

// Flex-style layout modelled on Figma auto layout. All values are design units; callers
// multiply the resulting rectangles by the UI scale.
enum class Axis : uint8_t { Row, Column };
enum class Sizing : uint8_t { Fixed, Hug, Fill };
enum class Align : uint8_t { Start, Center, End, SpaceBetween };

struct Edges {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    static constexpr Edges all(float v) { return {v, v, v, v}; }
    static constexpr Edges xy(float x, float y) { return {x, y, x, y}; }
};

struct NodeStyle {
    Axis axis = Axis::Column;
    Sizing widthSizing = Sizing::Hug;
    Sizing heightSizing = Sizing::Hug;
    Size2 size;           // Fixed size, or intrinsic content size of a leaf.
    Size2 minSize;
    Size2 maxSize{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    Edges padding;
    float gap = 0.0f;
    Align mainAlign = Align::Start;   // Along `axis`.
    Align crossAlign = Align::Start;  // Across `axis` (SpaceBetween acts as Start).
    bool wrap = false;                // Rows/columns wrap onto new lines.
    float lineGap = 0.0f;             // Gap between wrapped lines.
};

using NodeId = uint32_t;
inline constexpr NodeId kNoNode = UINT32_MAX;

class LayoutTree {
public:
    NodeId add(NodeId parent, const NodeStyle& style);
    NodeStyle& style(NodeId id) { return m_nodes[id].style; }
    const NodeStyle& style(NodeId id) const { return m_nodes[id].style; }

    // Lays out every root at `origin` within `available` space.
    void compute(Size2 available, Vec2 origin = {});
    Rect rect(NodeId id) const { return m_nodes[id].rect; }
    size_t size() const { return m_nodes.size(); }
    void clear() { m_nodes.clear(); }

private:
    struct Node {
        NodeStyle style;
        NodeId parent = kNoNode;
        std::vector<NodeId> children;
        Size2 measured;
        Rect rect;
    };

    Size2 measure(NodeId id);
    void arrange(NodeId id, Rect rect);
    float mainOf(Size2 s, Axis a) const { return a == Axis::Row ? s.width : s.height; }
    float crossOf(Size2 s, Axis a) const { return a == Axis::Row ? s.height : s.width; }

    std::vector<Node> m_nodes;
};

} // namespace zodar::ui
