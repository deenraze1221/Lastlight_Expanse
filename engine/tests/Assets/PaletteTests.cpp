// Phase C7: palette validation (master palette + sub-palettes in assets.json, asset checks).
#include "AssetManager/AssetRegistry.h"
#include "Core/Error.h"
#include "Core/ImageIO.h"
#include "SceneFile/DataError.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace zodar;

namespace {

// RGBA8 image from a list of 0xRRGGBBAA texels.
std::vector<uint8_t> rgba(const std::vector<uint32_t>& texels) {
    std::vector<uint8_t> out;
    for (const uint32_t t : texels) {
        out.insert(out.end(), {uint8_t(t >> 24), uint8_t(t >> 16), uint8_t(t >> 8), uint8_t(t)});
    }
    return out;
}

// writePng drops alpha; transparency needs a real RGBA PNG.
void writeRgbaPng(const std::filesystem::path& file, uint32_t w, uint32_t h, const std::vector<uint32_t>& texels);

struct Dir {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "zodar_palette_tests";
    Dir() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "materials");
    }
    ~Dir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void write(const std::string& name, const std::string& text) const { std::ofstream(dir / name) << text; }
};

std::vector<DataIssue> issuesOf(std::string_view json) {
    try {
        (void)AssetRegistry::parse(json, "p/assets.json");
    } catch (const DataError& e) {
        return e.issues();
    }
    return {};
}

bool hasIssue(const std::vector<DataIssue>& issues, std::string_view path, std::string_view text) {
    for (const DataIssue& i : issues) {
        if (i.path == path && i.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool contains(const std::vector<std::string>& list, std::string_view text) {
    for (const std::string& s : list) {
        if (s.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace {
void writeRgbaPng(const std::filesystem::path& file, uint32_t w, uint32_t h, const std::vector<uint32_t>& texels) {
    const std::vector<uint8_t> px = rgba(texels);
    std::ofstream out(file, std::ios::binary);
    REQUIRE(stbi_write_png_to_func(
                [](void* ctx, void* data, int size) {
                    static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size);
                },
                &out, int(w), int(h), 4, px.data(), int(w * 4)) != 0);
}
} // namespace

TEST_CASE("palette: colours, swatches and the --palette argument") {
    CHECK(parseHexColor("#5adcff") == 0x5adcffu);
    CHECK(parseHexColor("#5ADCFF") == 0x5adcffu);
    CHECK_FALSE(parseHexColor("5adcff").has_value());
    CHECK_FALSE(parseHexColor("#5adcf").has_value());
    CHECK_FALSE(parseHexColor("#5adcfg").has_value());
    CHECK(hexColor(0x00ff10) == "#00ff10");

    const PaletteSource list = parsePaletteArgument("#000000,#FFFFFF");
    CHECK(list.colors == std::vector<std::string>{"#000000", "#FFFFFF"});
    CHECK(list.swatch.empty());
    CHECK(parsePaletteArgument("art/palette.png").swatch == "art/palette.png");

    const Dir d;
    // Swatch: transparent texels are not palette colours; duplicates collapse.
    writeRgbaPng(d.dir / "swatch.png", 4, 1, {0xff0000ff, 0x00ff00ff, 0xff0000ff, 0x0000ff00});
    const Palette p = loadPalette({"swatch.png", {}}, d.dir, "master");
    CHECK(p.colors == std::vector<uint32_t>{0x00ff00u, 0xff0000u});
    CHECK(p.contains(0xff0000));
    CHECK_FALSE(p.contains(0x0000ff));
    CHECK(loadPalette({{}, {"#0000ff", "#0000FF"}}, d.dir, "x").colors == std::vector<uint32_t>{0x0000ffu});
    CHECK_THROWS_WITH_AS(loadPalette({"missing.png", {}}, d.dir, "m"), doctest::Contains("missing.png"), Error);
    CHECK_THROWS_WITH_AS(loadPalette({{}, {"red"}}, d.dir, "m"), doctest::Contains("malformed colour 'red'"), Error);
    writeRgbaPng(d.dir / "clear.png", 1, 1, {0x12345600});
    CHECK_THROWS_WITH_AS(loadPalette({"clear.png", {}}, d.dir, "m"), doctest::Contains("has no colours"), Error);
}

TEST_CASE("palette: assets.json forms, validation and round-trip; no palette = unchanged files") {
    const AssetRegistry shorthand = AssetRegistry::parse(
        R"({"schema": "zodar.asset_registry", "version": 1, "palette": "palette.png", "assets": []})", "p/assets.json");
    REQUIRE(shorthand.palette().has_value());
    CHECK(shorthand.palette()->master.swatch == "palette.png");
    const AssetRegistry list = AssetRegistry::parse(
        R"({"schema": "zodar.asset_registry", "version": 1, "palette": ["#000000", "#ffffff"], "assets": []})",
        "p/assets.json");
    CHECK(list.palette()->master.colors.size() == 2);

    const AssetRegistry full = AssetRegistry::parse(R"({"schema": "zodar.asset_registry", "version": 1,
        "palette": {"colors": "palette.png", "subPalettes": {"ash": ["#808080"], "ember": "ember.png"}},
        "assets": [{"id": "rock", "type": "texture", "source": "rock.png", "palette": "ash"}]})", "p/assets.json");
    CHECK(full.palette()->subPalettes.size() == 2);
    CHECK(full.palette()->subPalettes.at("ember").swatch == "ember.png");
    CHECK(full.at("rock").palette == "ash");
    const AssetRegistry back = AssetRegistry::parse(full.serialize(), "p/assets.json");
    CHECK(back == full);
    CHECK(back.serialize() == full.serialize());

    // Without a palette nothing new is written.
    AssetRegistry plain("p");
    AssetEntry e{"t", AssetType::Texture, "t.png"};
    plain.upsert(e);
    CHECK(plain.serialize().find("palette") == std::string::npos);

    const auto issues = issuesOf(R"({"schema": "zodar.asset_registry", "version": 1,
        "palette": {"colors": ["#12345", 7], "subPalettes": {"a": []}, "extra": 1},
        "assets": [{"id": "x", "type": "texture", "source": "x.png", "palette": "nope"}]})");
    CHECK(hasIssue(issues, "/palette/colors/0", "expected a colour \"#rrggbb\", got \"#12345\""));
    CHECK(hasIssue(issues, "/palette/colors/1", "got number"));
    CHECK(hasIssue(issues, "/palette/subPalettes/a", "non-empty array"));
    CHECK(hasIssue(issues, "/palette/extra", "unknown field"));
    CHECK(hasIssue(issues, "/assets/0/palette", "unknown sub-palette 'nope'"));
    CHECK(hasIssue(issuesOf(R"({"schema": "zodar.asset_registry", "version": 1, "assets": [
        {"id": "x", "type": "texture", "source": "x.png", "palette": "a"}]})"),
                   "/assets/0/palette", "the registry has no \"palette\""));
    CHECK(hasIssue(issuesOf(R"({"schema": "zodar.asset_registry", "version": 1, "palette": {}, "assets": []})"),
                   "/palette/colors", "required field is missing"));
}

TEST_CASE("palette: checks count opaque off-palette texels of colour textures only") {
    const Dir d;
    constexpr uint32_t A = 0x102030ff, B = 0xa0b0c0ff, OFF = 0xff00ffff, GLOW = 0x5adcffff;
    // 8x8: 40 A, 16 B, 4 opaque OFF, 4 transparent OFF (ignored).
    std::vector<uint32_t> texels(64, A);
    for (int i = 0; i < 16; ++i) texels[size_t(i)] = B;
    for (int i = 16; i < 20; ++i) texels[size_t(i)] = OFF;
    for (int i = 20; i < 24; ++i) texels[size_t(i)] = 0xff00ff00;
    writeRgbaPng(d.dir / "tile.png", 8, 8, texels);
    std::vector<uint32_t> glow(64, A);
    glow[0] = glow[1] = GLOW;
    writeRgbaPng(d.dir / "glow.png", 8, 8, glow);
    writeRgbaPng(d.dir / "normal.png", 8, 8, std::vector<uint32_t>(64, OFF));
    d.write("materials/m.material.json",
            R"({"baseColorTexture": "../tile.png", "normalTexture": "../normal.png", "emissiveTexture": "../glow.png"})");

    AssetRegistry r(d.dir);
    PaletteConfig config;
    config.master.colors = {"#102030", "#a0b0c0"};
    config.subPalettes["glow"].colors = {"#5adcff"};
    r.setPalette(config);
    importAsset(r, d.dir / "tile.png");
    AssetImportOptions glowOptions;
    glowOptions.palette = "glow";
    importAsset(r, d.dir / "glow.png", glowOptions);
    AssetImportOptions normalOptions;
    normalOptions.usage = TextureUsage::Normal;
    importAsset(r, d.dir / "normal.png", normalOptions);
    importAsset(r, d.dir / "materials" / "m.material.json");
    AssetImportOptions bad;
    bad.palette = "missing";
    CHECK_THROWS_WITH_AS(importAsset(r, d.dir / "tile.png", bad), doctest::Contains("unknown sub-palette"), Error);

    const AssetCheckResult tile = checkAsset(r, r.at("tile"));
    REQUIRE(tile.paletteDeviation.has_value());
    CHECK(tile.paletteDeviation->palette == "master");
    CHECK(tile.paletteDeviation->texels == 60);
    CHECK(tile.paletteDeviation->offPalette == 4);
    CHECK(tile.paletteDeviation->percent() == doctest::Approx(100.0 * 4 / 60));
    CHECK(tile.paletteDeviation->examples == std::vector<uint32_t>{0xff00ffu});
    CHECK(contains(tile.warnings, "palette: 4 of 60 texels (6.67%) not in palette 'master' (e.g. #ff00ff)"));
    REQUIRE(tile.textures.size() == 1);
    CHECK(tile.textures[0].paletteChecked);
    CHECK(tile.textures[0].offPaletteTexels == 4);

    // The sub-palette extends the master palette.
    const AssetCheckResult glowCheck = checkAsset(r, r.at("glow"));
    CHECK(glowCheck.paletteDeviation->palette == "master+glow");
    CHECK(glowCheck.paletteDeviation->offPalette == 0);
    CHECK(glowCheck.warnings.empty());
    r.remove("glow");
    importAsset(r, d.dir / "glow.png"); // master only now
    CHECK(checkAsset(r, r.at("glow")).paletteDeviation->offPalette == 2);

    // Data/normal textures are not colour data.
    const AssetCheckResult normal = checkAsset(r, r.at("normal"));
    CHECK_FALSE(normal.paletteDeviation.has_value());
    CHECK(normal.warnings.empty());
    // Materials: base colour + emissive counted, the normal map is not.
    const AssetCheckResult material = checkAsset(r, r.at("m"));
    REQUIRE(material.paletteDeviation.has_value());
    CHECK(material.paletteDeviation->texels == 60 + 64);
    CHECK(material.paletteDeviation->offPalette == 4 + 2);

    // Report: paletteDeviation and per-texture counts.
    const std::string report = assetReportJson(r, checkRegistry(r));
    CHECK(report.find("\"paletteDeviation\"") != std::string::npos);
    CHECK(report.find("\"offPaletteTexels\": 4") != std::string::npos);
    CHECK(report.find("\"#ff00ff\"") != std::string::npos);

    // --palette overrides the master palette; a broken palette is an error, not a crash.
    AssetCheckOptions everything;
    everything.palette = PaletteSource{{}, {"#102030", "#a0b0c0", "#ff00ff"}};
    CHECK(checkAsset(r, r.at("tile"), everything).paletteDeviation->offPalette == 0);
    AssetCheckOptions broken;
    broken.palette = PaletteSource{"nope.png", {}};
    CHECK(contains(checkAsset(r, r.at("tile"), broken).errors, "palette: "));

    // No palette at all: no deviation, no new report keys, same warnings as before.
    r.setPalette(std::nullopt);
    r.remove("glow");
    const AssetCheckResult plain = checkAsset(r, r.at("tile"));
    CHECK_FALSE(plain.paletteDeviation.has_value());
    CHECK(plain.warnings.empty());
    const std::string plainReport = assetReportJson(r, {plain});
    CHECK(plainReport.find("paletteDeviation") == std::string::npos);
    CHECK(plainReport.find("paletteTexels") == std::string::npos);
}

TEST_CASE("palette: the example registry uses only its palette colours") {
    const AssetRegistry r =
        AssetRegistry::load(std::filesystem::path(ZODAR_EXAMPLES_DIR) / "asset_loading" / "assets.json");
    REQUIRE(r.palette().has_value());
    CHECK(r.palette()->master.swatch == "palette.png");
    CHECK(r.at("rune").palette == "glow");
    for (const AssetCheckResult& res : checkRegistry(r)) {
        REQUIRE_MESSAGE(res.paletteDeviation.has_value(), res.id);
        CHECK_MESSAGE(res.paletteDeviation->offPalette == 0, res.id);
    }
}

TEST_CASE("palette: scenes check a texture used as colour by any material; long names are not truncated") {
    const Dir d;
    writeRgbaPng(d.dir / "shared.png", 8, 8, std::vector<uint32_t>(64, 0xff00ffff)); // all off-palette
    // Material "a" uses it as a normal map first, "b" as base colour.
    d.write("level.scene.json", R"({"version": 1, "materials": {
        "a": {"normalTexture": "shared.png"}, "b": {"baseColorTexture": "shared.png"}}, "entities": []})");
    AssetRegistry r(d.dir);
    PaletteConfig config;
    config.master.colors = {"#102030"};
    const std::string longName(200, 'p');
    config.subPalettes[longName].colors = {"#a0b0c0"};
    r.setPalette(config);
    AssetImportOptions options;
    options.palette = longName;
    importAsset(r, d.dir / "level.scene.json", options);
    const AssetCheckResult scene = checkAsset(r, r.at("level"));
    REQUIRE(scene.paletteDeviation.has_value());
    CHECK(scene.paletteDeviation->offPalette == 64);
    CHECK(contains(scene.warnings, "not in palette 'master+" + longName + "' (e.g. #ff00ff)"));
}
