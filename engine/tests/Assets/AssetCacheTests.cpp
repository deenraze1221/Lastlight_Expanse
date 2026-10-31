// Phase C CPU parts: reference-counted AssetCache and texture size standards.
#include "Assets/AssetCache.h"
#include "Assets/TextureStandards.h"
#include "Core/Error.h"

#include <doctest/doctest.h>

#include <memory>
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

TEST_CASE("texture standards: size classes and warnings") {
    CHECK((classifyTextureSize(32, 32) == TextureSizeClass::Small));
    CHECK((classifyTextureSize(64, 64) == TextureSizeClass::Primary));
    CHECK((classifyTextureSize(128, 128) == TextureSizeClass::Large));
    CHECK((classifyTextureSize(256, 256) == TextureSizeClass::Unique));
    CHECK((classifyTextureSize(512, 512) == TextureSizeClass::NonStandard));
    CHECK((classifyTextureSize(16, 16) == TextureSizeClass::NonStandard));
    CHECK((classifyTextureSize(64, 32) == TextureSizeClass::NonStandard));
    CHECK((classifyTextureSize(48, 48) == TextureSizeClass::NonStandard));
    CHECK_FALSE(checkTextureSize("crate.png", 64, 64).has_value());
    const auto warning = checkTextureSize("crate.png", 100, 64);
    REQUIRE(warning.has_value());
    CHECK(warning->find("crate.png: 100x64") == 0);
    CHECK(warning->find("64x64 primary") != std::string::npos);
    CHECK(std::string(toString(TextureSizeClass::Primary)) == "primary (64x64)");
}
