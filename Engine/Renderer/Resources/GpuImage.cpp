#include "Renderer/Resources/GpuImage.h"

#include "Core/Error.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace raven::gfx {

bool isDepthFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

bool hasStencil(VkFormat format) {
    return format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

uint32_t fullMipCount(uint32_t width, uint32_t height) {
    const uint32_t largest = std::max(1u, std::max(width, height));
    return static_cast<uint32_t>(std::bit_width(largest));
}

GpuImage::GpuImage(const VulkanContext& context, const ImageDesc& desc)
    : m_device(context.device()), m_allocator(context.allocator()), m_format(desc.format),
      m_extent{desc.width, desc.height}, m_mipLevels(desc.mipLevels) {
    RAVEN_ENSURE(desc.width > 0 && desc.height > 0, "image extent must be non-zero");
    RAVEN_ENSURE(desc.mipLevels >= 1 && desc.mipLevels <= fullMipCount(desc.width, desc.height),
                 "invalid mip count");

    m_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (isDepthFormat(desc.format)) {
        m_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencil(desc.format)) {
            m_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }

    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = desc.format;
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = desc.mipLevels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    // Render targets get their own allocation: they are large and may be recreated on resize.
    if (desc.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        alloc.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    RAVEN_VK_CHECK(vmaCreateImage(m_allocator, &info, &alloc, &m_image, &m_allocation, nullptr));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = desc.format;
    // Sampled depth views read the depth aspect only.
    viewInfo.subresourceRange = {isDepthFormat(desc.format) ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : m_aspect,
                                 0, desc.mipLevels, 0, 1};
    const VkResult result = vkCreateImageView(m_device, &viewInfo, nullptr, &m_view);
    if (result != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        throwVkError(result, "vkCreateImageView");
    }

    context.setDebugName(m_image, VK_OBJECT_TYPE_IMAGE, desc.debugName);
    context.setDebugName(m_view, VK_OBJECT_TYPE_IMAGE_VIEW, desc.debugName);
    if (desc.debugName) {
        vmaSetAllocationName(m_allocator, m_allocation, desc.debugName);
    }
}

GpuImage::~GpuImage() {
    reset();
}

GpuImage::GpuImage(GpuImage&& other) noexcept {
    *this = std::move(other);
}

GpuImage& GpuImage::operator=(GpuImage&& other) noexcept {
    if (this != &other) {
        reset();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_format = std::exchange(other.m_format, VK_FORMAT_UNDEFINED);
        m_extent = std::exchange(other.m_extent, {});
        m_mipLevels = std::exchange(other.m_mipLevels, 0);
        m_aspect = std::exchange(other.m_aspect, 0);
    }
    return *this;
}

void GpuImage::reset() {
    if (m_view) {
        vkDestroyImageView(m_device, m_view, nullptr);
    }
    if (m_image) {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    }
    m_view = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
}

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange range, VkImageLayout from,
                     VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace raven::gfx
