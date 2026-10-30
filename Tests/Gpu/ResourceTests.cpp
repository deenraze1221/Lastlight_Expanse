#include "GpuTestContext.h"

#include "Renderer/Resources/GpuBuffer.h"
#include "Renderer/Resources/GpuImage.h"
#include "Renderer/Resources/UploadContext.h"

#include <cstring>
#include <numeric>
#include <vector>

using namespace raven::gfx;
using raven::test::gpu;

TEST_CASE("gpu resources: device-local buffer upload and readback round trip") {
    raven::test::ValidationGuard guard;
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
    raven::test::ValidationGuard guard;
    GpuBuffer buffer(*gpu().context, {256, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload, "Mapped"});
    REQUIRE(buffer.mapped() != nullptr);
    const float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    buffer.write(values, sizeof(values), 64);
    const auto bytes = gpu().upload->readBuffer(buffer);
    CHECK(std::memcmp(bytes.data() + 64, values, sizeof(values)) == 0);
    CHECK_THROWS(buffer.write(values, sizeof(values), 250));
}

TEST_CASE("gpu resources: buffer and image ownership moves") {
    raven::test::ValidationGuard guard;
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
    raven::test::ValidationGuard guard;
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
    raven::test::ValidationGuard guard;
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
