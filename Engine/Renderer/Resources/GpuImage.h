#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace zodar::gfx {

class VulkanContext;

struct ImageDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    uint32_t mipLevels = 1;
    const char* debugName = nullptr;
};

// Move-only owner of a 2D VkImage, its allocation and a view covering all mips.
// The aspect (colour or depth/stencil) is derived from the format.
class GpuImage {
public:
    GpuImage() = default;
    GpuImage(const VulkanContext& context, const ImageDesc& desc);
    ~GpuImage();

    GpuImage(GpuImage&& other) noexcept;
    GpuImage& operator=(GpuImage&& other) noexcept;
    GpuImage(const GpuImage&) = delete;
    GpuImage& operator=(const GpuImage&) = delete;

    void reset();
    bool isValid() const { return m_image != VK_NULL_HANDLE; }

    VkImage handle() const { return m_image; }
    VkImageView view() const { return m_view; }
    VkFormat format() const { return m_format; }
    VkExtent2D extent() const { return m_extent; }
    uint32_t mipLevels() const { return m_mipLevels; }
    VkImageAspectFlags aspect() const { return m_aspect; }
    VkImageSubresourceRange fullRange() const { return {m_aspect, 0, m_mipLevels, 0, 1}; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    uint32_t m_mipLevels = 0;
    VkImageAspectFlags m_aspect = 0;
};

bool isDepthFormat(VkFormat format);
bool hasStencil(VkFormat format);
// Number of mips in a full chain down to 1x1.
uint32_t fullMipCount(uint32_t width, uint32_t height);

// Records a whole-image layout transition with explicit synchronization2 scopes.
void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange range, VkImageLayout from,
                     VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

} // namespace zodar::gfx
