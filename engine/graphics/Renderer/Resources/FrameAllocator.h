#pragma once

#include "Renderer/Resources/GpuBuffer.h"

#include <volk.h>

#include <cstdint>
#include <span>
#include <vector>

namespace zodar::gfx {

class VulkanContext;

// Linear allocator for data written by the CPU and read by the GPU during one frame
// (constants, instance data, indirect arguments). Each frame-in-flight slot owns its
// blocks; beginFrame(slot) recycles them, so the caller must only call it after the
// slot's previous frame completed (the frame loop guarantees this).
// Memory is host-visible and device-addressable (shaders read it via buffer references).
class FrameAllocator {
public:
    static constexpr VkDeviceSize kDefaultBlockSize = 4ull << 20;
    static constexpr VkDeviceSize kDefaultAlignment = 256;

    struct Allocation {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        void* data = nullptr;
        VkDeviceAddress address = 0;
    };

    FrameAllocator(const VulkanContext& context, uint32_t framesInFlight, VkDeviceSize blockSize = kDefaultBlockSize);

    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    void beginFrame(uint32_t slot);
    Allocation allocate(VkDeviceSize size, VkDeviceSize alignment = kDefaultAlignment);

    template <typename T>
    Allocation upload(std::span<const T> items, VkDeviceSize alignment = kDefaultAlignment) {
        Allocation a = allocate(std::max<VkDeviceSize>(items.size_bytes(), 1), alignment);
        if (!items.empty()) {
            write(a, items.data(), items.size_bytes());
        }
        return a;
    }
    template <typename T>
    Allocation upload(const T& value, VkDeviceSize alignment = kDefaultAlignment) {
        return upload(std::span<const T>(&value, 1), alignment);
    }

    // Copies into an allocation and flushes non-coherent memory. `data` must not point into
    // the allocation itself; for data written in place through Allocation::data use flush().
    void write(const Allocation& allocation, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    // Flushes data written in place through Allocation::data.
    void flush(const Allocation& allocation);

    VkDeviceSize bytesUsed() const;     // Current slot.
    VkDeviceSize bytesReserved() const; // All slots.
    uint32_t currentSlot() const { return m_slot; }

private:
    struct Block {
        GpuBuffer buffer;
        VkDeviceSize used = 0;
    };
    struct Slot {
        std::vector<Block> blocks;
        size_t current = 0;
    };

    const VulkanContext& m_context;
    VkDeviceSize m_blockSize;
    std::vector<Slot> m_slots;
    uint32_t m_slot = 0;
};

} // namespace zodar::gfx
