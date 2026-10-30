#include "Renderer/Vulkan/VulkanContext.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "Renderer/Vulkan/DeviceSelection.h"
#include "Renderer/Vulkan/VulkanCheck.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace raven::gfx {
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

bool hasLayer(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(),
                       [&](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
}

bool hasInstanceExtension(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

struct QueueFamilies {
    uint32_t graphics = UINT32_MAX;
    uint32_t present = UINT32_MAX;
};

QueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    QueueFamilies result;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = VK_FALSE;
        if (surface) {
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present);
        }
        const bool graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (graphics && (present || !surface)) {
            return {i, i}; // A single family for both is preferred; headless needs graphics only.
        }
        if (graphics && result.graphics == UINT32_MAX) {
            result.graphics = i;
        }
        if (present && result.present == UINT32_MAX) {
            result.present = i;
        }
    }
    return result;
}

} // namespace

VulkanContext::VulkanContext(const ContextDesc& desc, const Window* window) : m_window(window) {
    try {
        createInstance(desc);
        if (m_window) {
            m_surface = m_window->createSurface(m_instance);
        }
        pickPhysicalDevice();
        createDevice();
        createAllocator();
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
        RAVEN_LOG_INFO("Vulkan context destroyed (validation: {} errors, {} warnings)",
                       m_validationErrors.load(), m_validationWarnings.load());
    }
}

void VulkanContext::waitIdle() const {
    RAVEN_VK_CHECK(vkDeviceWaitIdle(m_device));
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
    RAVEN_LOG_INFO("Vulkan loader version {}", versionString(loaderVersion));
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
    if (desc.enableValidation) {
        if (!hasLayer(kValidationLayer)) {
            RAVEN_LOG_WARN("Validation requested but {} is not installed; continuing without it",
                           kValidationLayer);
        } else if (!m_debugUtils) {
            RAVEN_LOG_WARN("Validation requested but {} is unavailable; continuing without it",
                           VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            layers.push_back(kValidationLayer);
            m_validationEnabled = true;
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = desc.applicationName.c_str();
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.pEngineName = "RAVEN Engine";
    app.engineVersion = VK_MAKE_API_VERSION(0, RAVEN_VERSION_MAJOR, RAVEN_VERSION_MINOR, RAVEN_VERSION_PATCH);
    app.apiVersion = VK_API_VERSION_1_3;

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

    RAVEN_VK_CHECK(vkCreateInstance(&info, nullptr, &m_instance));
    volkLoadInstanceOnly(m_instance);

    if (m_validationEnabled) {
        RAVEN_VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &messengerInfo, nullptr, &m_debugMessenger));
    }
    RAVEN_LOG_INFO("Vulkan instance created (API 1.3, validation {}, {})", m_validationEnabled ? "on" : "off",
                   m_window ? "windowed" : "headless");
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    RAVEN_VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
    if (count == 0) {
        fail("No Vulkan physical devices found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    RAVEN_VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()));

    std::vector<DeviceCandidate> candidates;
    std::vector<QueueFamilies> families;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        if (props.apiVersion >= VK_API_VERSION_1_3) {
            features12.pNext = &features13;
            features.pNext = &features12;
            vkGetPhysicalDeviceFeatures2(device, &features);
        }

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());

        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        VkDeviceSize localBytes = 0;
        for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                localBytes += memory.memoryHeaps[i].size;
            }
        }

        const QueueFamilies q = findQueueFamilies(device, m_surface);
        DeviceCandidate c;
        c.name = props.deviceName;
        c.type = props.deviceType;
        c.apiVersion = props.apiVersion;
        c.hasGraphicsQueue = q.graphics != UINT32_MAX;
        // Headless contexts do not present, so presentation support is not a requirement.
        c.hasPresentQueue = !m_surface || q.present != UINT32_MAX;
        c.hasSwapchainExtension = !m_surface ||
            std::any_of(exts.begin(), exts.end(), [](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            });
        c.hasDescriptorIndexing = features12.descriptorIndexing && features12.runtimeDescriptorArray &&
                                  features12.descriptorBindingPartiallyBound &&
                                  features12.descriptorBindingSampledImageUpdateAfterBind &&
                                  features12.shaderSampledImageArrayNonUniformIndexing;
        c.hasDynamicRendering = features13.dynamicRendering == VK_TRUE;
        c.hasSynchronization2 = features13.synchronization2 == VK_TRUE;
        c.deviceLocalMemoryBytes = localBytes;

        RAVEN_LOG_INFO("  GPU candidate: {} ({}, Vulkan {}, score {})", c.name, deviceTypeName(c.type),
                       versionString(c.apiVersion), scoreDevice(c));
        candidates.push_back(c);
        families.push_back(q);
    }

    const int best = pickBestDevice(candidates);
    if (best < 0) {
        fail("No GPU supports Vulkan 1.3 with dynamic rendering, synchronization2, descriptor indexing"
             " and presentation");
    }
    m_physicalDevice = devices[static_cast<size_t>(best)];
    m_graphicsFamily = families[static_cast<size_t>(best)].graphics;
    m_presentFamily = families[static_cast<size_t>(best)].present;

    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);

    m_deviceInfo.name = props2.properties.deviceName;
    m_deviceInfo.type = props2.properties.deviceType;
    m_deviceInfo.apiVersion = props2.properties.apiVersion;
    m_deviceInfo.driverName = driver.driverName;
    m_deviceInfo.driverInfo = driver.driverInfo;
    m_deviceInfo.isSoftwareRasterizer = m_deviceInfo.type == VK_PHYSICAL_DEVICE_TYPE_CPU;

    VkPhysicalDeviceFeatures baseFeatures;
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &baseFeatures);
    m_deviceInfo.samplerAnisotropy = baseFeatures.samplerAnisotropy == VK_TRUE;
    m_deviceInfo.maxSamplerAnisotropy =
        m_deviceInfo.samplerAnisotropy ? props2.properties.limits.maxSamplerAnisotropy : 1.0f;

    RAVEN_LOG_INFO("Selected GPU: {} ({}), driver {} {}", m_deviceInfo.name, deviceTypeName(m_deviceInfo.type),
                   m_deviceInfo.driverName, m_deviceInfo.driverInfo);
    if (m_deviceInfo.isSoftwareRasterizer) {
        RAVEN_LOG_WARN("Rendering on a software rasterizer: output is valid, performance numbers are not");
    }
}

void VulkanContext::createDevice() {
    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (uint32_t family : std::set<uint32_t>{m_graphicsFamily, m_presentFamily}) {
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queues.push_back(q);
    }

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    // Bindless resource tables (see BindlessRegistry).
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.pNext = &features13;
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &features12;
    features.features.samplerAnisotropy = m_deviceInfo.samplerAnisotropy ? VK_TRUE : VK_FALSE;

    std::vector<const char*> extensions;
    if (m_surface) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features;
    info.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
    info.pQueueCreateInfos = queues.data();
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    RAVEN_VK_CHECK(vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device));
    volkLoadDevice(m_device);

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
    RAVEN_LOG_INFO("Logical device created (graphics family {}, present family {})", m_graphicsFamily,
                   m_presentFamily);
}

void VulkanContext::createAllocator() {
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.physicalDevice = m_physicalDevice;
    info.device = m_device;
    info.instance = m_instance;
    info.pVulkanFunctions = &functions;
    RAVEN_VK_CHECK(vmaCreateAllocator(&info, &m_allocator));
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                            VkDebugUtilsMessageTypeFlagsEXT,
                                                            const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                            void* userData) {
    auto* self = static_cast<VulkanContext*>(userData);
    const char* id = data->pMessageIdName ? data->pMessageIdName : "";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        self->m_validationErrors.fetch_add(1);
        RAVEN_LOG_ERROR("[Vulkan] {} {}", id, data->pMessage);
    } else {
        self->m_validationWarnings.fetch_add(1);
        RAVEN_LOG_WARN("[Vulkan] {} {}", id, data->pMessage);
    }
    return VK_FALSE;
}

} // namespace raven::gfx
