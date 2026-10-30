#include "Renderer/Resources/UploadContext.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <cstring>

namespace raven::gfx {

uint32_t formatBytesPerPixel(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM: return 1;
    case VK_FORMAT_R8G8_UNORM: return 2;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT:
        return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    default: return 0;
    }
}

UploadContext::UploadContext(const VulkanContext& context) : m_context(context) {
    const VkDevice device = context.device();
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = context.graphicsQueueFamily();
    RAVEN_VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_pool));

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &m_cmd);
    if (result == VK_SUCCESS) {
        result = vkCreateFence(device, &fenceInfo, nullptr, &m_fence);
    }
    if (result != VK_SUCCESS) {
        vkDestroyCommandPool(device, m_pool, nullptr);
        throwVkError(result, "UploadContext setup");
    }
    context.setDebugName(m_pool, VK_OBJECT_TYPE_COMMAND_POOL, "UploadContext");
}

UploadContext::~UploadContext() {
    vkDestroyFence(m_context.device(), m_fence, nullptr);
    vkDestroyCommandPool(m_context.device(), m_pool, nullptr);
}

void UploadContext::immediate(const std::function<void(VkCommandBuffer)>& record) {
    RAVEN_ENSURE(!m_recording, "UploadContext is already recording (nested batch?)");
    const VkDevice device = m_context.device();
    RAVEN_VK_CHECK(vkResetCommandPool(device, m_pool, 0));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    RAVEN_VK_CHECK(vkBeginCommandBuffer(m_cmd, &begin));
    m_recording = true;
    try {
        record(m_cmd);
    } catch (...) {
        vkEndCommandBuffer(m_cmd);
        m_recording = false;
        throw;
    }
    m_recording = false;
    RAVEN_VK_CHECK(vkEndCommandBuffer(m_cmd));

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = m_cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    RAVEN_VK_CHECK(vkQueueSubmit2(m_context.graphicsQueue(), 1, &submit, m_fence));
    RAVEN_VK_CHECK(vkWaitForFences(device, 1, &m_fence, VK_TRUE, UINT64_MAX));
    RAVEN_VK_CHECK(vkResetFences(device, 1, &m_fence));
}

std::vector<uint8_t> UploadContext::readBuffer(const GpuBuffer& buffer) {
    GpuBuffer readback(m_context, {buffer.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::Readback,
                                   "ReadbackBuffer"});
    immediate([&](VkCommandBuffer cmd) {
        VkBufferCopy region{0, 0, buffer.size()};
        vkCmdCopyBuffer(cmd, buffer.handle(), readback.handle(), 1, &region);
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);
    });
    readback.invalidate();
    const auto* data = static_cast<const uint8_t*>(readback.mapped());
    return {data, data + buffer.size()};
}

std::vector<uint8_t> UploadContext::readImage(const GpuImage& image, VkImageLayout layout, uint32_t mip) {
    const uint32_t bpp = formatBytesPerPixel(image.format());
    RAVEN_ENSURE(bpp > 0, "readImage: unsupported format");
    RAVEN_ENSURE(mip < image.mipLevels(), "readImage: mip out of range");
    const uint32_t width = std::max(1u, image.extent().width >> mip);
    const uint32_t height = std::max(1u, image.extent().height >> mip);
    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * bpp;

    GpuBuffer readback(m_context, {size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::Readback, "ReadbackImage"});
    const VkImageAspectFlags aspect = isDepthFormat(image.format()) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    const VkImageSubresourceRange range{aspect, mip, 1, 0, 1};
    immediate([&](VkCommandBuffer cmd) {
        transitionImage(cmd, image.handle(), range, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {aspect, mip, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle(), 1,
                               &region);
        transitionImage(cmd, image.handle(), range, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &host;
        vkCmdPipelineBarrier2(cmd, &dep);
    });
    readback.invalidate();
    const auto* data = static_cast<const uint8_t*>(readback.mapped());
    return {data, data + size};
}

// ---------------------------------------------------------------------------------------
// UploadBatch: staging buffers are filled immediately; GPU commands are recorded as
// closures and replayed into one command buffer by submit().

UploadBatch::UploadBatch(UploadContext& upload) : m_upload(upload) {}

UploadBatch::~UploadBatch() {
    if (!m_submitted) {
        try {
            submit();
        } catch (const std::exception& e) {
            RAVEN_LOG_ERROR("UploadBatch submit in destructor failed: {}", e.what());
        }
    }
}

GpuBuffer UploadBatch::createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                    const char* debugName) {
    RAVEN_ENSURE(!m_submitted, "UploadBatch already submitted");
    const VulkanContext& context = m_upload.context();
    GpuBuffer staging(context, {size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload, "Staging"});
    staging.write(data, size);
    GpuBuffer target(context, {size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::GpuOnly, debugName});

    const VkBuffer src = staging.handle();
    const VkBuffer dst = target.handle(); // Handles survive moves of the owning objects.
    m_commands.push_back([src, dst, size](VkCommandBuffer cmd) {
        const VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    });
    m_staging.push_back(std::move(staging));
    return target;
}

void UploadBatch::uploadImage(const GpuImage& image, std::span<const ImageLevelData> levels) {
    RAVEN_ENSURE(!m_submitted, "UploadBatch already submitted");
    RAVEN_ENSURE(!levels.empty() && levels.size() <= image.mipLevels(), "uploadImage: bad level count");
    const uint32_t bpp = formatBytesPerPixel(image.format());
    RAVEN_ENSURE(bpp > 0, "uploadImage: unsupported format");

    VkDeviceSize total = 0;
    for (size_t i = 0; i < levels.size(); ++i) {
        const uint32_t expectedW = std::max(1u, image.extent().width >> i);
        const uint32_t expectedH = std::max(1u, image.extent().height >> i);
        RAVEN_ENSURE(levels[i].width == expectedW && levels[i].height == expectedH, "uploadImage: mip size mismatch");
        RAVEN_ENSURE(levels[i].bytes.size() == static_cast<size_t>(expectedW) * expectedH * bpp,
                     "uploadImage: mip byte size mismatch");
        total += (levels[i].bytes.size() + 15) & ~VkDeviceSize(15); // Keep each level 16-byte aligned.
    }

    GpuBuffer staging(m_upload.context(), {total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload, "Staging"});
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize offset = 0;
    for (size_t i = 0; i < levels.size(); ++i) {
        staging.write(levels[i].bytes.data(), levels[i].bytes.size(), offset);
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), 0, 1};
        region.imageExtent = {levels[i].width, levels[i].height, 1};
        regions.push_back(region);
        offset += (levels[i].bytes.size() + 15) & ~VkDeviceSize(15);
    }

    const VkBuffer src = staging.handle();
    const VkImage dst = image.handle();
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(levels.size()), 0, 1};
    m_commands.push_back([src, dst, range, regions = std::move(regions)](VkCommandBuffer cmd) {
        transitionImage(cmd, dst, range, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdCopyBufferToImage(cmd, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());
        transitionImage(cmd, dst, range, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    m_staging.push_back(std::move(staging));
}

void UploadBatch::submit() {
    RAVEN_ENSURE(!m_submitted, "UploadBatch already submitted");
    m_submitted = true;
    if (m_commands.empty()) {
        return;
    }
    m_upload.immediate([&](VkCommandBuffer cmd) {
        for (const auto& record : m_commands) {
            record(cmd);
        }
        // Buffer copies become visible to every later consumer (vertex input, shaders, copies).
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);
    });
    m_commands.clear();
    m_staging.clear();
}

} // namespace raven::gfx
