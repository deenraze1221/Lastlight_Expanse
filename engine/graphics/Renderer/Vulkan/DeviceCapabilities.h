#pragma once

#include <volk.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace zodar::gfx {

// Everything the engine needs to know about a physical device, gathered once.
// Required features form the engine baseline (any current desktop Vulkan 1.3 driver from
// NVIDIA, AMD or Intel, Mesa RADV/ANV, and lavapipe); everything else is optional and
// routed through ResolvedFeatures with an explicit fallback.
struct DeviceCapabilities {
    std::string name;
    uint32_t apiVersion = 0;          // Reported by the device (VkPhysicalDeviceProperties).
    uint32_t effectiveApiVersion = 0; // min(instance request, device), major.minor (ApiVersion.h)
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    std::array<uint8_t, VK_UUID_SIZE> pipelineCacheUUID{};

    // --- Engine baseline (all required) ---
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool descriptorIndexing = false;  // Runtime arrays, partially bound, update-after-bind, non-uniform indexing.
    bool timelineSemaphore = false;
    bool bufferDeviceAddress = false;
    bool drawIndirectCount = false;
    bool multiDrawIndirect = false;
    bool drawIndirectFirstInstance = false;
    bool shaderDrawParameters = false;

    // --- Optional ---
    bool samplerAnisotropy = false;
    float maxSamplerAnisotropy = 1.0f;
    bool textureCompressionBC = false;
    bool pipelineStatisticsQuery = false;
    bool hostQueryReset = false;
    bool shaderInt64 = false;
    bool memoryBudget = false;           // VK_EXT_memory_budget
    bool meshShader = false;             // VK_EXT_mesh_shader (mesh + task)
    bool accelerationStructure = false;  // VK_KHR_acceleration_structure
    bool rayTracingPipeline = false;     // VK_KHR_ray_tracing_pipeline
    bool rayQuery = false;               // VK_KHR_ray_query
    bool timestampQueries = false;       // Graphics queue has valid timestamp bits.
    float timestampPeriodNs = 0.0f;
    uint32_t timestampValidBits = 0;

    // --- Queues ---
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;
    uint32_t asyncComputeFamily = UINT32_MAX; // Compute without graphics.
    uint32_t transferFamily = UINT32_MAX;     // Transfer without graphics/compute.

    // --- Limits ---
    VkDeviceSize deviceLocalMemoryBytes = 0;
    uint32_t maxImageDimension2D = 0;
    uint32_t maxPushConstantsSize = 0;
    uint32_t maxDrawIndirectCount = 0;
    uint32_t maxComputeWorkGroupInvocations = 0;
    uint32_t maxUpdateAfterBindSampledImages = 0;
    uint32_t subgroupSize = 0;
    uint32_t maxShadowMapSize = 0; // Largest supported D32 2D attachment (== maxImageDimension2D).
};

// Baseline features that are missing (empty = device is usable).
std::vector<std::string> missingBaseline(const DeviceCapabilities& caps, bool needsPresentation);

// Optional engine features after capability checks. Each disabled feature has a fallback.
struct ResolvedFeatures {
    struct Entry {
        const char* name;
        bool enabled;
        const char* fallback; // What the engine uses instead when disabled.
    };

    bool anisotropicFiltering = false;
    bool bcTextureCompression = false;
    bool gpuTimestamps = false;
    bool pipelineStatistics = false;
    bool memoryBudget = false;
    bool asyncCompute = false;
    bool dedicatedTransfer = false;
    bool meshShaders = false;
    bool rayTracing = false;
    // Effective API version >= 1.4. Detected only: no 1.4 functionality is enabled or used
    // yet, every path runs on Vulkan 1.3 core.
    bool vulkan14 = false;

    std::vector<Entry> entries() const;
};

// Pure policy: decides which optional paths are used. No Vulkan calls (unit tested).
ResolvedFeatures resolveFeatures(const DeviceCapabilities& caps);

// Queries a physical device. `surface` may be VK_NULL_HANDLE (headless). `instanceApiVersion`
// is the VkApplicationInfo::apiVersion the instance was created with.
DeviceCapabilities queryCapabilities(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t instanceApiVersion);

} // namespace zodar::gfx
