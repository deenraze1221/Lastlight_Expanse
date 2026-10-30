#pragma once

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace zodar::gfx {

class VulkanContext;

// GPU timestamp scopes per frame-in-flight slot. Results of a slot are read when the slot
// is reused (its previous frame has completed), so they lag by framesInFlight frames.
// Disabled (all calls are no-ops) when the device has no usable timestamps.
//
// Optional pipeline-statistics scopes (fragment shader invocations etc.) follow the same
// per-slot scheme; they are no-ops when the device lacks pipelineStatisticsQuery.
class GpuProfiler {
public:
    struct Scope {
        std::string name;
        int depth = 0;
        double ms = 0.0;
    };

    // Pipeline statistics of one scope (graphics stages only).
    struct Statistics {
        std::string name;
        uint64_t inputPrimitives = 0;
        uint64_t vertexInvocations = 0;
        uint64_t clippingPrimitives = 0; // Primitives that survived clipping/culling.
        uint64_t fragmentInvocations = 0;
    };

    GpuProfiler(const VulkanContext& context, uint32_t framesInFlight, uint32_t maxScopes = 256);
    ~GpuProfiler();

    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    bool enabled() const { return m_enabled; }

    // Start of a frame's command buffer: collects the slot's previous results, then resets.
    void beginFrame(VkCommandBuffer cmd, uint32_t slot);
    uint32_t beginScope(VkCommandBuffer cmd, const char* name);
    void endScope(VkCommandBuffer cmd, uint32_t scope);

    bool statisticsEnabled() const { return m_statsEnabled; }
    // Must begin and end within the same render pass instance (or both outside one).
    uint32_t beginStatistics(VkCommandBuffer cmd, const char* name);
    void endStatistics(VkCommandBuffer cmd, uint32_t scope);
    const std::vector<Statistics>& lastStatistics() const { return m_lastStats; }

    // Most recent completed frame.
    const std::vector<Scope>& lastResults() const { return m_last; }
    // Duration of the outermost scope(s) of the last completed frame.
    double lastFrameMs() const { return m_lastFrameMs; }
    uint64_t resultsFrameAge() const { return m_framesInFlight; }

private:
    struct Slot {
        VkQueryPool pool = VK_NULL_HANDLE;
        std::vector<Scope> scopes;
        uint32_t used = 0;
        VkQueryPool statsPool = VK_NULL_HANDLE;
        std::vector<Statistics> stats;
        bool statsWritten = false;
    };

    static constexpr uint32_t kMaxStatistics = 8;

    void collect(Slot& slot);

    VkDevice m_device;
    bool m_enabled = false;
    bool m_statsEnabled = false;
    double m_periodMs = 0.0;
    uint64_t m_mask = ~0ull;
    uint32_t m_maxScopes;
    uint32_t m_framesInFlight;
    std::vector<Slot> m_slots;
    Slot* m_current = nullptr;
    int m_depth = 0;
    std::vector<Scope> m_last;
    double m_lastFrameMs = 0.0;
    std::vector<Statistics> m_lastStats;
};

// Device memory use (VK_EXT_memory_budget when available).
struct GpuMemoryStats {
    uint64_t deviceLocalUsage = 0;  // Bytes used by this process in device-local heaps.
    uint64_t deviceLocalBudget = 0; // Bytes the process can use before trouble.
    uint64_t allocationBytes = 0;   // Bytes in VMA allocations (all heaps).
    uint32_t allocationCount = 0;
    bool budgetFromDriver = false;
};
GpuMemoryStats queryMemoryStats(const VulkanContext& context);

} // namespace zodar::gfx
