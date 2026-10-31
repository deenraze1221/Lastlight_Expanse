#include <doctest/doctest.h>

#include "UI/Layout.h"

using namespace zodar::ui;

namespace {
NodeStyle leaf(float w, float h) {
    NodeStyle s;
    s.widthSizing = Sizing::Fixed;
    s.heightSizing = Sizing::Fixed;
    s.size = {w, h};
    return s;
}
} // namespace

TEST_CASE("ui layout: hug row with gap and padding") {
    LayoutTree t;
    NodeStyle row;
    row.axis = Axis::Row;
    row.gap = 10;
    row.padding = Edges::xy(20, 8);
    const NodeId r = t.add(kNoNode, row);
    const NodeId a = t.add(r, leaf(30, 20));
    const NodeId b = t.add(r, leaf(40, 24));
    t.compute({1000, 1000});
    CHECK(t.rect(r) == Rect{0, 0, 120, 40});
    CHECK(t.rect(a) == Rect{20, 8, 30, 20});
    CHECK(t.rect(b) == Rect{60, 8, 40, 24});
}

TEST_CASE("ui layout: fill children share remaining space and respect max") {
    LayoutTree t;
    NodeStyle row;
    row.axis = Axis::Row;
    row.widthSizing = Sizing::Fixed;
    row.size = {300, 40};
    row.gap = 0;
    const NodeId r = t.add(kNoNode, row);
    t.add(r, leaf(100, 40));
    NodeStyle fill;
    fill.widthSizing = Sizing::Fill;
    const NodeId f1 = t.add(r, fill);
    fill.maxSize.width = 50;
    const NodeId f2 = t.add(r, fill);
    t.compute({1000, 1000});
    CHECK(t.rect(f2).width == doctest::Approx(50));
    CHECK(t.rect(f1).width == doctest::Approx(150));
    CHECK(t.rect(f1).x == doctest::Approx(100));
}

TEST_CASE("ui layout: alignment") {
    for (Align a : {Align::Center, Align::End, Align::SpaceBetween}) {
        LayoutTree t;
        NodeStyle row;
        row.axis = Axis::Row;
        row.widthSizing = Sizing::Fixed;
        row.heightSizing = Sizing::Fixed;
        row.size = {200, 100};
        row.mainAlign = a;
        row.crossAlign = Align::Center;
        const NodeId r = t.add(kNoNode, row);
        const NodeId x = t.add(r, leaf(20, 20));
        const NodeId y = t.add(r, leaf(20, 20));
        t.compute({1000, 1000});
        CHECK(t.rect(x).y == doctest::Approx(40));
        if (a == Align::Center) CHECK(t.rect(x).x == doctest::Approx(80));
        if (a == Align::End) CHECK(t.rect(y).right() == doctest::Approx(200));
        if (a == Align::SpaceBetween) {
            CHECK(t.rect(x).x == doctest::Approx(0));
            CHECK(t.rect(y).right() == doctest::Approx(200));
        }
    }
}

TEST_CASE("ui layout: column with fill cross axis and fill root") {
    LayoutTree t;
    NodeStyle col;
    col.widthSizing = Sizing::Fill;
    col.padding = Edges::all(10);
    col.gap = 5;
    const NodeId c = t.add(kNoNode, col);
    NodeStyle child = leaf(10, 30);
    child.widthSizing = Sizing::Fill;
    const NodeId a = t.add(c, child);
    const NodeId b = t.add(c, leaf(40, 30));
    t.compute({400, 300});
    CHECK(t.rect(c).width == doctest::Approx(400));
    CHECK(t.rect(c).height == doctest::Approx(85));
    CHECK(t.rect(a).width == doctest::Approx(380));
    CHECK(t.rect(b) == Rect{10, 45, 40, 30});
}

TEST_CASE("ui layout: wrapping row breaks into lines") {
    LayoutTree t;
    NodeStyle row;
    row.axis = Axis::Row;
    row.widthSizing = Sizing::Fixed;
    row.size = {100, 0};
    row.gap = 10;
    row.lineGap = 4;
    row.wrap = true;
    const NodeId r = t.add(kNoNode, row);
    NodeId items[4];
    for (NodeId& i : items) i = t.add(r, leaf(40, 20));
    t.compute({1000, 1000});
    CHECK(t.rect(items[0]).y == doctest::Approx(0));
    CHECK(t.rect(items[1]).x == doctest::Approx(50));
    CHECK(t.rect(items[2]).x == doctest::Approx(0));
    CHECK(t.rect(items[2]).y == doctest::Approx(24));
}

TEST_CASE("ui layout: min size clamps hug") {
    LayoutTree t;
    NodeStyle s;
    s.minSize = {120, 40};
    const NodeId n = t.add(kNoNode, s);
    t.add(n, leaf(10, 10));
    t.compute({1000, 1000});
    CHECK(t.rect(n).width == doctest::Approx(120));
    CHECK(t.rect(n).height == doctest::Approx(40));
}

TEST_CASE("ui layout: fixed-width wrapping row reports its wrapped height to the parent") {
    LayoutTree t;
    const NodeId col = t.add(kNoNode, NodeStyle{});
    NodeStyle row;
    row.axis = Axis::Row;
    row.wrap = true;
    row.gap = 10;
    row.lineGap = 10;
    row.widthSizing = Sizing::Fixed;
    row.size = {300, 0};
    const NodeId wrapRow = t.add(col, row);
    for (int i = 0; i < 5; ++i) t.add(wrapRow, leaf(100, 50)); // Lines of 2, 2, 1.
    const NodeId below = t.add(col, leaf(40, 20));
    t.compute({1000, 1000});
    CHECK(t.rect(wrapRow).height == doctest::Approx(170)); // 3 lines x 50 + 2 x 10.
    CHECK(t.rect(below).y == doctest::Approx(170));          // Next sibling no longer overlaps.
}
