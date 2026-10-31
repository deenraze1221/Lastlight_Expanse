#include "Renderer/GpuProfiler.h"

#include "Core/Error.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <vk_mem_alloc.h>

namespace zodar::gfx {
namespace {

constexpr VkQueryPipelineStatisticFlags kStatistics =
    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
constexpr uint32_t kStatisticCount = 4; // Results come in bit order.

} // namespace

GpuProfiler::GpuProfiler(const VulkanContext& context, uint32_t framesInFlight, uint32_t maxScopes)
    : m_device(context.device()), m_maxScopes(maxScopes), m_framesInFlight(framesInFlight) {
    const auto& caps = context.capabilities();
    m_enabled = context.features().gpuTimestamps;
    if (!m_enabled) {
        return;
    }
    // Statistics share the frame bookkeeping (beginFrame) with the timestamps.
    m_statsEnabled = context.features().pipelineStatistics;
    m_periodMs = static_cast<double>(caps.timestampPeriodNs) * 1e-6;
    m_mask = caps.timestampValidBits >= 64 ? ~0ull : ((1ull << caps.timestampValidBits) - 1);
    m_slots.resize(framesInFlight);
    for (auto& slot : m_slots) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = maxScopes * 2;
        ZODAR_VK_CHECK(vkCreateQueryPool(m_device, &info, nullptr, &slot.pool));
        if (m_statsEnabled) {
            VkQueryPoolCreateInfo stats{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            stats.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
            stats.queryCount = kMaxStatistics;
            stats.pipelineStatistics = kStatistics;
            ZODAR_VK_CHECK(vkCreateQueryPool(m_device, &stats, nullptr, &slot.statsPool));
        }
    }
}

GpuProfiler::~GpuProfiler() {
    for (auto& slot : m_slots) {
        vkDestroyQueryPool(m_device, slot.pool, nullptr);
        vkDestroyQueryPool(m_device, slot.statsPool, nullptr);
    }
}

void GpuProfiler::collect(Slot& slot) {
    if (slot.statsWritten) {
        const auto count = static_cast<uint32_t>(slot.stats.size());
        constexpr uint32_t stride = kStatisticCount + 1; // + availability
        std::vector<uint64_t> data(static_cast<size_t>(count) * stride);
        const VkResult result = vkGetQueryPoolResults(
            m_device, slot.statsPool, 0, count, data.size() * sizeof(uint64_t), data.data(), stride * sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result != VK_SUCCESS && result != VK_NOT_READY) {
            throwVkError(result, "vkGetQueryPoolResults (statistics)");
        }
        m_lastStats.clear();
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t* q = &data[size_t(i) * stride];
            if (q[kStatisticCount] == 0) continue; // Not available: skip rather than report zeros.
            Statistics s = slot.stats[i];
            s.inputPrimitives = q[0];
            s.vertexInvocations = q[1];
            s.clippingPrimitives = q[2];
            s.fragmentInvocations = q[3];
            m_lastStats.push_back(std::move(s));
        }
    }
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
    ZODAR_ENSURE(slotIndex < m_slots.size(), "GpuProfiler slot out of range");
    Slot& slot = m_slots[slotIndex];
    collect(slot);
    slot.scopes.clear();
    slot.used = 0;
    vkCmdResetQueryPool(cmd, slot.pool, 0, m_maxScopes * 2);
    slot.stats.clear();
    slot.statsWritten = false;
    if (m_statsEnabled) {
        vkCmdResetQueryPool(cmd, slot.statsPool, 0, kMaxStatistics);
    }
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

uint32_t GpuProfiler::beginStatistics(VkCommandBuffer cmd, const char* name) {
    if (!m_statsEnabled || !m_current || m_current->stats.size() >= kMaxStatistics) {
        return UINT32_MAX;
    }
    const auto scope = static_cast<uint32_t>(m_current->stats.size());
    m_current->stats.push_back({name});
    vkCmdBeginQuery(cmd, m_current->statsPool, scope, 0);
    return scope;
}

void GpuProfiler::endStatistics(VkCommandBuffer cmd, uint32_t scope) {
    if (scope == UINT32_MAX || !m_current) {
        return;
    }
    vkCmdEndQuery(cmd, m_current->statsPool, scope);
    m_current->statsWritten = true;
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

} // namespace zodar::gfx
