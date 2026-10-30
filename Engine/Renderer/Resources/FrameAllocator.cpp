#include "Renderer/Resources/FrameAllocator.h"

#include "Core/Error.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace zodar::gfx {

namespace {

constexpr VkBufferUsageFlags kUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

} // namespace

FrameAllocator::FrameAllocator(const VulkanContext& context, uint32_t framesInFlight, VkDeviceSize blockSize)
    : m_context(context), m_blockSize(blockSize), m_slots(framesInFlight) {
    ZODAR_ENSURE(framesInFlight > 0 && blockSize > 0, "invalid FrameAllocator configuration");
}

void FrameAllocator::beginFrame(uint32_t slot) {
    ZODAR_ENSURE(slot < m_slots.size(), "FrameAllocator slot out of range");
    m_slot = slot;
    Slot& s = m_slots[slot];
    for (auto& block : s.blocks) {
        block.used = 0;
    }
    s.current = 0;
}

FrameAllocator::Allocation FrameAllocator::allocate(VkDeviceSize size, VkDeviceSize alignment) {
    ZODAR_ENSURE(size > 0, "zero-sized frame allocation");
    ZODAR_ENSURE(std::has_single_bit(alignment), "alignment must be a power of two");
    Slot& s = m_slots[m_slot];
    while (true) {
        if (s.current == s.blocks.size()) {
            Block block;
            block.buffer = GpuBuffer(m_context, {std::max(m_blockSize, size + alignment), kUsage, MemoryUsage::Upload,
                                                 "FrameScratch"});
            s.blocks.push_back(std::move(block));
        }
        Block& block = s.blocks[s.current];
        const VkDeviceSize offset = (block.used + alignment - 1) & ~(alignment - 1);
        if (offset + size <= block.buffer.size()) {
            block.used = offset + size;
            Allocation a;
            a.buffer = block.buffer.handle();
            a.offset = offset;
            a.size = size;
            a.data = static_cast<uint8_t*>(block.buffer.mapped()) + offset;
            a.address = block.buffer.deviceAddress() + offset;
            return a;
        }
        ++s.current; // Block full: continue in the next one (allocated on demand).
    }
}

void FrameAllocator::write(const Allocation& allocation, const void* data, VkDeviceSize size, VkDeviceSize offset) {
    ZODAR_ENSURE(offset + size <= allocation.size, "frame allocation write out of range");
    const auto* dst = static_cast<const uint8_t*>(allocation.data) + offset;
    const auto* src = static_cast<const uint8_t*>(data);
    ZODAR_ENSURE(src + size <= dst || dst + size <= src, "write() source overlaps the allocation; use flush()");
    for (auto& block : m_slots[m_slot].blocks) {
        if (block.buffer.handle() == allocation.buffer) {
            block.buffer.write(data, size, allocation.offset + offset);
            return;
        }
    }
    fail("write() to an allocation that is not from the current frame");
}

void FrameAllocator::flush(const Allocation& allocation) {
    for (auto& block : m_slots[m_slot].blocks) {
        if (block.buffer.handle() == allocation.buffer) {
            block.buffer.flush(allocation.offset, allocation.size);
            return;
        }
    }
    fail("flush() of an allocation that is not from the current frame");
}

VkDeviceSize FrameAllocator::bytesUsed() const {
    VkDeviceSize total = 0;
    for (const auto& block : m_slots[m_slot].blocks) {
        total += block.used;
    }
    return total;
}

VkDeviceSize FrameAllocator::bytesReserved() const {
    VkDeviceSize total = 0;
    for (const auto& slot : m_slots) {
        for (const auto& block : slot.blocks) {
            total += block.buffer.size();
        }
    }
    return total;
}

} // namespace zodar::gfx
