#include "GpuTestContext.h"

#include "Core/Profiler.h"
#include "Renderer/Resources/GpuBuffer.h"
#include "Renderer/Resources/GpuImage.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Resources/UploadQueue.h"

#include <chrono>
#include <cstring>
#include <numeric>
#include <vector>

using namespace zodar::gfx;
using zodar::test::gpu;

namespace {

std::vector<uint8_t> pattern(size_t bytes, uint32_t seed) {
    std::vector<uint8_t> v(bytes);
    uint32_t x = seed * 2654435761u + 1;
    for (auto& b : v) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = static_cast<uint8_t>(x);
    }
    return v;
}

// Stands in for a frame: a graphics command buffer that acquires finished uploads.
uint32_t acquireInFrame(UploadContext& upload) {
    uint32_t acquired = 0;
    upload.immediate([&](VkCommandBuffer cmd) { acquired = upload.queue().acquireCompleted(cmd); });
    return acquired;
}

} // namespace

TEST_CASE("upload queue: asynchronous batches become ready only after a graphics acquire") {
    zodar::test::ValidationGuard guard;
    UploadContext& upload = *gpu().upload;
    UploadQueue& queue = upload.queue();
    MESSAGE("upload queue: " << (queue.dedicated() ? "dedicated transfer queue" : "graphics queue fallback")
                             << ", family " << queue.family());

    constexpr int kBatches = 6;
    std::vector<std::vector<uint8_t>> bufferData, imageData;
    std::vector<GpuBuffer> buffers;
    std::vector<GpuImage> images;
    std::vector<UploadTicket> tickets;
    for (int i = 0; i < kBatches; ++i) {
        UploadBatch batch(upload);
        bufferData.push_back(pattern(size_t(64 + i) * 1024, uint32_t(i)));
        buffers.push_back(batch.createBuffer(bufferData.back().data(), bufferData.back().size(),
                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                             "AsyncBuffer"));
        imageData.push_back(pattern(64 * 32 * 4, uint32_t(100 + i)));
        images.emplace_back(*gpu().context, ImageDesc{64, 32, VK_FORMAT_R8G8B8A8_UNORM,
                                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                                      1, "AsyncImage"});
        const ImageLevelData level{64, 32, imageData.back()};
        batch.uploadImage(images.back(), std::span(&level, 1));
        tickets.push_back(batch.submitAsync());
        REQUIRE(tickets.back().valid());
        CHECK_FALSE(queue.isReady(tickets.back())); // Never ready before an acquire.
    }
    CHECK(tickets.back().value > tickets.front().value);

    queue.waitIdle(); // Transfers done, still not acquired.
    for (const UploadTicket& t : tickets) {
        CHECK(queue.isTransferComplete(t));
        CHECK_FALSE(queue.isReady(t));
    }
    CHECK(queue.stats().inFlight == 0);
    CHECK(acquireInFrame(upload) == kBatches);
    for (const UploadTicket& t : tickets) CHECK(queue.isReady(t));
    CHECK(acquireInFrame(upload) == 0); // Each upload is acquired exactly once.

    for (int i = 0; i < kBatches; ++i) {
        CHECK(upload.readBuffer(buffers[size_t(i)]) == bufferData[size_t(i)]);
        CHECK(upload.readImage(images[size_t(i)], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) == imageData[size_t(i)]);
    }
}

TEST_CASE("upload queue: blocking submit and mixed async/blocking ordering") {
    zodar::test::ValidationGuard guard;
    UploadContext& upload = *gpu().upload;
    UploadQueue& queue = upload.queue();

    const auto a = pattern(300000, 7), b = pattern(1000, 8);
    UploadBatch first(upload);
    GpuBuffer bufA = first.createBuffer(a.data(), a.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "A");
    const UploadTicket ta = first.submitAsync();

    UploadBatch second(upload);
    GpuBuffer bufB = second.createBuffer(b.data(), b.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "B");
    second.submit(); // Blocking: B is ready, A is independent.
    CHECK(upload.readBuffer(bufB) == b);

    queue.wait(ta); // Blocking acquire of an async ticket.
    CHECK(queue.isReady(ta));
    CHECK(upload.readBuffer(bufA) == a);
    CHECK(acquireInFrame(upload) == 0);
    CHECK(queue.isReady(UploadTicket{})); // An empty batch needs nothing.
}

TEST_CASE("upload queue: shared buffers accept copies while in use; exclusive ones are rejected") {
    zodar::test::ValidationGuard guard;
    UploadContext& upload = *gpu().upload;
    GpuBuffer shared(*gpu().context, {4096, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      MemoryUsage::GpuOnly, "Shared", true});
    const auto data = pattern(4096, 3);
    for (int i = 0; i < 4; ++i) {
        UploadBatch batch(upload);
        batch.copyToBuffer(data.data() + i * 1024, 1024, shared, VkDeviceSize(i) * 1024);
        batch.submit();
    }
    CHECK(upload.readBuffer(shared) == data);

    GpuBuffer exclusive(*gpu().context, {64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::GpuOnly, "Exclusive"});
    UploadBatch batch(upload);
    if (upload.queue().dedicated()) {
        CHECK_THROWS(batch.copyToBuffer(data.data(), 64, exclusive, 0));
    } else {
        batch.copyToBuffer(data.data(), 64, exclusive, 0); // One family: no ownership to transfer.
    }
    batch.submit();
}

TEST_CASE("upload queue: graphics-queue fallback path on the same device") {
    if (!gpu().context->features().dedicatedTransfer) {
        MESSAGE("device has no dedicated transfer family: the shared context already uses the fallback");
        return;
    }
    // A second device that forces uploads onto the graphics queue (A/B of both paths).
    zodar::gfx::ContextDesc desc{"ZodarGpuTests.fallback", true, true};
    desc.dedicatedTransferQueue = false;
    VulkanContext context(desc, nullptr);
    {
        UploadContext upload(context);
        CHECK_FALSE(upload.queue().dedicated());
        const auto data = pattern(65536, 11);
        GpuBuffer buffer;
        {
            UploadBatch batch(upload);
            buffer = batch.createBuffer(data.data(), data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "Fallback");
            batch.submit();
        }
        CHECK(upload.readBuffer(buffer) == data);
    }
    CHECK(context.validationErrorCount() == 0);
}

TEST_CASE("upload queue: throughput (informational)") {
    zodar::test::ValidationGuard guard;
    UploadContext& upload = *gpu().upload;
    constexpr size_t kBytes = 32u << 20;
    const auto data = pattern(kBytes, 5);
    const auto start = std::chrono::steady_clock::now();
    UploadBatch batch(upload);
    GpuBuffer buffer = batch.createBuffer(data.data(), kBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "Throughput");
    const auto staged = std::chrono::steady_clock::now();
    const UploadTicket ticket = batch.submitAsync();
    const auto submitted = std::chrono::steady_clock::now();
    upload.queue().wait(ticket);
    const auto done = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    MESSAGE("32 MiB upload: staging " << ms(start, staged) << " ms, submitAsync " << ms(staged, submitted)
                                      << " ms (non-blocking), transfer+acquire " << ms(submitted, done)
                                      << " ms (driver-dependent; informational)");
    CHECK(ms(staged, submitted) < ms(submitted, done) + 50.0); // Submission does not wait for the copy.
    CHECK(upload.queue().stats().stagingBytesInFlight == 0);
}
