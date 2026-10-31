#include "GpuTestContext.h"

#include "Renderer/Resources/GpuBuffer.h"
#include "Renderer/Resources/GpuImage.h"
#include "Renderer/Resources/UploadContext.h"

#include <cstring>
#include <memory>
#include <numeric>
#include <vector>

using namespace zodar::gfx;
using zodar::test::gpu;

TEST_CASE("gpu resources: device-local buffer upload and readback round trip") {
    zodar::test::ValidationGuard guard;
    std::vector<uint32_t> data(4096);
    std::iota(data.begin(), data.end(), 7u);

    GpuBuffer buffer;
    {
        UploadBatch batch(*gpu().upload);
        buffer = batch.createBuffer(data.data(), data.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    "TestBuffer");
        batch.submit();
    }
    REQUIRE(buffer.isValid());
    CHECK(buffer.mapped() == nullptr);

    const auto bytes = gpu().upload->readBuffer(buffer);
    REQUIRE(bytes.size() == data.size() * sizeof(uint32_t));
    CHECK(std::memcmp(bytes.data(), data.data(), bytes.size()) == 0);
}

TEST_CASE("gpu resources: mapped upload buffer write") {
    zodar::test::ValidationGuard guard;
    GpuBuffer buffer(*gpu().context, {256, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload, "Mapped"});
    REQUIRE(buffer.mapped() != nullptr);
    const float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    buffer.write(values, sizeof(values), 64);
    const auto bytes = gpu().upload->readBuffer(buffer);
    CHECK(std::memcmp(bytes.data() + 64, values, sizeof(values)) == 0);
    CHECK_THROWS(buffer.write(values, sizeof(values), 250));
}

TEST_CASE("gpu resources: buffer and image ownership moves") {
    zodar::test::ValidationGuard guard;
    GpuBuffer a(*gpu().context, {64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::Upload, "A"});
    const VkBuffer handle = a.handle();
    GpuBuffer b(std::move(a));
    CHECK_FALSE(a.isValid());
    CHECK(b.handle() == handle);
    b = GpuBuffer{};
    CHECK_FALSE(b.isValid());

    GpuImage image(*gpu().context, {8, 8, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "Moved"});
    GpuImage other = std::move(image);
    CHECK_FALSE(image.isValid());
    CHECK(other.isValid());
    CHECK(other.view() != VK_NULL_HANDLE);
}

TEST_CASE("gpu resources: mip chain upload and per-mip readback") {
    zodar::test::ValidationGuard guard;
    const uint32_t size = 16;
    const uint32_t mips = fullMipCount(size, size);
    CHECK(mips == 5);

    std::vector<std::vector<uint8_t>> pixels;
    std::vector<ImageLevelData> levels;
    for (uint32_t m = 0; m < mips; ++m) {
        const uint32_t s = size >> m;
        pixels.emplace_back(static_cast<size_t>(s) * s * 4, static_cast<uint8_t>(10 + m * 40));
    }
    for (uint32_t m = 0; m < mips; ++m) {
        levels.push_back({size >> m, size >> m, pixels[m]});
    }

    GpuImage image(*gpu().context, {size, size, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    mips, "MipTest"});
    {
        UploadBatch batch(*gpu().upload);
        batch.uploadImage(image, levels);
    } // Destructor submits.

    for (uint32_t m = 0; m < mips; ++m) {
        const auto read = gpu().upload->readImage(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m);
        CHECK(read == pixels[m]);
    }
}

TEST_CASE("gpu resources: invalid uploads are rejected") {
    zodar::test::ValidationGuard guard;
    GpuImage image(*gpu().context, {4, 4, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "Bad"});
    std::vector<uint8_t> tooSmall(10);
    const ImageLevelData level{4, 4, tooSmall};
    UploadBatch batch(*gpu().upload);
    CHECK_THROWS(batch.uploadImage(image, std::span(&level, 1)));
    CHECK_THROWS(GpuImage(*gpu().context, {0, 4, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "Zero"}));
    CHECK_THROWS(GpuImage(*gpu().context, {4, 4, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 9, "Mips"}));
}

TEST_CASE("gpu resources: depth image aspect") {
    GpuImage depth(*gpu().context, {32, 32, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 1, "Depth"});
    CHECK(depth.aspect() == VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(isDepthFormat(VK_FORMAT_D32_SFLOAT));
    CHECK_FALSE(isDepthFormat(VK_FORMAT_R8G8B8A8_UNORM));
}

#include "Renderer/Resources/FrameAllocator.h"

TEST_CASE("frame allocator: alignment, growth, addresses and slot recycling") {
    zodar::test::ValidationGuard guard;
    FrameAllocator scratch(*gpu().context, 2, 1024);
    scratch.beginFrame(0);
    const auto a = scratch.allocate(100);
    const auto b = scratch.allocate(100, 64);
    CHECK(a.offset % 256 == 0);
    CHECK(b.offset % 64 == 0);
    CHECK(b.offset >= a.offset + 100);
    CHECK(a.address != 0);
    CHECK(b.address == a.address + (b.offset - a.offset)); // Same block, consistent addressing.
    const auto big = scratch.allocate(5000);                // Larger than a block: new block.
    CHECK(big.buffer != a.buffer);
    const uint32_t values[3] = {1, 2, 3};
    scratch.write(big, values, sizeof(values));
    CHECK(static_cast<const uint32_t*>(big.data)[2] == 3);
    const VkDeviceSize reserved = scratch.bytesReserved();

    scratch.beginFrame(1);
    CHECK(scratch.bytesUsed() == 0);
    scratch.beginFrame(0); // Recycled: same memory, no new blocks.
    const auto again = scratch.allocate(100);
    CHECK(again.buffer == a.buffer);
    CHECK(again.offset == 0);
    CHECK(scratch.bytesReserved() == reserved);
    CHECK_THROWS(scratch.allocate(0));
}

TEST_CASE("deletion queue: retired GPU objects are destroyed after their submission completes") {
    zodar::test::ValidationGuard guard;
    auto buffer = std::make_shared<GpuBuffer>(*gpu().context,
                                              BufferDesc{256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUsage::Upload, "Retired"});
    bool destroyed = false;
    gpu().context->deletionQueue().retire([buffer, &destroyed]() mutable {
        buffer.reset();
        destroyed = true;
    });
    buffer.reset(); // Only the queue holds it now.
    CHECK_FALSE(destroyed);
    gpu().upload->immediate([](VkCommandBuffer) {}); // Submits, waits and collects.
    CHECK(destroyed);
    CHECK(gpu().context->timeline().completedValue() == gpu().context->timeline().lastSubmitted());
}

#include "Renderer/SceneRenderer.h"
#include "../Support/TempFile.h"

#include <chrono>
#include <filesystem>
#include <fstream>

TEST_CASE("pipeline cache: persisted to disk, reloaded, corrupted files ignored") {
    const zodar::test::TempFile cacheFile("zodar_test_pipeline_cache.bin");
    const std::filesystem::path& path = cacheFile.path();
    const auto buildPipelines = [&](size_t* loaded) {
        VulkanContext context({"PipelineCacheTest", true, false, path}, nullptr);
        *loaded = context.pipelineCacheObject()->loadedBytes();
        UploadContext upload(context);
        SceneRenderer scene(context, upload, {zodar::test::shaderDir()});
        const auto start = std::chrono::steady_clock::now();
        for (AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Blend}) {
            for (bool twoSided : {false, true}) {
                for (ShaderHandle shader : {scene.materials().pbrShader(), scene.materials().unlitShader()}) {
                    MaterialDesc d;
                    d.shader = shader;
                    d.alphaMode = mode;
                    d.doubleSided = twoSided;
                    scene.materials().pipeline(scene.materials().createMaterial(d));
                }
            }
        }
        CHECK(context.validationErrorCount() == 0);
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    size_t loaded = 0;
    const double cold = buildPipelines(&loaded);
    CHECK(loaded == 0);
    REQUIRE(std::filesystem::exists(path));
    const auto fileSize = std::filesystem::file_size(path);
    // Drivers may store only the header (lavapipe does not cache pipelines); real GPU
    // drivers store compiled pipelines after it.
    CHECK(fileSize >= sizeof(VkPipelineCacheHeaderVersionOne));
    const double warm = buildPipelines(&loaded);
    CHECK(loaded == fileSize); // Validated and handed to the driver.
    MESSAGE("8 pipelines: cold " << cold << " ms, warm " << warm << " ms, cache " << fileSize
                                 << " bytes (driver-dependent; informational)");

    cacheFile.write("definitely not a pipeline cache");
    buildPipelines(&loaded);
    CHECK(loaded == 0); // Rejected before reaching the driver, rebuilt and saved again.
    CHECK(cacheFile.remove()); // The cache file handle was closed after loading and saving.
}
