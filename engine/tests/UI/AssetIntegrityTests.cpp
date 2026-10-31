#include <doctest/doctest.h>

#include "../Support/UiTestDir.h"

#include "../Support/Sha256.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

// Brand logos and fonts must stay byte-for-byte identical to the approved originals.
TEST_CASE("ui assets: every file matches MANIFEST.sha256 and nothing is unlisted") {
    namespace fs = std::filesystem;
    const fs::path dir = zodar::test::uiTestDir();
    std::ifstream manifest(dir / "MANIFEST.sha256");
    REQUIRE(manifest.good());
    std::map<std::string, std::string> expected;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.size() < 67) continue;
        expected[line.substr(66)] = line.substr(0, 64);
    }
    CHECK(expected.size() == 9);

    for (const auto& [rel, hash] : expected) {
        std::ifstream f(dir / fs::path(rel), std::ios::binary);
        REQUIRE_MESSAGE(f.good(), "missing: " << rel);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        CHECK_MESSAGE(zodar::test::sha256Hex(bytes) == hash, "changed: " << rel);
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().filename() == "MANIFEST.sha256") continue;
        const std::string rel = fs::relative(entry.path(), dir).generic_string();
        CHECK_MESSAGE(expected.count(rel) == 1, "not in manifest: " << rel);
    }
}

TEST_CASE("ui assets: sha256 implementation") {
    CHECK(zodar::test::sha256Hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    CHECK(zodar::test::sha256Hex({abc.begin(), abc.end()}) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
