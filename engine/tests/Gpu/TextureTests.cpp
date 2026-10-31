#include "GpuTestContext.h"

#include "AssetPipeline/TextureCompiler.h"
#include "Assets/ImageData.h"
#include "Assets/Ktx2.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Texture.h"
#include "Renderer/TextureArray.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

using namespace zodar;
using namespace zodar::gfx;
using zodar::test::gpu;

namespace {

struct Push {
    uint32_t textureIndex;
    uint32_t samplerIndex;
    float lod;
};

// Renders a fullscreen triangle that samples one bindless texture into a small RGBA8 target.
std::vector<uint8_t> renderSample(BindlessRegistry& registry, uint32_t size, const Push& push,
                                  const char* fragmentSpv = "test_sample.frag.spv") {
    const auto& ctx = *gpu().context;
    ShaderModule vs(ctx.device(), loadSpirv(zodar::test::shaderDir() + "/test_sample.vert.spv"));
    ShaderModule fs(ctx.device(), loadSpirv(zodar::test::shaderDir() + "/" + fragmentSpv));
    GraphicsPipelineDesc desc;
    desc.vertexShader = vs.handle();
    desc.fragmentShader = fs.handle();
    desc.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    desc.setLayouts = {registry.layout()};
    desc.pushConstantSize = sizeof(Push);
    desc.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
    GraphicsPipeline pipeline(ctx, desc);

    GpuImage target(ctx, {size, size, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "SampleTarget"});
    gpu().upload->immediate([&](VkCommandBuffer cmd) {
        transitionImage(cmd, target.handle(), target.fullRange(), VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = target.view();
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, {size, size}};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &rendering);
        const VkViewport viewport{0, 0, float(size), float(size), 0, 1};
        const VkRect2D scissor{{0, 0}, {size, size}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
        const VkDescriptorSet set = registry.set();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        transitionImage(cmd, target.handle(), target.fullRange(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_READ_BIT);
    });
    return gpu().upload->readImage(target, VK_IMAGE_LAYOUT_GENERAL);
}

const uint8_t* pixel(const std::vector<uint8_t>& image, uint32_t size, uint32_t x, uint32_t y) {
    return &image[(static_cast<size_t>(y) * size + x) * 4];
}

ImageData quadrants() {
    // 2x2: red, green / blue, white (top-left origin).
    ImageData image;
    image.name = "Quadrants";
    image.width = image.height = 2;
    image.colorSpace = ColorSpace::Linear;
    image.levels.push_back({255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255});
    return image;
}

} // namespace

TEST_CASE("textures: nearest sampling returns exact texels through the bindless table") {
    zodar::test::ValidationGuard guard;
    BindlessRegistry registry(*gpu().context);
    Texture texture;
    {
        UploadBatch batch(*gpu().upload);
        texture = Texture(batch, registry, quadrants());
    }
    const uint32_t nearest = registry.sampler(SamplerDesc::nearest());
    const auto image = renderSample(registry, 4, {texture.bindlessIndex(), nearest, 0.0f});
    CHECK(pixel(image, 4, 0, 0)[0] == 255); // red
    CHECK(pixel(image, 4, 3, 0)[1] == 255); // green
    CHECK(pixel(image, 4, 0, 3)[2] == 255); // blue
    CHECK(pixel(image, 4, 3, 3)[0] == 255);
    CHECK(pixel(image, 4, 3, 3)[1] == 255);
    CHECK(pixel(image, 4, 1, 1)[1] == 0);   // Still pure red next to the centre: no filtering.
}

TEST_CASE("textures: linear sampling blends neighbours") {
    zodar::test::ValidationGuard guard;
    BindlessRegistry registry(*gpu().context);
    UploadBatch batch(*gpu().upload);
    Texture texture(batch, registry, quadrants());
    batch.submit();
    SamplerDesc desc = SamplerDesc::linear();
    desc.addressU = desc.addressV = AddressMode::ClampToEdge;
    const auto image = renderSample(registry, 4, {texture.bindlessIndex(), registry.sampler(desc), 0.0f});
    const uint8_t* centre = pixel(image, 4, 1, 1); // 3/4 red, 1/4 neighbours
    CHECK(centre[0] > 100);
    CHECK(centre[1] > 20);
    CHECK(centre[2] > 20);
}

TEST_CASE("textures: sRGB textures decode to linear and mips are selectable") {
    zodar::test::ValidationGuard guard;
    BindlessRegistry registry(*gpu().context);
    // sRGB 188 is linear ~0.5; level 1 is forced to a different colour to prove LOD selection.
    ImageData image = ImageData::solid(2, 2, 188, 188, 188, 255, ColorSpace::Srgb, "Srgb");
    image.levels.push_back({0, 0, 255, 255});
    UploadBatch batch(*gpu().upload);
    Texture texture(batch, registry, image);
    batch.submit();
    const uint32_t sampler = registry.sampler(SamplerDesc::nearest());

    const auto level0 = renderSample(registry, 2, {texture.bindlessIndex(), sampler, 0.0f});
    CHECK(std::abs(int(pixel(level0, 2, 0, 0)[0]) - 128) <= 1);
    const auto level1 = renderSample(registry, 2, {texture.bindlessIndex(), sampler, 1.0f});
    CHECK(pixel(level1, 2, 0, 0)[2] == 255);
    CHECK(pixel(level1, 2, 0, 0)[0] == 0);
}

TEST_CASE("bindless registry: slot reuse and sampler deduplication") {
    zodar::test::ValidationGuard guard;
    BindlessRegistry registry(*gpu().context);
    CHECK(registry.sampler(SamplerDesc::nearest()) == registry.sampler(SamplerDesc::nearest()));
    CHECK(registry.sampler(SamplerDesc::linear()) != registry.sampler(SamplerDesc::nearest()));
    CHECK(registry.samplerCount() == 2);

    UploadBatch batch(*gpu().upload);
    Texture a(batch, registry, ImageData::solid(1, 1, 1, 2, 3, 4, ColorSpace::Linear, "a"));
    Texture b(batch, registry, ImageData::solid(1, 1, 1, 2, 3, 4, ColorSpace::Linear, "b"));
    batch.submit();
    CHECK(a.bindlessIndex() != b.bindlessIndex());
    const uint32_t freed = a.bindlessIndex();
    a = Texture{};
    CHECK(registry.textureCount() == 1);
    UploadBatch batch2(*gpu().upload);
    Texture c(batch2, registry, ImageData::solid(1, 1, 0, 0, 0, 0, ColorSpace::Linear, "c"));
    batch2.submit();
    CHECK(c.bindlessIndex() == freed);
    CHECK_THROWS(registry.releaseTexture(9999));
}

TEST_CASE("textures: compiled BCn KTX2 textures upload byte-exact and sample like the CPU decoder") {
    zodar::test::ValidationGuard guard;
    if (!gpu().context->features().bcTextureCompression) {
        MESSAGE("device cannot sample BCn: Texture decodes to RGBA8 (fallback path)");
    }
    BindlessRegistry registry(*gpu().context);
    const uint32_t nearest = registry.sampler(SamplerDesc::nearest());
    constexpr uint32_t kSize = 16;
    ImageData source;
    source.name = "BcnSource";
    source.width = source.height = kSize;
    source.colorSpace = ColorSpace::Linear;
    std::vector<uint8_t> px(kSize * kSize * 4);
    for (uint32_t i = 0; i < kSize * kSize; ++i) {
        px[i * 4 + 0] = uint8_t((i % kSize) * 16);
        px[i * 4 + 1] = uint8_t((i / kSize) * 16);
        px[i * 4 + 2] = uint8_t((i * 37) & 0xFF);
        px[i * 4 + 3] = uint8_t(255 - (i % kSize) * 8);
    }
    source.levels.push_back(px);

    for (PixelFormat format : {PixelFormat::Bc7, PixelFormat::Bc1, PixelFormat::Bc3, PixelFormat::Bc4, PixelFormat::Bc5}) {
        const std::string formatName = toString(format);
        CAPTURE(formatName);
        assets::TextureCompileOptions options;
        options.format = format;
        options.colorSpace = ColorSpace::Linear;
        options.quality = assets::EncodeQuality::Fast;
        // Through the file format, as the engine loads compiled assets.
        const ImageData compiled = readKtx2(writeKtx2(assets::compileTexture(source, options)), formatName).image;
        REQUIRE(compiled.levels.size() == 5);

        UploadBatch batch(*gpu().upload);
        Texture texture(batch, registry, compiled);
        const UploadTicket ticket = batch.submitAsync();
        gpu().upload->queue().wait(ticket);

        if (gpu().context->features().bcTextureCompression) {
            CHECK(texture.format() == textureFormat(format, ColorSpace::Linear));
            for (uint32_t mip = 0; mip < compiled.levels.size(); ++mip) {
                CHECK(gpu().upload->readImage(texture.image(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mip) ==
                      compiled.levels[mip]); // Raw blocks, every baked mip.
            }
        }
        const ImageData decoded = decompressToRgba8(compiled);
        const auto image = renderSample(registry, kSize, {texture.bindlessIndex(), nearest, 0.0f});
        int maxDiff = 0;
        for (size_t i = 0; i < image.size(); ++i) {
            maxDiff = std::max(maxDiff, std::abs(int(image[i]) - int(decoded.levels[0][i])));
        }
        MESSAGE(formatName << ": max |GPU - CPU decode| = " << maxDiff);
        CHECK(maxDiff <= 3); // Hardware BCn interpolation may differ slightly from bcdec.
    }
}

TEST_CASE("texture arrays: every layer is sampled through the bindless 2D_ARRAY view") {
    zodar::test::ValidationGuard guard;
    BindlessRegistry registry(*gpu().context);
    const uint8_t colours[3][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 128}};
    std::vector<ImageData> layers;
    for (const auto& c : colours) {
        ImageData image;
        image.name = "Layer";
        image.width = image.height = 4;
        image.colorSpace = ColorSpace::Linear;
        image.levels.emplace_back();
        for (int i = 0; i < 16; ++i) image.levels[0].insert(image.levels[0].end(), c, c + 4);
        image.generateMips();
        layers.push_back(std::move(image));
    }
    TextureArray array;
    {
        UploadBatch batch(*gpu().upload);
        array = TextureArray(batch, registry, layers, "TestArray");
        batch.submit();
    }
    CHECK(array.layerCount() == 3);
    CHECK(array.image().isArray());
    CHECK(array.image().mipLevels() == 3);
    const uint32_t sampler = registry.sampler(SamplerDesc::nearest());
    for (uint32_t layer = 0; layer < 3; ++layer) {
        const auto image = renderSample(registry, 8, {array.bindlessIndex(), sampler, float(layer)},
                                        "test_sample_array.frag.spv");
        const uint8_t* p = pixel(image, 8, 4, 4);
        CHECK(p[0] == colours[layer][0]);
        CHECK(p[1] == colours[layer][1]);
        CHECK(p[2] == colours[layer][2]);
        CHECK(p[3] == colours[layer][3]);
    }
}
