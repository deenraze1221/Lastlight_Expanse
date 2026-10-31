#include "Renderer/Vulkan/VulkanContext.h"

#include "Renderer/Vulkan/ApiVersion.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "Renderer/Vulkan/DeviceSelection.h"
#include "Renderer/Vulkan/VulkanCheck.h"

#include <algorithm>
#include <optional>
#include <cstring>
#include <set>
#include <vector>

namespace zodar::gfx {
namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

std::string versionString(uint32_t v) {
    return fmt::format("{}.{}.{}", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
}

const char* deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU (software)";
    default: return "other";
    }
}

// Spec version of an installed instance layer, or nullopt when it is not installed.
std::optional<uint32_t> layerSpecVersion(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const VkLayerProperties& l : layers) {
        if (std::strcmp(l.layerName, name) == 0) {
            return l.specVersion;
        }
    }
    return std::nullopt;
}

bool hasInstanceExtension(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

} // namespace

VulkanContext::VulkanContext(const ContextDesc& desc, const Window* window) : m_window(window) {
    try {
        createInstance(desc);
        if (m_window) {
            m_surface = m_window->createSurface(m_instance);
        }
        pickPhysicalDevice();
        m_useTransferQueue = desc.dedicatedTransferQueue;
        createDevice();
        createAllocator();
        m_timeline = std::make_unique<GpuTimeline>(m_device);
        m_pipelineCache = std::make_unique<PipelineCache>(m_device, m_caps.vendorId, m_caps.deviceId,
                                                          m_caps.pipelineCacheUUID, desc.pipelineCachePath);
    } catch (...) {
        destroy(); // The destructor does not run for a failed constructor.
        throw;
    }
}

VulkanContext::~VulkanContext() {
    destroy();
}

void VulkanContext::destroy() {
    if (m_device) {
        vkDeviceWaitIdle(m_device);
    }
    m_deletionQueue.flushAll();
    m_pipelineCache.reset(); // Saves to disk.
    m_timeline.reset();
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_surface) {
        m_window->destroySurface(m_instance, m_surface);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_debugMessenger) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        ZODAR_LOG_INFO("Vulkan context destroyed (validation: {} errors, {} warnings)",
                       m_validationErrors.load(), m_validationWarnings.load());
    }
}

void VulkanContext::waitIdle() const {
    ZODAR_VK_CHECK(vkDeviceWaitIdle(m_device));
}

uint64_t VulkanContext::submitGraphics(const GraphicsSubmit& submit) {
    const uint64_t value = m_timeline->reserveNext();
    std::vector<VkSemaphoreSubmitInfo> signals(submit.signals.begin(), submit.signals.end());
    VkSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    timeline.semaphore = m_timeline->semaphore();
    timeline.value = value;
    timeline.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals.push_back(timeline);

    std::vector<VkSemaphoreSubmitInfo> waits(submit.waits.begin(), submit.waits.end());
    waits.insert(waits.end(), m_pendingWaits.begin(), m_pendingWaits.end());

    VkSubmitInfo2 info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    info.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    info.pWaitSemaphoreInfos = waits.data();
    info.commandBufferInfoCount = static_cast<uint32_t>(submit.commandBuffers.size());
    info.pCommandBufferInfos = submit.commandBuffers.data();
    info.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    info.pSignalSemaphoreInfos = signals.data();
    ZODAR_VK_CHECK(vkQueueSubmit2(m_graphicsQueue, 1, &info, submit.fence));
    m_deletionQueue.onSubmit(value, submit.frameSubmission, m_frameRecording);
    return value;
}

void VulkanContext::addGraphicsWait(VkSemaphore semaphore, uint64_t value) {
    for (VkSemaphoreSubmitInfo& w : m_pendingWaits) {
        if (w.semaphore == semaphore) {
            w.value = std::max(w.value, value);
            return;
        }
    }
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = semaphore;
    wait.value = value;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    m_pendingWaits.push_back(wait);
}

void VulkanContext::removeGraphicsWait(VkSemaphore semaphore) {
    std::erase_if(m_pendingWaits, [&](const VkSemaphoreSubmitInfo& w) { return w.semaphore == semaphore; });
}

void VulkanContext::setDebugName(VkObjectType type, uint64_t handle, const char* name) const {
    if (!m_debugUtils || !name || handle == 0) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

void VulkanContext::createInstance(const ContextDesc& desc) {
    if (m_window) {
        // Use SDL's loader so the engine and SDL resolve the same Vulkan library.
        volkInitializeCustom(m_window->vulkanGetInstanceProcAddr());
    } else if (volkInitialize() != VK_SUCCESS) {
        fail("Vulkan loader not found");
    }

    const uint32_t loaderVersion = volkGetInstanceVersion();
    ZODAR_LOG_INFO("Vulkan loader version {}", versionString(loaderVersion));
    if (loaderVersion < VK_API_VERSION_1_3) {
        fail("Vulkan 1.3 is required; the installed loader supports " + versionString(loaderVersion));
    }

    std::vector<const char*> extensions;
    if (m_window) {
        extensions = m_window->requiredInstanceExtensions();
    }
    std::vector<const char*> layers;

    // Debug utils is enabled whenever available: object names help validation and RenderDoc.
    m_debugUtils = hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (m_debugUtils) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    m_validationEnabled = false;
    std::optional<uint32_t> validationLayerVersion;
    if (desc.enableValidation) {
        validationLayerVersion = layerSpecVersion(kValidationLayer);
        if (!validationLayerVersion) {
            ZODAR_LOG_WARN("Validation requested but {} is not installed; continuing without it",
                           kValidationLayer);
        } else if (!m_debugUtils) {
            ZODAR_LOG_WARN("Validation requested but {} is unavailable; continuing without it",
                           VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            layers.push_back(kValidationLayer);
            m_validationEnabled = true;
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = desc.applicationName.c_str();
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.pEngineName = "ZODAR Engine";
    app.engineVersion = VK_MAKE_API_VERSION(0, ZODAR_VERSION_MAJOR, ZODAR_VERSION_MINOR, ZODAR_VERSION_PATCH);
    // Vulkan 1.3 minimum; 1.4 requested unless an enabled validation layer is older (ApiVersion.h).
    const InstanceApiChoice api =
        chooseInstanceApiVersion(m_validationEnabled ? validationLayerVersion : std::nullopt);
    m_instanceApiVersion = api.version;
    app.apiVersion = m_instanceApiVersion;

    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = &VulkanContext::debugCallback;
    messengerInfo.pUserData = this;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    // Also captures messages from vkCreateInstance / vkDestroyInstance themselves.
    info.pNext = m_validationEnabled ? &messengerInfo : nullptr;
    const VkValidationFeatureEnableEXT syncValidation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validationFeatures.enabledValidationFeatureCount = 1;
    validationFeatures.pEnabledValidationFeatures = &syncValidation;
    if (m_validationEnabled && desc.synchronizationValidation) {
        validationFeatures.pNext = info.pNext;
        info.pNext = &validationFeatures;
    }

    ZODAR_VK_CHECK(vkCreateInstance(&info, nullptr, &m_instance));
    volkLoadInstanceOnly(m_instance);

    if (m_validationEnabled) {
        ZODAR_VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &messengerInfo, nullptr, &m_debugMessenger));
    }
    ZODAR_LOG_INFO("Vulkan instance created (API {}, limited by {}, validation {}, {})",
                   apiVersionString(m_instanceApiVersion, false), api.limitedBy,
                   m_validationEnabled ? (desc.synchronizationValidation ? "on + sync" : "on") : "off",
                   m_window ? "windowed" : "headless");
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    ZODAR_VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
    if (count == 0) {
        fail("No Vulkan physical devices found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    ZODAR_VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()));

    std::vector<DeviceCandidate> candidates;
    std::vector<DeviceCapabilities> caps;
    for (VkPhysicalDevice device : devices) {
        DeviceCapabilities c = queryCapabilities(device, m_surface, m_instanceApiVersion);
        const auto missing = missingBaseline(c, m_surface != VK_NULL_HANDLE);
        DeviceCandidate candidate{c.name, c.type, c.deviceLocalMemoryBytes, missing.empty()};
        std::string missingText;
        for (const auto& m : missing) {
            missingText += (missingText.empty() ? "" : ", ") + m;
        }
        ZODAR_LOG_INFO("  GPU candidate: {} ({}, Vulkan {}, score {}{}{})", c.name, deviceTypeName(c.type),
                       versionString(c.apiVersion), scoreDevice(candidate), missing.empty() ? "" : ", missing: ",
                       missingText);
        candidates.push_back(candidate);
        caps.push_back(std::move(c));
    }

    const int best = pickBestDevice(candidates);
    if (best < 0) {
        fail("No GPU meets the ZODAR baseline (Vulkan 1.3 with dynamic rendering, synchronization2, descriptor "
             "indexing, timeline semaphores, buffer device address and indirect drawing); see the log for details");
    }
    m_physicalDevice = devices[static_cast<size_t>(best)];
    m_caps = caps[static_cast<size_t>(best)];
    m_features = resolveFeatures(m_caps);
    m_graphicsFamily = m_caps.graphicsFamily;
    m_presentFamily = m_surface ? m_caps.presentFamily : m_caps.graphicsFamily;

    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);

    m_deviceInfo.name = props2.properties.deviceName;
    m_deviceInfo.type = props2.properties.deviceType;
    m_deviceInfo.apiVersion = props2.properties.apiVersion;
    m_deviceInfo.effectiveApiVersion = m_caps.effectiveApiVersion;
    m_deviceInfo.driverName = driver.driverName;
    m_deviceInfo.driverInfo = driver.driverInfo;
    m_deviceInfo.isSoftwareRasterizer = m_deviceInfo.type == VK_PHYSICAL_DEVICE_TYPE_CPU;

    m_deviceInfo.samplerAnisotropy = m_features.anisotropicFiltering;
    m_deviceInfo.maxSamplerAnisotropy = m_caps.maxSamplerAnisotropy;

    ZODAR_LOG_INFO("Selected GPU: {} ({}), driver {} {}", m_deviceInfo.name, deviceTypeName(m_deviceInfo.type),
                   m_deviceInfo.driverName, m_deviceInfo.driverInfo);
    ZODAR_LOG_INFO("Vulkan API: device {}, instance {}, effective {}{}", versionString(m_deviceInfo.apiVersion),
                   apiVersionString(m_instanceApiVersion, false), apiVersionString(m_caps.effectiveApiVersion, false),
                   m_features.vulkan14 ? " (1.4 detected; engine paths use 1.3 core)" : "");
    if (m_deviceInfo.isSoftwareRasterizer) {
        ZODAR_LOG_WARN("Rendering on a software rasterizer: output is valid, performance numbers are not");
    }
    ZODAR_LOG_INFO("Optional features:");
    for (const auto& entry : m_features.entries()) {
        ZODAR_LOG_INFO("  {:<38} {}", entry.name, entry.enabled ? "yes" : fmt::format("no -> {}", entry.fallback));
    }
}

void VulkanContext::createDevice() {
    const float priority = 1.0f;
    std::set<uint32_t> families{m_graphicsFamily, m_presentFamily};
    if (m_features.asyncCompute) families.insert(m_caps.asyncComputeFamily);
    if (m_features.dedicatedTransfer && m_useTransferQueue) families.insert(m_caps.transferFamily);
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (uint32_t family : families) {
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queues.push_back(q);
    }

    // Engine baseline (validated in pickPhysicalDevice) plus supported optional features.
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.pNext = &features13;
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.timelineSemaphore = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.drawIndirectCount = VK_TRUE;
    features12.hostQueryReset = m_caps.hostQueryReset ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    features11.pNext = &features12;
    features11.shaderDrawParameters = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &features11;
    features.features.multiDrawIndirect = VK_TRUE;
    features.features.drawIndirectFirstInstance = VK_TRUE;
    features.features.samplerAnisotropy = m_features.anisotropicFiltering ? VK_TRUE : VK_FALSE;
    features.features.textureCompressionBC = m_features.bcTextureCompression ? VK_TRUE : VK_FALSE;
    features.features.pipelineStatisticsQuery = m_features.pipelineStatistics ? VK_TRUE : VK_FALSE;
    // Mesh shaders and ray tracing are detected only; they are enabled when their render
    // paths exist (S9 / S11), so no unused feature affects driver behaviour today.

    std::vector<const char*> extensions;
    if (m_surface) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    if (m_features.memoryBudget) {
        extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features;
    info.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
    info.pQueueCreateInfos = queues.data();
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    ZODAR_VK_CHECK(vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device));
    volkLoadDevice(m_device);

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
    if (m_features.asyncCompute) vkGetDeviceQueue(m_device, m_caps.asyncComputeFamily, 0, &m_computeQueue);
    if (m_features.dedicatedTransfer && m_useTransferQueue) {
        vkGetDeviceQueue(m_device, m_caps.transferFamily, 0, &m_transferQueue);
    }
    ZODAR_LOG_INFO("Logical device created (graphics family {}, present family {}, compute {}, transfer {})",
                   m_graphicsFamily, m_presentFamily, m_computeQueue ? std::to_string(m_caps.asyncComputeFamily) : "-",
                   m_transferQueue ? std::to_string(m_caps.transferFamily) : "-");
}

void VulkanContext::createAllocator() {
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo info{};
    // VMA must not assume more than both the instance and the device provide.
    info.vulkanApiVersion = m_caps.effectiveApiVersion;
    info.physicalDevice = m_physicalDevice;
    info.device = m_device;
    info.instance = m_instance;
    info.pVulkanFunctions = &functions;
    info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    if (m_features.memoryBudget) {
        info.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    ZODAR_VK_CHECK(vmaCreateAllocator(&info, &m_allocator));
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                            VkDebugUtilsMessageTypeFlagsEXT,
                                                            const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                            void* userData) {
    auto* self = static_cast<VulkanContext*>(userData);
    const char* id = data->pMessageIdName ? data->pMessageIdName : "";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        self->m_validationErrors.fetch_add(1);
        ZODAR_LOG_ERROR("[Vulkan] {} {}", id, data->pMessage);
    } else {
        self->m_validationWarnings.fetch_add(1);
        ZODAR_LOG_WARN("[Vulkan] {} {}", id, data->pMessage);
    }
    return VK_FALSE;
}

} // namespace zodar::gfx
