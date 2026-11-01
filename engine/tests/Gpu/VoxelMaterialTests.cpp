// Phase V2 on a real device: the engine voxel shader "zodar/voxel" (texture array + exact
// per-layer parameter table), compared against zodar/unlit (emissive, byte-exact) and
// zodar/pbr (lit and fogged) on the same surfaces.
#include "GpuTestContext.h"

#include "AssetManager/BlockMaterials.h"
#include "Assets/ImageData.h"
#include "Core/Error.h"
#include "Core/ImageIO.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Texture.h"
#include "Renderer/Vec4Table.h"
#include "Scene/Voxel/ChunkMesher.h"
#include "SceneFile/BlockFile.h"
#include "SceneFile/DataError.h"

#include <doctest/doctest.h>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace zodar;
using namespace zodar::gfx;
using zodar::test::gpu;

namespace {

struct Fixture {
    SceneRenderer scene{*gpu().context, *gpu().upload, {zodar::test::shaderDir()}};
    RenderGraphResources graphResources{*gpu().context, scene.bindless()};
    FrameAllocator scratch{*gpu().context, 2};
    uint64_t frame = 0;

    std::vector<uint8_t> render(const SceneView& view, VkExtent2D extent) {
        GpuImage out(*gpu().context, {extent.width, extent.height, VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "Out"});
        scratch.beginFrame(static_cast<uint32_t>(frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            const FrameInfo info{cmd, static_cast<uint32_t>(frame % 2), frame, &scratch, nullptr};
            RenderGraph graph(graphResources);
            RGImportedImage imported;
            imported.image = out.handle();
            imported.view = out.view();
            imported.format = out.format();
            imported.extent = extent;
            imported.final = AccessState{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                                         VK_IMAGE_LAYOUT_GENERAL};
            const RGImage output = graph.importImage("Output", imported);
            scene.addToGraph(graph, info, extent, view, output, out.format(), nullptr);
            graph.execute(info);
        });
        ++frame;
        return gpu().upload->readImage(out, VK_IMAGE_LAYOUT_GENERAL);
    }
};

// Temporary block assets: two 16x16 textures and a block file.
//  glow.png  : 4x4-texel checker of two colours, alpha 0 (fully emissive)
//  stone.png : 2-texel checker of two colours, opaque
struct BlockDir {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "zodar_voxel_material_tests";
    BlockDir() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        writeRgba(dir / "glow.png", checker(4, {230, 180, 40, 0}, {40, 90, 200, 0}));
        writeRgba(dir / "stone.png", checker(2, {150, 140, 120, 255}, {90, 85, 80, 255}));
        std::ofstream(dir / "test.blocks.json") << R"({"schema": "zodar.blocks", "version": 1, "uvPerMetre": 1,
            "materials": [
              {"name": "glow", "textures": "glow.png", "emissive": 1, "roughness": 0.7},
              {"name": "stone", "textures": "stone.png", "roughness": 0.55, "metallic": 0.25}],
            "blocks": [{"name": "glow", "material": "glow"}, {"name": "stone", "material": "stone"}]})";
    }
    ~BlockDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    // 16x16 RGBA PNG with stored (uncompressed) deflate blocks: Core writePng drops alpha, and
    // emissive texels need alpha 0.
    static void writeRgba(const std::filesystem::path& file, const std::vector<uint8_t>& px) {
        const auto crc = [](const std::vector<uint8_t>& d, size_t from) {
            uint32_t c = 0xFFFFFFFFu;
            for (size_t i = from; i < d.size(); ++i) {
                c ^= d[i];
                for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            }
            return ~c;
        };
        const auto be32 = [](std::vector<uint8_t>& d, uint32_t v) {
            for (int s = 24; s >= 0; s -= 8) d.push_back(static_cast<uint8_t>(v >> s));
        };
        const auto chunk = [&](std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
            be32(out, static_cast<uint32_t>(data.size()));
            const size_t start = out.size();
            out.insert(out.end(), type, type + 4);
            out.insert(out.end(), data.begin(), data.end());
            be32(out, crc(out, start));
        };
        std::vector<uint8_t> raw;
        for (int y = 0; y < 16; ++y) {
            raw.push_back(0); // filter: none
            raw.insert(raw.end(), px.begin() + y * 64, px.begin() + (y + 1) * 64);
        }
        uint32_t a = 1, b = 0;
        for (uint8_t v : raw) {
            a = (a + v) % 65521u;
            b = (b + a) % 65521u;
        }
        std::vector<uint8_t> z = {0x78, 0x01, 0x01}; // zlib header, final stored block
        const auto len = static_cast<uint16_t>(raw.size());
        z.insert(z.end(), {static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(~len),
                           static_cast<uint8_t>(~len >> 8)});
        z.insert(z.end(), raw.begin(), raw.end());
        be32(z, (b << 16) | a);
        std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        std::vector<uint8_t> ihdr;
        be32(ihdr, 16);
        be32(ihdr, 16);
        ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
        chunk(png, "IHDR", ihdr);
        chunk(png, "IDAT", z);
        chunk(png, "IEND", {});
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(png.data()),
                                                    static_cast<std::streamsize>(png.size()));
        const ImageData check = loadImage(file, ColorSpace::Srgb);
        REQUIRE(check.levels[0] == px);
    }
    static std::vector<uint8_t> checker(int cell, std::array<uint8_t, 4> a, std::array<uint8_t, 4> b) {
        std::vector<uint8_t> px;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const auto& c = ((x / cell + y / cell) & 1) ? b : a;
                px.insert(px.end(), c.begin(), c.end());
            }
        }
        return px;
    }
};

// A 16 x 16 x 1 wall of `id` facing +Z, meshed with the catalogue palette (uv = metres).
MeshData wall(const BlockCatalogue& c, voxel::Voxel id) {
    voxel::VoxelChunk chunk;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) chunk.set(x, y, 0, id);
    }
    voxel::ChunkNeighborhood nb;
    nb.chunks[13] = &chunk;
    voxel::MeshOptions o;
    o.uvPerMetre = c.uvPerMetre;
    return voxel::meshChunk(nb, c.palette, o);
}

SceneView wallView(const GpuMesh& mesh, MaterialHandle material, std::vector<MeshDraw>& draws) {
    SceneView view;
    view.camera.position = {8.0f, 8.0f, 9.0f};
    view.camera.lookAt({8.0f, 8.0f, 1.0f});
    view.lighting.sun.enabled = false;
    view.lighting.ambientIntensity = 0.0f;
    draws = {{&mesh, 0, material, glm::mat4(1.0f)}};
    view.draws = draws;
    return view;
}

size_t distinctColors(const std::vector<uint8_t>& rgba) {
    std::set<uint32_t> colors;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        colors.insert(uint32_t(rgba[i]) | uint32_t(rgba[i + 1]) << 8 | uint32_t(rgba[i + 2]) << 16);
    }
    return colors.size();
}

int maxDifference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    REQUIRE(a.size() == b.size());
    int m = 0;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::abs(int(a[i]) - int(b[i])));
    return m;
}

double meanDifference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) sum += std::abs(int(a[i]) - int(b[i]));
    return sum / static_cast<double>(a.size());
}

// Reference material on the same texture as a plain 2D texture.
struct Reference {
    std::optional<Texture> texture;
    MaterialHandle material = 0;
};

Reference reference(Fixture& f, const std::filesystem::path& png, ShaderHandle shader, float roughness, float metallic) {
    Reference r;
    ImageData image = loadImage(png, ColorSpace::Srgb);
    image.generateMips();
    {
        UploadBatch batch(*gpu().upload);
        r.texture.emplace(batch, f.scene.bindless(), image);
        batch.submit();
    }
    MaterialDesc desc;
    desc.shader = shader;
    desc.roughness = roughness;
    desc.metallic = metallic;
    desc.baseColor = {&*r.texture, SamplerDesc::pixelArt()};
    // zodar/pbr perturbs normals by the default flat normal texel (128/255 is not exactly 0.5);
    // scale 0 gives the interpolated normal, which zodar/voxel uses.
    desc.normalScale = 0.0f;
    r.material = f.scene.materials().createMaterial(desc);
    return r;
}

} // namespace

TEST_CASE("voxel material: per-layer parameters are stored as exact float32 (RGBA8/RGBA16F are not)") {
    zodar::test::ValidationGuard guard;
    const std::vector<glm::vec4> values = {{0.3f, 0.55f, 0.123456789f, 0.0f},
                                           {1.0f, 0.0f, 2.5f, 0.0f},
                                           {0.03f, 1.0f, 1000.0f, 1e-7f}};
    // Why not 8- or 16-bit: roughness 0.3 and emissive 2.5 do not survive them.
    CHECK(std::abs(std::round(0.3f * 255.0f) / 255.0f - 0.3f) > 1e-3f); // RGBA8: 0.30196
    CHECK(std::min(2.5f, 1.0f) != 2.5f);                                  // UNORM saturates emissive
    CHECK(glm::unpackHalf1x16(glm::packHalf1x16(0.3f)) != 0.3f);          // RGBA16F: 0.2998
    CHECK(glm::unpackHalf1x16(glm::packHalf1x16(0.123456789f)) != 0.123456789f);

    Fixture f;
    Vec4Table table;
    {
        UploadBatch batch(*gpu().upload);
        table = Vec4Table(batch, f.scene.bindless(), values, "Params");
        batch.submit();
    }
    CHECK(table.size() == 3);
    CHECK(table.image().format() == VK_FORMAT_R32G32B32A32_SFLOAT);
    const std::vector<uint8_t> back = gpu().upload->readImage(table.image(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    REQUIRE(back.size() == values.size() * sizeof(glm::vec4));
    CHECK(std::memcmp(back.data(), values.data(), back.size()) == 0); // bit-exact
    {
        UploadBatch batch(*gpu().upload);
        CHECK_THROWS_AS(Vec4Table(batch, f.scene.bindless(), std::vector<glm::vec4>{}, "Empty"), Error);
        batch.submit();
    }
    gpu().context->waitIdle();
}

TEST_CASE("voxel material: emissive blocks match zodar/unlit byte-exact") {
    zodar::test::ValidationGuard guard;
    const BlockDir files;
    const BlockCatalogue c = loadBlockCatalogue(files.dir / "test.blocks.json");
    Fixture f;
    std::optional<GpuMesh> mesh;
    {
        UploadBatch batch(*gpu().upload);
        mesh.emplace(batch, wall(c, *c.find("glow")));
        batch.submit();
    }
    {
        BlockMaterials blocks(*gpu().context, *gpu().upload, f.scene, c, "test.blocks.json");
        CHECK(blocks.textures().layerCount() == 2);
        CHECK(blocks.parameters().size() == 2);
        CHECK(BlockMaterials::layerTable(c)[0] == glm::vec4(0.7f, 0.0f, 1.0f, 0.0f));
        const Reference unlit = reference(f, files.dir / "glow.png", f.scene.materials().unlitShader(), 0.5f, 0.0f);

        const VkExtent2D extent{96, 64};
        std::vector<MeshDraw> draws;
        // No light at all: the voxel output is the emissive term texel.rgb * (1 - 0) * 1.
        const auto voxel = f.render(wallView(*mesh, blocks.material(), draws), extent);
        const auto expected = f.render(wallView(*mesh, unlit.material, draws), extent);
        CHECK(distinctColors(voxel) >= 2);
        CHECK(voxel == expected);
        f.scene.materials().releaseMaterial(unlit.material);
        gpu().context->waitIdle();
    }
    gpu().context->waitIdle();
    gpu().context->collectGarbage();
}

TEST_CASE("voxel material: lit and fogged blocks match zodar/pbr; parameters come from the layer table") {
    zodar::test::ValidationGuard guard;
    const BlockDir files;
    const BlockCatalogue c = loadBlockCatalogue(files.dir / "test.blocks.json");
    Fixture f;
    std::optional<GpuMesh> mesh;
    {
        UploadBatch batch(*gpu().upload);
        mesh.emplace(batch, wall(c, *c.find("stone")));
        batch.submit();
    }
    BlockMaterials blocks(*gpu().context, *gpu().upload, f.scene, c, "test.blocks.json");
    const Reference pbr = reference(f, files.dir / "stone.png", f.scene.materials().pbrShader(), 0.55f, 0.25f);
    const Reference smooth = reference(f, files.dir / "stone.png", f.scene.materials().pbrShader(), 0.1f, 0.25f);

    const VkExtent2D extent{96, 64};
    std::vector<MeshDraw> draws;
    const auto lit = [&](MaterialHandle material, bool fog) {
        SceneView view = wallView(*mesh, material, draws);
        view.lighting.sun.enabled = true;
        view.lighting.sun.direction = glm::normalize(glm::vec3(-0.3f, -0.4f, -1.0f));
        view.lighting.sun.intensity = 3.0f;
        view.lighting.ambientIntensity = 0.1f;
        view.lighting.fog.enabled = fog;
        view.lighting.fog.density = 0.12f;
        view.lighting.fog.color = {0.6f, 0.3f, 0.2f};
        return f.render(view, extent);
    };
    const auto voxel = lit(blocks.material(), false);
    const auto reference = lit(pbr.material, false);
    CHECK(distinctColors(voxel) >= 2);
    const int litDiff = maxDifference(voxel, reference);
    MESSAGE("lit: max difference to zodar/pbr " << litDiff << ", identical " << (voxel == reference));
    CHECK(litDiff <= 1);
    // The table's roughness is used: a smoother reference differs clearly.
    CHECK(meanDifference(voxel, lit(smooth.material, false)) > 0.5);

    const auto voxelFog = lit(blocks.material(), true);
    const auto referenceFog = lit(pbr.material, true);
    const int fogDiff = maxDifference(voxelFog, referenceFog);
    MESSAGE("fog: max difference to zodar/pbr " << fogDiff);
    CHECK(fogDiff <= 1);
    CHECK(meanDifference(voxelFog, voxel) > 10.0); // fog is applied

    f.scene.materials().releaseMaterial(pbr.material);
    f.scene.materials().releaseMaterial(smooth.material);
    gpu().context->waitIdle();
}

TEST_CASE("voxel material: texture-array dimension errors name the layer's JSON path") {
    zodar::test::ValidationGuard guard;
    const BlockDir files;
    REQUIRE(writePng(files.dir / "stone.png", 8, 8, std::vector<uint8_t>(8 * 8 * 4, 100)));
    const BlockCatalogue c = loadBlockCatalogue(files.dir / "test.blocks.json");
    Fixture f;
    try {
        BlockMaterials blocks(*gpu().context, *gpu().upload, f.scene, c, "test.blocks.json");
        FAIL("expected DataError");
    } catch (const DataError& e) {
        REQUIRE(e.issues().size() == 1);
        CHECK(e.issues()[0].path == "/materials/1/textures/top");
        CHECK(e.issues()[0].message.find("8x8 (stone.png) differs from the texture array size 16x16") == 0);
    }
    gpu().context->waitIdle();
}
