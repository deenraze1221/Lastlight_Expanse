#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Resources/UploadQueue.h"

#include <doctest/doctest.h>

using namespace zodar::gfx;

namespace {

VkImage fakeImage(uintptr_t v) { return reinterpret_cast<VkImage>(v); }
VkBuffer fakeBuffer(uintptr_t v) { return reinterpret_cast<VkBuffer>(v); }

} // namespace

TEST_CASE("upload ownership: two families release on transfer, acquire on graphics") {
    const UploadedImage images[] = {{fakeImage(0x10), {VK_IMAGE_ASPECT_COLOR_BIT, 0, 5, 0, 1},
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    const VkBuffer buffers[] = {fakeBuffer(0x20), fakeBuffer(0x30)};
    const OwnershipTransfer t = makeOwnershipTransfer(images, buffers, /*transfer*/ 2, /*graphics*/ 0);
    REQUIRE(t.releaseImages.size() == 1);
    REQUIRE(t.acquireImages.size() == 1);
    REQUIRE(t.releaseBuffers.size() == 2);
    REQUIRE(t.acquireBuffers.size() == 2);
    CHECK(t.needsAcquire());
    for (const auto* b : {&t.releaseImages[0], &t.acquireImages[0]}) {
        // Both halves must match exactly in families, layouts and subresources.
        CHECK(b->srcQueueFamilyIndex == 2);
        CHECK(b->dstQueueFamilyIndex == 0);
        CHECK(b->oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CHECK(b->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        CHECK(b->image == images[0].image);
        CHECK(b->subresourceRange.levelCount == 5);
    }
    CHECK(t.releaseImages[0].srcStageMask == VK_PIPELINE_STAGE_2_COPY_BIT);
    CHECK(t.releaseImages[0].srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    CHECK(t.releaseImages[0].dstAccessMask == VK_ACCESS_2_NONE); // Ignored for a release.
    CHECK(t.acquireImages[0].srcAccessMask == VK_ACCESS_2_NONE); // Ignored for an acquire.
    CHECK(t.acquireImages[0].dstStageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    for (size_t i = 0; i < 2; ++i) {
        CHECK(t.releaseBuffers[i].buffer == buffers[i]);
        CHECK(t.acquireBuffers[i].buffer == buffers[i]);
        CHECK(t.releaseBuffers[i].srcQueueFamilyIndex == 2);
        CHECK(t.acquireBuffers[i].dstQueueFamilyIndex == 0);
        CHECK(t.acquireBuffers[i].size == VK_WHOLE_SIZE);
    }
}

TEST_CASE("upload ownership: one family only transitions layouts") {
    const UploadedImage images[] = {{fakeImage(0x10), {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    const VkBuffer buffers[] = {fakeBuffer(0x20)};
    const OwnershipTransfer t = makeOwnershipTransfer(images, buffers, 0, 0);
    REQUIRE(t.releaseImages.size() == 1);
    CHECK(t.acquireImages.empty());
    CHECK(t.releaseBuffers.empty()); // Visibility comes from the timeline semaphore wait.
    CHECK(t.acquireBuffers.empty());
    CHECK_FALSE(t.needsAcquire());
    CHECK(t.releaseImages[0].srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    CHECK(t.releaseImages[0].dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    CHECK(t.releaseImages[0].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(t.releaseImages[0].dstAccessMask == VK_ACCESS_2_MEMORY_READ_BIT);
}

TEST_CASE("format blocks and level sizes") {
    CHECK(imageLevelSize(VK_FORMAT_R8G8B8A8_UNORM, 7, 3) == 7 * 3 * 4);
    CHECK(imageLevelSize(VK_FORMAT_BC7_SRGB_BLOCK, 4, 4) == 16);
    CHECK(imageLevelSize(VK_FORMAT_BC7_UNORM_BLOCK, 5, 5) == 4 * 16); // Partial blocks round up.
    CHECK(imageLevelSize(VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 1, 1) == 8);
    CHECK(imageLevelSize(VK_FORMAT_BC4_UNORM_BLOCK, 8, 4) == 16);
    CHECK(imageLevelSize(VK_FORMAT_BC5_UNORM_BLOCK, 2, 2) == 16);
    CHECK(imageLevelSize(VK_FORMAT_BC3_SRGB_BLOCK, 16, 8) == 8 * 16);
    CHECK(formatBlock(VK_FORMAT_UNDEFINED).bytes == 0);
}
