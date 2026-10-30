#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <span>

namespace zodar::gfx {

class VulkanContext;

enum class MemoryUsage {
    GpuOnly,  // Device-local; filled through staging uploads.
    Upload,   // Host-visible, persistently mapped, written sequentially by the CPU.
    Readback, // Host-visible, persistently mapped, read by the CPU.
};

struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    MemoryUsage memory = MemoryUsage::GpuOnly;
    const char* debugName = nullptr;
    // The buffer is written by the upload (transfer) queue while the graphics queue may use
    // it, e.g. append-only pools. Uses VK_SHARING_MODE_CONCURRENT across the graphics and
    // transfer families (no ownership transfers); ignored when both are the same family.
    bool sharedWithTransfer = false;
};

// Move-only owner of a VkBuffer and its VMA allocation.
class GpuBuffer {
public:
    GpuBuffer() = default;
    GpuBuffer(const VulkanContext& context, const BufferDesc& desc);
    ~GpuBuffer();

    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    void reset();
    bool isValid() const { return m_buffer != VK_NULL_HANDLE; }

    VkBuffer handle() const { return m_buffer; }
    VkDeviceSize size() const { return m_size; }
    // Non-null for Upload and Readback buffers.
    void* mapped() const { return m_mapped; }
    // GPU address (buffers created with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT), else 0.
    VkDeviceAddress deviceAddress() const { return m_address; }
    // True when created with sharedWithTransfer across two queue families.
    bool isConcurrent() const { return m_concurrent; }

    // Copies into a mapped buffer and flushes non-coherent memory.
    void write(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    template <typename T>
    void write(std::span<const T> items, VkDeviceSize offset = 0) {
        write(items.data(), items.size_bytes(), offset);
    }
    // Flushes host writes made directly through mapped() (no-op for coherent memory).
    void flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    // Makes GPU writes visible to the host before reading mapped().
    void invalidate();

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    void* m_mapped = nullptr;
    VkDeviceAddress m_address = 0;
    bool m_concurrent = false;
};

} // namespace zodar::gfx
