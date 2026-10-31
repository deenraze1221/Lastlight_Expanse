#include "Renderer/Vulkan/DeviceCapabilities.h"

#include "Renderer/Vulkan/ApiVersion.h"

#include <algorithm>
#include <cstring>

namespace zodar::gfx {

std::vector<std::string> missingBaseline(const DeviceCapabilities& c, bool needsPresentation) {
    std::vector<std::string> missing;
    const auto require = [&](bool ok, const char* what) {
        if (!ok) missing.emplace_back(what);
    };
    require(c.apiVersion >= VK_API_VERSION_1_3, "Vulkan 1.3");
    require(c.graphicsFamily != UINT32_MAX, "graphics queue");
    if (needsPresentation) {
        require(c.presentFamily != UINT32_MAX, "presentation support");
    }
    require(c.dynamicRendering, "dynamicRendering");
    require(c.synchronization2, "synchronization2");
    require(c.descriptorIndexing, "descriptor indexing");
    require(c.timelineSemaphore, "timelineSemaphore");
    require(c.bufferDeviceAddress, "bufferDeviceAddress");
    require(c.drawIndirectCount, "drawIndirectCount");
    require(c.multiDrawIndirect, "multiDrawIndirect");
    require(c.drawIndirectFirstInstance, "drawIndirectFirstInstance");
    require(c.shaderDrawParameters, "shaderDrawParameters");
    return missing;
}

ResolvedFeatures resolveFeatures(const DeviceCapabilities& c) {
    ResolvedFeatures f;
    f.anisotropicFiltering = c.samplerAnisotropy && c.maxSamplerAnisotropy > 1.0f;
    f.bcTextureCompression = c.textureCompressionBC;
    f.gpuTimestamps = c.timestampQueries && c.timestampPeriodNs > 0.0f;
    f.pipelineStatistics = c.pipelineStatisticsQuery;
    f.memoryBudget = c.memoryBudget;
    f.asyncCompute = c.asyncComputeFamily != UINT32_MAX;
    f.dedicatedTransfer = c.transferFamily != UINT32_MAX;
    f.meshShaders = c.meshShader;
    f.rayTracing = c.accelerationStructure && (c.rayQuery || c.rayTracingPipeline);
    f.vulkan14 = c.effectiveApiVersion >= VK_API_VERSION_1_4;
    return f;
}

std::vector<ResolvedFeatures::Entry> ResolvedFeatures::entries() const {
    return {
        {"Anisotropic filtering", anisotropicFiltering, "trilinear filtering"},
        {"BC texture compression", bcTextureCompression, "decode to RGBA8 at load"},
        {"GPU timestamps", gpuTimestamps, "CPU timers only"},
        {"Pipeline statistics", pipelineStatistics, "CPU-side triangle counts"},
        {"Memory budget", memoryBudget, "heap sizes as budget"},
        {"Async compute queue", asyncCompute, "graphics queue"},
        {"Dedicated transfer queue", dedicatedTransfer, "graphics queue uploads"},
        {"Mesh shaders (detected, not used yet)", meshShaders, "vertex pipeline + indirect draws"},
        {"Ray tracing (detected, not used yet)", rayTracing, "probes and screen-space techniques"},
        {"Vulkan 1.4 core (detected, not used yet)", vulkan14, "Vulkan 1.3 core"},
    };
}

namespace {

bool hasExtension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    return std::any_of(exts.begin(), exts.end(),
                       [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

} // namespace

DeviceCapabilities queryCapabilities(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t instanceApiVersion) {
    DeviceCapabilities c;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());

    VkPhysicalDeviceProperties base;
    vkGetPhysicalDeviceProperties(device, &base);
    c.name = base.deviceName;
    c.apiVersion = base.apiVersion;
    c.effectiveApiVersion = effectiveApiVersion(instanceApiVersion, base.apiVersion);
    c.vendorId = base.vendorID;
    c.deviceId = base.deviceID;
    c.type = base.deviceType;
    std::copy(std::begin(base.pipelineCacheUUID), std::end(base.pipelineCacheUUID), c.pipelineCacheUUID.begin());
    if (c.apiVersion < VK_API_VERSION_1_3) {
        return c; // The 1.1-1.3 feature structs below may not be queried on older devices.
    }

    // Features: core 1.1-1.3 plus optional extensions (chained only when supported).
    VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &f11;
    f11.pNext = &f12;
    f12.pNext = &f13;
    void** tail = &f13.pNext;
    const bool hasMesh = hasExtension(exts, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    const bool hasAccel = hasExtension(exts, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                          hasExtension(exts, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    const bool hasRtPipeline = hasExtension(exts, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    const bool hasRayQuery = hasExtension(exts, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    if (hasMesh) { *tail = &mesh; tail = &mesh.pNext; }
    if (hasAccel) { *tail = &accel; tail = &accel.pNext; }
    if (hasRtPipeline) { *tail = &rtPipeline; tail = &rtPipeline.pNext; }
    if (hasRayQuery) { *tail = &rayQuery; tail = &rayQuery.pNext; }
    vkGetPhysicalDeviceFeatures2(device, &features);

    c.dynamicRendering = f13.dynamicRendering;
    c.synchronization2 = f13.synchronization2;
    c.descriptorIndexing = f12.descriptorIndexing && f12.runtimeDescriptorArray && f12.descriptorBindingPartiallyBound &&
                           f12.descriptorBindingSampledImageUpdateAfterBind &&
                           f12.shaderSampledImageArrayNonUniformIndexing;
    c.timelineSemaphore = f12.timelineSemaphore;
    c.bufferDeviceAddress = f12.bufferDeviceAddress;
    c.drawIndirectCount = f12.drawIndirectCount;
    c.hostQueryReset = f12.hostQueryReset;
    c.shaderDrawParameters = f11.shaderDrawParameters;
    c.multiDrawIndirect = features.features.multiDrawIndirect;
    c.drawIndirectFirstInstance = features.features.drawIndirectFirstInstance;
    c.samplerAnisotropy = features.features.samplerAnisotropy;
    c.textureCompressionBC = features.features.textureCompressionBC;
    c.pipelineStatisticsQuery = features.features.pipelineStatisticsQuery;
    c.shaderInt64 = features.features.shaderInt64;
    c.memoryBudget = hasExtension(exts, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    c.meshShader = hasMesh && mesh.meshShader && mesh.taskShader;
    c.accelerationStructure = hasAccel && accel.accelerationStructure;
    c.rayTracingPipeline = hasRtPipeline && rtPipeline.rayTracingPipeline;
    c.rayQuery = hasRayQuery && rayQuery.rayQuery;

    // Properties.
    VkPhysicalDeviceVulkan11Properties p11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES};
    VkPhysicalDeviceVulkan12Properties p12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    p11.pNext = &p12;
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &p11;
    vkGetPhysicalDeviceProperties2(device, &props);
    const VkPhysicalDeviceLimits& limits = props.properties.limits;
    c.maxSamplerAnisotropy = c.samplerAnisotropy ? limits.maxSamplerAnisotropy : 1.0f;
    c.timestampPeriodNs = limits.timestampPeriod;
    c.maxImageDimension2D = limits.maxImageDimension2D;
    c.maxShadowMapSize = limits.maxImageDimension2D;
    c.maxPushConstantsSize = limits.maxPushConstantsSize;
    c.maxDrawIndirectCount = limits.maxDrawIndirectCount;
    c.maxComputeWorkGroupInvocations = limits.maxComputeWorkGroupInvocations;
    c.maxUpdateAfterBindSampledImages = p12.maxDescriptorSetUpdateAfterBindSampledImages;
    c.subgroupSize = p11.subgroupSize;

    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            c.deviceLocalMemoryBytes += memory.memoryHeaps[i].size;
        }
    }

    // Queue families: prefer one family for graphics + present; find dedicated compute/transfer.
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
    for (uint32_t i = 0; i < familyCount; ++i) {
        const VkQueueFlags flags = families[i].queueFlags;
        VkBool32 present = VK_FALSE;
        if (surface) {
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present);
        }
        const bool graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (graphics && (present || !surface) && c.graphicsFamily == UINT32_MAX) {
            c.graphicsFamily = i;
            c.presentFamily = surface ? i : UINT32_MAX;
        }
        if (present && c.presentFamily == UINT32_MAX) {
            c.presentFamily = i;
        }
        if ((flags & VK_QUEUE_COMPUTE_BIT) && !graphics && c.asyncComputeFamily == UINT32_MAX) {
            c.asyncComputeFamily = i;
        }
        if ((flags & VK_QUEUE_TRANSFER_BIT) && !graphics && !(flags & VK_QUEUE_COMPUTE_BIT) &&
            c.transferFamily == UINT32_MAX) {
            c.transferFamily = i;
        }
    }
    if (c.graphicsFamily == UINT32_MAX) {
        for (uint32_t i = 0; i < familyCount; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                c.graphicsFamily = i;
                break;
            }
        }
    }
    if (c.graphicsFamily != UINT32_MAX) {
        c.timestampValidBits = families[c.graphicsFamily].timestampValidBits;
        c.timestampQueries = c.timestampValidBits > 0;
    }
    return c;
}

} // namespace zodar::gfx
