// Phase C CPU parts: reference-counted AssetCache and texture size standards.
#include "Assets/AssetCache.h"
#include "Assets/TextureStandards.h"
#include "Core/Error.h"

#include <doctest/doctest.h>

#include <memory>
#include <utility>
#include <string>
#include <vector>

using namespace zodar;

namespace {

struct Dummy {
    int value = 0;
};

} // namespace

TEST_CASE("asset cache: loads once, shares handles and retires on the last release") {
    std::vector<int> retired;
    AssetCache<Dummy> cache([&](std::unique_ptr<Dummy> d) { retired.push_back(d->value); });
    int loads = 0;
    const auto load = [&] {
        ++loads;
        return std::make_unique<Dummy>(Dummy{7});
    };
    AssetHandle<Dummy> a = cache.getOrLoad("a", load);
    AssetHandle<Dummy> b = cache.getOrLoad("a", load);
    CHECK(loads == 1);
    CHECK(a == b);
    CHECK(a.ready());
    CHECK(a->value == 7);
    CHECK(a.key() == "a");
    CHECK(a.useCount() == 2);
    CHECK(cache.size() == 1);
    CHECK(cache.find("a") == a);
    CHECK_FALSE(cache.find("missing").valid());
    CHECK(cache.stats().loads == 1);
    CHECK(cache.stats().hits == 1);

    a = {};
    CHECK(retired.empty());
    CHECK(cache.size() == 1);
    b = {};
    CHECK(retired == std::vector<int>{7});
    CHECK(cache.size() == 0);
    CHECK(cache.stats().released == 1);

    // Released: a new request loads again.
    AssetHandle<Dummy> c = cache.getOrLoad("a", load);
    CHECK(loads == 2);
    CHECK(cache.keys() == std::vector<std::string>{"a"});
}

TEST_CASE("asset cache: failed loads keep the message and are cached while held") {
    int retired = 0;
    AssetCache<Dummy> cache([&](std::unique_ptr<Dummy>) { ++retired; });
    int loads = 0;
    AssetHandle<Dummy> bad = cache.getOrLoad("bad.png", [&]() -> std::unique_ptr<Dummy> {
        ++loads;
        fail("bad.png: not a PNG file");
    });
    CHECK(bad.valid());
    CHECK(bad.failed());
    CHECK_FALSE(bad.ready());
    CHECK_FALSE(static_cast<bool>(bad));
    CHECK(bad.error().find("not a PNG") != std::string::npos);
    CHECK_THROWS_AS((void)bad.get(), Error);
    CHECK(cache.stats().failures == 1);

    AssetHandle<Dummy> again = cache.getOrLoad("bad.png", [&] { return std::make_unique<Dummy>(); });
    CHECK(loads == 1);
    CHECK(again.failed());

    AssetHandle<Dummy> null = cache.getOrLoad("null", [] { return std::unique_ptr<Dummy>(); });
    CHECK(null.failed());
    CHECK(null.error() == "loader returned no asset");

    bad = {};
    again = {};
    null = {};
    CHECK(retired == 0); // nothing to retire for failed entries
    CHECK(cache.size() == 0);
    CHECK_THROWS_AS((void)AssetHandle<Dummy>().get(), Error);
}

TEST_CASE("asset cache: handles outliving the cache still release safely") {
    int retired = 0;
    AssetHandle<Dummy> h;
    {
        AssetCache<Dummy> cache([&](std::unique_ptr<Dummy>) { ++retired; });
        h = cache.getOrLoad("x", [] { return std::make_unique<Dummy>(Dummy{1}); });
    }
    CHECK(h->value == 1);
    h = {};
    CHECK(retired == 1);
}

TEST_CASE("texture standards: 32x32 default, 64x64 maximum, violations warn") {
    CHECK((classifyTextureSize(32, 32) == TextureSizeClass::Default));
    CHECK((classifyTextureSize(64, 64) == TextureSizeClass::Maximum));
    CHECK((classifyTextureSize(16, 16) == TextureSizeClass::Small));
    CHECK((classifyTextureSize(8, 8) == TextureSizeClass::Small));
    CHECK((classifyTextureSize(128, 128) == TextureSizeClass::Oversized));
    CHECK((classifyTextureSize(256, 256) == TextureSizeClass::Oversized));
    CHECK((classifyTextureSize(4, 4) == TextureSizeClass::NonStandard));
    CHECK((classifyTextureSize(64, 32) == TextureSizeClass::NonStandard));
    CHECK((classifyTextureSize(48, 48) == TextureSizeClass::NonStandard));
    CHECK(isTextureSizeAllowed(TextureSizeClass::Maximum));
    CHECK_FALSE(isTextureSizeAllowed(TextureSizeClass::Oversized));
    CHECK_FALSE(checkTextureSize("crate.png", 32, 32).has_value());
    CHECK_FALSE(checkTextureSize("crate.png", 64, 64).has_value());
    const auto big = checkTextureSize("hero.png", 128, 128);
    REQUIRE(big.has_value());
    CHECK(big->find("hero.png: 128x128 exceeds the 64x64 maximum") == 0);
    CHECK(big->find("not resized") != std::string::npos);
    const auto odd = checkTextureSize("crate.png", 100, 64);
    REQUIRE(odd.has_value());
    CHECK(odd->find("crate.png: 100x64 is not a standard texture size") == 0);
    CHECK(std::string(toString(TextureSizeClass::Default)) == "default (32x32)");
}

TEST_CASE("texture standards: configurable classes, prohibited sizes, engine default unchanged") {
    const TextureStandard engine = TextureStandard::engineDefault();
    CHECK_NOTHROW(engine.validate());
    CHECK(engine.densityTarget == 32.0);
    CHECK(engine.densityTolerance == 2.0);
    // The engine default reproduces the legacy rule and messages for every size below 256.
    for (uint32_t w : {4u, 8u, 16u, 32u, 48u, 64u, 100u, 128u}) {
        for (uint32_t h : {8u, 32u, 64u, 128u}) {
            const auto legacy = checkTextureSize("t.png", w, h);
            const auto issue = checkTextureSize(engine, "t.png", w, h);
            REQUIRE(legacy.has_value() == issue.has_value());
            if (issue) {
                CHECK((issue->verdict == TextureSizeVerdict::Violation));
                CHECK(issue->message == *legacy);
            }
        }
    }
    // 256 and above: an error under every standard, on either side.
    for (const auto& [w, h] : {std::pair{256u, 256u}, std::pair{512u, 512u}, std::pair{256u, 32u}, std::pair{32u, 300u}}) {
        const auto issue = checkTextureSize(engine, "big.png", w, h);
        REQUIRE(issue.has_value());
        CHECK((issue->verdict == TextureSizeVerdict::Prohibited));
        CHECK(issue->message.find("is prohibited (256x256 and above") != std::string::npos);
        REQUIRE(checkTextureSize("big.png", w, h).has_value());
        CHECK(checkTextureSize("big.png", w, h)->find("is prohibited") != std::string::npos);
    }

    // A game standard (GDD §3 A1 shape): 32 small, 64 environment, 128 characters and weapons.
    TextureStandard game;
    game.classes = {{"small", {32}}, {"environment", {64}}, {"character", {128}}};
    game.defaultClass = "environment";
    game.densityTarget = 64.0;
    CHECK_NOTHROW(game.validate());
    CHECK_FALSE(checkTextureSize(game, "wall.png", 64, 64).has_value());
    CHECK_FALSE(checkTextureSize(game, "bolt.png", 32, 32, "small").has_value());
    CHECK_FALSE(checkTextureSize(game, "hero.png", 128, 128, "character").has_value());
    for (uint32_t size : {8u, 16u, 32u, 128u}) {
        const auto issue = checkTextureSize(game, "wall.png", size, size); // default class: environment
        REQUIRE(issue.has_value());
        CHECK((issue->verdict == TextureSizeVerdict::Violation));
        CHECK(issue->message.find("is not allowed for texture class 'environment' (64x64;") != std::string::npos);
    }
    CHECK((checkTextureSize(game, "hero.png", 64, 64, "character")->verdict == TextureSizeVerdict::Violation));
    CHECK((checkTextureSize(game, "hero.png", 128, 64, "character")->verdict == TextureSizeVerdict::Violation));
    CHECK((checkTextureSize(game, "hero.png", 256, 256, "character")->verdict == TextureSizeVerdict::Prohibited));
    CHECK(checkTextureSize(game, "x.png", 64, 64, "vehicle")->message == "x.png: unknown texture class 'vehicle'");

    // Invalid standards name the problem.
    const auto invalid = [](TextureStandard s) {
        try {
            s.validate();
        } catch (const Error& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    TextureStandard bad = game;
    bad.classes.clear();
    CHECK(invalid(bad).find("no texture classes") != std::string::npos);
    bad = game;
    bad.classes.push_back({"small", {32}});
    CHECK(invalid(bad).find("class 'small' is declared twice") != std::string::npos);
    bad = game;
    bad.classes[1].sizes = {48};
    CHECK(invalid(bad).find("size 48 is not a power of two") != std::string::npos);
    bad = game;
    bad.classes[2].sizes = {256};
    CHECK(invalid(bad).find("at or above the prohibited size 256") != std::string::npos);
    bad = game;
    bad.classes[1].sizes = {64, 32};
    CHECK(invalid(bad).find("ascending and unique") != std::string::npos);
    bad = game;
    bad.defaultClass = "props";
    CHECK(invalid(bad).find("default class 'props' is not declared") != std::string::npos);
    bad = game;
    bad.prohibitedSize = 512;
    CHECK(invalid(bad).find("must be in 1..256") != std::string::npos);
    bad = game;
    bad.densityTarget = 0.0;
    CHECK(invalid(bad).find("densityTarget must be positive") != std::string::npos);
    bad = game;
    bad.classes[0].sizes.clear();
    CHECK(invalid(bad).find("class 'small' allows no sizes") != std::string::npos);
}

TEST_CASE("asset cache: pending entries resolve or reject later (async loads)") {
    int retired = 0;
    AssetCache<Dummy> cache([&](std::unique_ptr<Dummy>) { ++retired; });
    bool created = false;
    AssetHandle<Dummy> a = cache.getOrCreatePending("tex", created);
    CHECK(created);
    CHECK(a.loading());
    CHECK((a.state() == AssetState::Loading));
    CHECK_FALSE(a.ready());
    CHECK_THROWS_WITH_AS((void)a.get(), doctest::Contains("still loading"), Error);
    AssetHandle<Dummy> b = cache.getOrCreatePending("tex", created);
    CHECK_FALSE(created);
    CHECK(a == b);
    // A blocking-style request finds the pending entry instead of loading again.
    AssetHandle<Dummy> c = cache.getOrLoad("tex", [] { return std::make_unique<Dummy>(Dummy{9}); });
    CHECK(c.loading());
    CHECK(cache.stats().loads == 1);
    CHECK(cache.stats().hits == 2);

    cache.resolve(a, std::make_unique<Dummy>(Dummy{5}));
    CHECK(b.ready());
    CHECK(c->value == 5);
    cache.reject(a, "ignored once ready");
    CHECK(a.ready());

    AssetHandle<Dummy> bad = cache.getOrCreatePending("bad", created);
    cache.reject(bad, "decode failed");
    CHECK(bad.failed());
    CHECK(bad.error() == "decode failed");
    AssetHandle<Dummy> empty = cache.getOrCreatePending("empty", created);
    cache.resolve(empty, nullptr);
    CHECK(empty.failed());
    CHECK(cache.stats().failures == 2);

    a = b = c = {};
    CHECK(retired == 1);
    // Dropping a pending entry before completion: nothing to retire.
    AssetHandle<Dummy> dropped = cache.getOrCreatePending("x", created);
    dropped = {};
    CHECK(retired == 1);
    CHECK_FALSE(cache.find("x").valid());
}

TEST_CASE("asset cache: replace swaps content in place (hot reload) and keeps addresses") {
    std::vector<int> retired;
    AssetCache<Dummy> cache([&](std::unique_ptr<Dummy> d) { retired.push_back(d->value); });
    AssetHandle<Dummy> a = cache.getOrLoad("a", [] { return std::make_unique<Dummy>(Dummy{1}); });
    const AssetHandle<Dummy> b = cache.find("a");
    Dummy* address = &a.get();
    CHECK(a.generation() == 0);

    std::unique_ptr<Dummy> old = cache.replace(a, std::make_unique<Dummy>(Dummy{2}));
    REQUIRE(old);
    CHECK(old->value == 1);         // previous content handed back (caller retires it)
    CHECK(&a.get() == address);     // same object, new content
    CHECK(b->value == 2);           // every handle sees it
    CHECK(a.generation() == 1);
    CHECK(cache.replace(a, nullptr) == nullptr); // null: no effect
    CHECK(a.generation() == 1);

    // Failed entry: replace makes it ready (a fixed file reloads).
    AssetHandle<Dummy> f = cache.getOrLoad("f", []() -> std::unique_ptr<Dummy> { throw Error("broken"); });
    REQUIRE(f.failed());
    CHECK(cache.replace(f, std::make_unique<Dummy>(Dummy{5})) == nullptr);
    CHECK(f.ready());
    CHECK(f.error().empty());
    CHECK(f->value == 5);
    CHECK(f.generation() == 1);

    // Loading entry: left alone, value returned.
    bool created = false;
    AssetHandle<Dummy> p = cache.getOrCreatePending("p", created);
    std::unique_ptr<Dummy> back = cache.replace(p, std::make_unique<Dummy>(Dummy{9}));
    REQUIRE(back);
    CHECK(back->value == 9);
    CHECK(p.loading());
    CHECK(p.generation() == 0);

    a = {};
    CHECK(retired.empty()); // b still holds it
}
