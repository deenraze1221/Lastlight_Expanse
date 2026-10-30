#include "Renderer/Resources/GpuBuffer.h"

#include "Core/Error.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <cstring>
#include <utility>

namespace raven::gfx {

GpuBuffer::GpuBuffer(const VulkanContext& context, const BufferDesc& desc)
    : m_allocator(context.allocator()), m_size(desc.size) {
    RAVEN_ENSURE(desc.size > 0, "buffer size must be non-zero");

    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = desc.size;
    info.usage = desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    switch (desc.memory) {
    case MemoryUsage::GpuOnly:
        alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryUsage::Upload:
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryUsage::Readback:
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }

    VmaAllocationInfo allocInfo{};
    RAVEN_VK_CHECK(vmaCreateBuffer(m_allocator, &info, &alloc, &m_buffer, &m_allocation, &allocInfo));
    m_mapped = allocInfo.pMappedData;
    if (desc.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = m_buffer;
        m_address = vkGetBufferDeviceAddress(context.device(), &addressInfo);
    }
    context.setDebugName(m_buffer, VK_OBJECT_TYPE_BUFFER, desc.debugName);
    if (desc.debugName) {
        vmaSetAllocationName(m_allocator, m_allocation, desc.debugName);
    }
}

GpuBuffer::~GpuBuffer() {
    reset();
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
      m_buffer(std::exchange(other.m_buffer, VK_NULL_HANDLE)),
      m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE)),
      m_size(std::exchange(other.m_size, 0)),
      m_mapped(std::exchange(other.m_mapped, nullptr)),
      m_address(std::exchange(other.m_address, 0)) {}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_size = std::exchange(other.m_size, 0);
        m_mapped = std::exchange(other.m_mapped, nullptr);
        m_address = std::exchange(other.m_address, 0);
    }
    return *this;
}

void GpuBuffer::reset() {
    if (m_buffer) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
    m_buffer = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_size = 0;
    m_mapped = nullptr;
    m_address = 0;
}

void GpuBuffer::write(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    RAVEN_ENSURE(m_mapped != nullptr, "write() needs a host-visible buffer");
    RAVEN_ENSURE(offset + size <= m_size, "buffer write out of range");
    std::memcpy(static_cast<uint8_t*>(m_mapped) + offset, data, static_cast<size_t>(size));
    RAVEN_VK_CHECK(vmaFlushAllocation(m_allocator, m_allocation, offset, size));
}

void GpuBuffer::invalidate() {
    RAVEN_VK_CHECK(vmaInvalidateAllocation(m_allocator, m_allocation, 0, VK_WHOLE_SIZE));
}

} // namespace raven::gfx
