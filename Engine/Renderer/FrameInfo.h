#pragma once

#include <volk.h>

#include <cstdint>

namespace raven::gfx {

class FrameAllocator;
class GpuProfiler;

// Per-frame recording context handed to renderers and render-graph passes.
struct FrameInfo {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    uint32_t slot = 0;                // Frame-in-flight index whose previous use has completed.
    uint64_t frameNumber = 0;         // Monotonic frame counter.
    FrameAllocator* scratch = nullptr; // Per-frame CPU->GPU memory (required by scene rendering).
    GpuProfiler* profiler = nullptr;   // Optional GPU timestamps.
};

} // namespace raven::gfx
