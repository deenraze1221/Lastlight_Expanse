#include "Renderer/Resources/UploadContext.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <cstring>

namespace zodar::gfx {

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

FormatBlock formatBlock(VkFormat format) {
    switch (format) {
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
        return {4, 4, 8};
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return {4, 4, 16};
    default:
        return {1, 1, formatBytesPerPixel(format)};
    }
}

VkDeviceSize imageLevelSize(VkFormat format, uint32_t width, uint32_t height) {
    const FormatBlock b = formatBlock(format);
    return VkDeviceSize((width + b.width - 1) / b.width) * ((height + b.height - 1) / b.height) * b.bytes;
}

UploadContext::UploadContext(VulkanContext& context)
    : m_context(context), m_queue(std::make_unique<UploadQueue>(context)) {
    const VkDevice device = context.device();
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = context.graphicsQueueFamily();
    ZODAR_VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_pool));

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &m_cmd);
    if (result == VK_SUCCESS) {
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
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
    ZODAR_ENSURE(!m_recording, "UploadContext is already recording (nested batch?)");
    const VkDevice device = m_context.device();
    ZODAR_VK_CHECK(vkResetCommandPool(device, m_pool, 0));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ZODAR_VK_CHECK(vkBeginCommandBuffer(m_cmd, &begin));
    m_recording = true;
    try {
        record(m_cmd);
    } catch (...) {
        vkEndCommandBuffer(m_cmd);
        m_recording = false;
        throw;
    }
    m_recording = false;
    ZODAR_VK_CHECK(vkEndCommandBuffer(m_cmd));

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = m_cmd;
    GraphicsSubmit submit;
    submit.commandBuffers = std::span(&cmdInfo, 1);
    // The blocking wait uses a fence: equivalent to waiting on the timeline value, but
    // also understood by older synchronization-validation layers (1.3.275 does not model
    // vkWaitSemaphores host waits and reports false WAW hazards on reused readback memory).
    submit.fence = m_fence;
    m_context.submitGraphics(submit);
    ZODAR_VK_CHECK(vkWaitForFences(device, 1, &m_fence, VK_TRUE, UINT64_MAX));
    ZODAR_VK_CHECK(vkResetFences(device, 1, &m_fence));
    m_context.collectGarbage();
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

std::vector<uint8_t> UploadContext::readImage(const GpuImage& image, VkImageLayout layout, uint32_t mip, uint32_t layer) {
    ZODAR_ENSURE(layer < image.layers(), "readImage: layer out of range");
    ZODAR_ENSURE(formatBlock(image.format()).bytes > 0, "readImage: unsupported format");
    ZODAR_ENSURE(mip < image.mipLevels(), "readImage: mip out of range");
    const uint32_t width = std::max(1u, image.extent().width >> mip);
    const uint32_t height = std::max(1u, image.extent().height >> mip);
    const VkDeviceSize size = imageLevelSize(image.format(), width, height); // Raw blocks for BCn.

    GpuBuffer readback(m_context, {size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::Readback, "ReadbackImage"});
    const VkImageAspectFlags aspect = isDepthFormat(image.format()) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    const VkImageSubresourceRange range{aspect, mip, 1, layer, 1};
    immediate([&](VkCommandBuffer cmd) {
        transitionImage(cmd, image.handle(), range, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {aspect, mip, layer, 1};
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
            ZODAR_LOG_ERROR("UploadBatch submit in destructor failed: {}", e.what());
        }
    }
}

GpuBuffer& UploadBatch::stage(const void* data, VkDeviceSize size) {
    ZODAR_ENSURE(!m_submitted, "UploadBatch already submitted");
    GpuBuffer staging(m_upload.context(), {size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload, "Staging"});
    if (data) staging.write(data, size);
    m_work.stagingBytes += size;
    m_work.staging.push_back(std::move(staging));
    return m_work.staging.back();
}

GpuBuffer UploadBatch::createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                    const char* debugName) {
    const VkBuffer src = stage(data, size).handle();
    GpuBuffer target(m_upload.context(), {size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::GpuOnly, debugName});
    const VkBuffer dst = target.handle(); // Handles survive moves of the owning objects.
    m_work.commands.push_back([src, dst, size](VkCommandBuffer cmd) {
        const VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    });
    m_work.exclusiveBuffers.push_back(dst); // Handed to the graphics family after the copy.
    return target;
}

void UploadBatch::copyToBuffer(const void* data, VkDeviceSize size, const GpuBuffer& target, VkDeviceSize offset) {
    ZODAR_ENSURE(size > 0, "copyToBuffer with no data");
    const VkBuffer dst = target.handle();
    const bool ownBuffer = std::find(m_work.exclusiveBuffers.begin(), m_work.exclusiveBuffers.end(), dst) !=
                           m_work.exclusiveBuffers.end();
    ZODAR_ENSURE(!m_upload.queue().dedicated() || target.isConcurrent() || ownBuffer,
                 "copyToBuffer: with a dedicated transfer queue the target needs BufferDesc::sharedWithTransfer");
    const VkBuffer src = stage(data, size).handle();
    m_work.commands.push_back([src, dst, offset, size](VkCommandBuffer cmd) {
        const VkBufferCopy region{0, offset, size};
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    });
}

void UploadBatch::record(std::function<void(VkCommandBuffer)> commands) {
    ZODAR_ENSURE(!m_submitted, "UploadBatch already submitted");
    m_work.commands.push_back(std::move(commands));
}

void UploadBatch::keepAlive(std::shared_ptr<void> object) {
    ZODAR_ENSURE(!m_submitted, "UploadBatch already submitted");
    m_work.keepAlive.push_back(std::move(object));
}

void UploadBatch::uploadImage(const GpuImage& image, std::span<const ImageLevelData> levels) {
    ZODAR_ENSURE(!m_submitted, "UploadBatch already submitted");
    ZODAR_ENSURE(!levels.empty() && levels.size() <= image.mipLevels(), "uploadImage: bad level count");
    ZODAR_ENSURE(formatBlock(image.format()).bytes > 0, "uploadImage: unsupported format");

    VkDeviceSize total = 0;
    for (size_t i = 0; i < levels.size(); ++i) {
        const uint32_t expectedW = std::max(1u, image.extent().width >> i);
        const uint32_t expectedH = std::max(1u, image.extent().height >> i);
        ZODAR_ENSURE(levels[i].width == expectedW && levels[i].height == expectedH, "uploadImage: mip size mismatch");
        ZODAR_ENSURE(levels[i].bytes.size() == imageLevelSize(image.format(), expectedW, expectedH),
                     "uploadImage: mip byte size mismatch");
        total += (levels[i].bytes.size() + 15) & ~VkDeviceSize(15); // Keep each level 16-byte aligned.
    }

    GpuBuffer& staging = stage(nullptr, total);
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
    m_work.commands.push_back([src, dst, range, regions = std::move(regions)](VkCommandBuffer cmd) {
        transitionImage(cmd, dst, range, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdCopyBufferToImage(cmd, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());
    });
    // The final layout transition (and queue-family release/acquire) is done by the queue.
    m_work.images.push_back({dst, range, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
}

UploadTicket UploadBatch::submitAsync() {
    ZODAR_ENSURE(!m_submitted, "UploadBatch already submitted");
    m_submitted = true;
    if (m_work.commands.empty()) {
        return {};
    }
    return m_upload.queue().submit(std::move(m_work));
}

void UploadBatch::submit() {
    const UploadTicket ticket = submitAsync();
    if (ticket.valid()) {
        m_upload.queue().wait(ticket);
    }
}

} // namespace zodar::gfx
