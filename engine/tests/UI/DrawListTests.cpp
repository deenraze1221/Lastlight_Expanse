#include <doctest/doctest.h>

#include "UI/DrawList.h"

using namespace zodar::ui;

TEST_CASE("ui: colour packing round-trips") {
    const Color c{0x18, 0xB7, 0x7A, 0x80};
    CHECK(unpackColor(packColor(c)) == c);
    CHECK(packColor(Color::hex(0x112233)) == 0xFF332211u);
}

TEST_CASE("ui: draw commands batch by texture and clip") {
    DrawList dl({0, 0, 100, 100});
    dl.fillRect({0, 0, 10, 10}, color::kBrandAccent);
    dl.fillRect({10, 0, 10, 10}, color::kBrandAccent);
    REQUIRE(dl.commands().size() == 1);
    CHECK(dl.commands()[0].indexCount == 12);
    CHECK(dl.vertices().size() == 8);

    dl.image({0, 0, 5, 5}, 7);
    CHECK(dl.commands().size() == 2);
    CHECK(dl.commands()[1].texture == 7);

    dl.pushClip({0, 0, 50, 50});
    dl.image({0, 0, 5, 5}, 7);
    CHECK(dl.commands().size() == 3);
    CHECK(dl.commands()[2].clip == Rect{0, 0, 50, 50});
    dl.popClip();
    CHECK(dl.currentClip() == Rect{0, 0, 100, 100});
}

TEST_CASE("ui: transparent and empty shapes emit nothing") {
    DrawList dl;
    dl.fillRect({0, 0, 0, 10}, color::kBrandAccent);
    dl.fillRect({0, 0, 10, 10}, Color{1, 2, 3, 0});
    dl.strokeRect({0, 0, 10, 10}, 0.0f, color::kBorder);
    CHECK(dl.indices().empty());
}

TEST_CASE("ui: strokes and rounded shapes produce closed geometry") {
    DrawList dl;
    dl.strokeRect({0, 0, 20, 10}, 1.0f, color::kBorder);
    CHECK(dl.indices().size() == 24);
    dl.reset({0, 0, 100, 100});
    dl.fillRoundRect({0, 0, 40, 20}, 2.0f, color::kBrandAccent);
    CHECK(dl.vertices().size() == 20);
    CHECK(dl.indices().size() == 18 * 3);
    dl.reset({0, 0, 100, 100});
    dl.strokeRoundRect({0, 0, 40, 20}, 2.0f, 1.0f, color::kBorder);
    CHECK(dl.vertices().size() == 40);
    CHECK(dl.indices().size() == 20 * 6);
}
