#include "Renderer/GpuProfiler.h"

#include "Core/Error.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <vk_mem_alloc.h>

namespace raven::gfx {

GpuProfiler::GpuProfiler(const VulkanContext& context, uint32_t framesInFlight, uint32_t maxScopes)
    : m_device(context.device()), m_maxScopes(maxScopes), m_framesInFlight(framesInFlight) {
    const auto& caps = context.capabilities();
    m_enabled = context.features().gpuTimestamps;
    if (!m_enabled) {
        return;
    }
    m_periodMs = static_cast<double>(caps.timestampPeriodNs) * 1e-6;
    m_mask = caps.timestampValidBits >= 64 ? ~0ull : ((1ull << caps.timestampValidBits) - 1);
    m_slots.resize(framesInFlight);
    for (auto& slot : m_slots) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = maxScopes * 2;
        RAVEN_VK_CHECK(vkCreateQueryPool(m_device, &info, nullptr, &slot.pool));
    }
}

GpuProfiler::~GpuProfiler() {
    for (auto& slot : m_slots) {
        vkDestroyQueryPool(m_device, slot.pool, nullptr);
    }
}

void GpuProfiler::collect(Slot& slot) {
    if (slot.used == 0) {
        return;
    }
    std::vector<uint64_t> data(static_cast<size_t>(slot.used) * 2 * 2); // value + availability per query
    const VkResult result =
        vkGetQueryPoolResults(m_device, slot.pool, 0, slot.used * 2, data.size() * sizeof(uint64_t), data.data(),
                              2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS && result != VK_NOT_READY) {
        throwVkError(result, "vkGetQueryPoolResults");
    }
    m_last.clear();
    m_lastFrameMs = 0.0;
    for (uint32_t i = 0; i < slot.used; ++i) {
        const uint64_t begin = data[i * 4 + 0] & m_mask;
        const uint64_t end = data[i * 4 + 2] & m_mask;
        const bool available = data[i * 4 + 1] != 0 && data[i * 4 + 3] != 0;
        Scope scope = slot.scopes[i];
        scope.ms = available && end >= begin ? static_cast<double>(end - begin) * m_periodMs : 0.0;
        if (scope.depth == 0) {
            m_lastFrameMs += scope.ms;
        }
        m_last.push_back(std::move(scope));
    }
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd, uint32_t slotIndex) {
    if (!m_enabled) {
        return;
    }
    RAVEN_ENSURE(slotIndex < m_slots.size(), "GpuProfiler slot out of range");
    Slot& slot = m_slots[slotIndex];
    collect(slot);
    slot.scopes.clear();
    slot.used = 0;
    vkCmdResetQueryPool(cmd, slot.pool, 0, m_maxScopes * 2);
    m_current = &slot;
    m_depth = 0;
}

uint32_t GpuProfiler::beginScope(VkCommandBuffer cmd, const char* name) {
    if (!m_enabled || !m_current || m_current->used >= m_maxScopes) {
        return UINT32_MAX;
    }
    const uint32_t scope = m_current->used++;
    m_current->scopes.push_back({name, m_depth++, 0.0});
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_current->pool, scope * 2);
    return scope;
}

void GpuProfiler::endScope(VkCommandBuffer cmd, uint32_t scope) {
    if (scope == UINT32_MAX || !m_current) {
        return;
    }
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_current->pool, scope * 2 + 1);
    --m_depth;
}

GpuMemoryStats queryMemoryStats(const VulkanContext& context) {
    GpuMemoryStats stats;
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(context.allocator(), budgets);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(context.physicalDevice(), &memory);
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        stats.allocationBytes += budgets[i].statistics.allocationBytes;
        stats.allocationCount += budgets[i].statistics.allocationCount;
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            stats.deviceLocalUsage += budgets[i].usage;
            stats.deviceLocalBudget += budgets[i].budget;
        }
    }
    stats.budgetFromDriver = context.features().memoryBudget;
    return stats;
}

} // namespace raven::gfx
