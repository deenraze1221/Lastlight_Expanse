#include "UI/Layout.h"

#include "Core/Error.h"

#include <algorithm>

namespace zodar::ui {
namespace {

float clampAxis(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

Size2 clampSize(Size2 s, const NodeStyle& st) {
    return {clampAxis(s.width, st.minSize.width, st.maxSize.width),
            clampAxis(s.height, st.minSize.height, st.maxSize.height)};
}

Sizing mainSizing(const NodeStyle& child, Axis parentAxis) {
    return parentAxis == Axis::Row ? child.widthSizing : child.heightSizing;
}
Sizing crossSizing(const NodeStyle& child, Axis parentAxis) {
    return parentAxis == Axis::Row ? child.heightSizing : child.widthSizing;
}

} // namespace

NodeId LayoutTree::add(NodeId parent, const NodeStyle& style) {
    const NodeId id = static_cast<NodeId>(m_nodes.size());
    m_nodes.push_back({style, parent, {}, {}, {}});
    if (parent != kNoNode) {
        ZODAR_ENSURE(parent < id, "layout parent must exist");
        m_nodes[parent].children.push_back(id);
    }
    return id;
}

Size2 LayoutTree::measure(NodeId id) {
    Node& n = m_nodes[id];
    const NodeStyle& st = n.style;
    const Axis axis = st.axis;
    float main = 0.0f;
    float cross = 0.0f;
    size_t counted = 0;
    for (NodeId c : n.children) {
        const Size2 cs = measure(c);
        // Fill children contribute their minimum to the hug size.
        const NodeStyle& cst = m_nodes[c].style;
        const float cm = mainSizing(cst, axis) == Sizing::Fill ? mainOf(cst.minSize, axis) : mainOf(cs, axis);
        main += cm;
        cross = std::max(cross, crossOf(cs, axis));
        ++counted;
    }
    if (counted > 1) {
        main += st.gap * static_cast<float>(counted - 1);
    }
    // A wrapping container with a fixed main size knows where its lines break, so its hug
    // cross size is the sum of the wrapped lines (same breaking rule as arrange()).
    const bool fixedMain = axis == Axis::Row ? st.widthSizing == Sizing::Fixed : st.heightSizing == Sizing::Fixed;
    if (st.wrap && fixedMain && !n.children.empty()) {
        const float avail = axis == Axis::Row ? st.size.width - st.padding.left - st.padding.right
                                              : st.size.height - st.padding.top - st.padding.bottom;
        float cursor = 0.0f;
        float lineCross = 0.0f;
        float totalCross = 0.0f;
        bool lineEmpty = true;
        for (NodeId c : n.children) {
            const float m = mainOf(m_nodes[c].measured, axis);
            if (!lineEmpty && cursor + st.gap + m > avail + 0.01f) {
                totalCross += lineCross + st.lineGap;
                cursor = 0.0f;
                lineCross = 0.0f;
                lineEmpty = true;
            }
            cursor += (lineEmpty ? 0.0f : st.gap) + m;
            lineCross = std::max(lineCross, crossOf(m_nodes[c].measured, axis));
            lineEmpty = false;
        }
        cross = totalCross + lineCross;
    }
    Size2 content = axis == Axis::Row ? Size2{main, cross} : Size2{cross, main};
    content.width += st.padding.left + st.padding.right;
    content.height += st.padding.top + st.padding.bottom;

    Size2 result = content;
    if (n.children.empty()) {
        result = {st.size.width + st.padding.left + st.padding.right, st.size.height + st.padding.top + st.padding.bottom};
    }
    if (st.widthSizing == Sizing::Fixed) result.width = st.size.width;
    if (st.heightSizing == Sizing::Fixed) result.height = st.size.height;
    n.measured = clampSize(result, st);
    return n.measured;
}

void LayoutTree::arrange(NodeId id, Rect rect) {
    Node& n = m_nodes[id];
    n.rect = rect;
    const NodeStyle& st = n.style;
    const Axis axis = st.axis;
    const Rect content{rect.x + st.padding.left, rect.y + st.padding.top,
                       std::max(0.0f, rect.width - st.padding.left - st.padding.right),
                       std::max(0.0f, rect.height - st.padding.top - st.padding.bottom)};
    const float contentMain = axis == Axis::Row ? content.width : content.height;
    const float contentCross = axis == Axis::Row ? content.height : content.width;

    // Split children into lines (one line unless wrapping).
    std::vector<std::vector<NodeId>> lines(1);
    if (st.wrap) {
        float cursor = 0.0f;
        for (NodeId c : n.children) {
            const float m = mainOf(m_nodes[c].measured, axis);
            if (!lines.back().empty() && cursor + st.gap + m > contentMain + 0.01f) {
                lines.emplace_back();
                cursor = 0.0f;
            }
            cursor += (lines.back().empty() ? 0.0f : st.gap) + m;
            lines.back().push_back(c);
        }
    } else {
        lines[0] = n.children;
    }

    float lineOffset = 0.0f;
    for (const std::vector<NodeId>& line : lines) {
        // Main sizes: fixed/hug use measured size, fill children share the remainder.
        std::vector<float> mainSize(line.size());
        float used = line.size() > 1 ? st.gap * static_cast<float>(line.size() - 1) : 0.0f;
        std::vector<size_t> fills;
        for (size_t i = 0; i < line.size(); ++i) {
            const NodeStyle& cst = m_nodes[line[i]].style;
            if (!st.wrap && mainSizing(cst, axis) == Sizing::Fill) {
                fills.push_back(i);
                mainSize[i] = 0.0f;
            } else {
                mainSize[i] = mainOf(m_nodes[line[i]].measured, axis);
                used += mainSize[i];
            }
        }
        // Distribute, honouring min/max; re-run when a child hits a limit.
        float remaining = std::max(0.0f, contentMain - used);
        std::vector<size_t> open = fills;
        while (!open.empty()) {
            const float share = remaining / static_cast<float>(open.size());
            std::vector<size_t> next;
            float consumed = 0.0f;
            for (size_t i : open) {
                const NodeStyle& cst = m_nodes[line[i]].style;
                const float lo = mainOf(cst.minSize, axis);
                const float hi = mainOf(cst.maxSize, axis);
                const float v = clampAxis(share, lo, hi);
                if (v != share) {
                    mainSize[i] = v;
                    consumed += v;
                } else {
                    next.push_back(i);
                }
            }
            if (next.size() == open.size()) {
                for (size_t i : open) mainSize[i] = share;
                break;
            }
            remaining = std::max(0.0f, remaining - consumed);
            open = std::move(next);
        }

        float total = line.size() > 1 ? st.gap * static_cast<float>(line.size() - 1) : 0.0f;
        float lineCross = 0.0f;
        for (size_t i = 0; i < line.size(); ++i) {
            total += mainSize[i];
            lineCross = std::max(lineCross, crossOf(m_nodes[line[i]].measured, axis));
        }
        if (!st.wrap) {
            lineCross = contentCross;
        }

        float cursor = 0.0f;
        float gap = st.gap;
        const float free = contentMain - total;
        if (st.mainAlign == Align::Center) cursor = free * 0.5f;
        if (st.mainAlign == Align::End) cursor = free;
        if (st.mainAlign == Align::SpaceBetween && line.size() > 1 && free > 0.0f) {
            gap += free / static_cast<float>(line.size() - 1);
        }

        for (size_t i = 0; i < line.size(); ++i) {
            const Node& child = m_nodes[line[i]];
            const NodeStyle& cst = child.style;
            float cs = crossSizing(cst, axis) == Sizing::Fill ? lineCross : crossOf(child.measured, axis);
            cs = clampAxis(cs, crossOf(cst.minSize, axis), crossOf(cst.maxSize, axis));
            float co = 0.0f;
            if (st.crossAlign == Align::Center) co = (lineCross - cs) * 0.5f;
            if (st.crossAlign == Align::End) co = lineCross - cs;
            const float m = cursor;
            const float c = lineOffset + co;
            const Rect r = axis == Axis::Row ? Rect{content.x + m, content.y + c, mainSize[i], cs}
                                             : Rect{content.x + c, content.y + m, cs, mainSize[i]};
            arrange(line[i], r);
            cursor += mainSize[i] + gap;
        }
        lineOffset += lineCross + st.lineGap;
    }
}

void LayoutTree::compute(Size2 available, Vec2 origin) {
    for (NodeId id = 0; id < m_nodes.size(); ++id) {
        if (m_nodes[id].parent != kNoNode) {
            continue;
        }
        Size2 s = measure(id);
        const NodeStyle& st = m_nodes[id].style;
        if (st.widthSizing == Sizing::Fill) s.width = available.width;
        if (st.heightSizing == Sizing::Fill) s.height = available.height;
        s = clampSize(s, st);
        // Known limitation (Stage 1): a wrapping container measures its wrapped height only
        // when its main size is Fixed; with Hug/Fill it is measured as a single line.
        arrange(id, {origin.x, origin.y, s.width, s.height});
    }
}

} // namespace zodar::ui
