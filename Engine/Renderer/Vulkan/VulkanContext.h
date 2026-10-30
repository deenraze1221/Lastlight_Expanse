#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace raven {
class Window;
}

namespace raven::gfx {

struct ContextDesc {
    std::string applicationName = "RAVEN Application";
    // Requests VK_LAYER_KHRONOS_validation. If the layer is missing a warning is
    // logged and the context continues without it.
    bool enableValidation = true;
};

struct DeviceInfo {
    // Filled from the selected physical device.
    std::string name;
    std::string driverName;
    std::string driverInfo;
    VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    uint32_t apiVersion = 0;
    bool isSoftwareRasterizer = false;
    bool samplerAnisotropy = false;
    float maxSamplerAnisotropy = 1.0f;
};

// Owns instance, debug messenger, surface, logical device, queues and the memory
// allocator. Destruction order is the reverse of creation. Not copyable or movable:
// other objects keep references to it.
//
// With a window the context can present. Without one (window == nullptr) it is headless:
// no surface or swapchain support is required, which is used by GPU tests and tools.
class VulkanContext {
public:
    VulkanContext(const ContextDesc& desc, const Window* window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance instance() const { return m_instance; }
    VkSurfaceKHR surface() const { return m_surface; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VmaAllocator allocator() const { return m_allocator; }

    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkQueue presentQueue() const { return m_presentQueue; }
    uint32_t graphicsQueueFamily() const { return m_graphicsFamily; }
    uint32_t presentQueueFamily() const { return m_presentFamily; }

    const DeviceInfo& deviceInfo() const { return m_deviceInfo; }
    bool validationEnabled() const { return m_validationEnabled; }
    bool isHeadless() const { return m_window == nullptr; }

    // Names an object for validation messages and graphics debuggers (no-op when
    // VK_EXT_debug_utils is unavailable).
    void setDebugName(VkObjectType type, uint64_t handle, const char* name) const;
    template <typename T>
    void setDebugName(T handle, VkObjectType type, const char* name) const {
        setDebugName(type, reinterpret_cast<uint64_t>(handle), name);
    }

    // Number of validation errors reported since creation. Zero is required for tests.
    uint32_t validationErrorCount() const { return m_validationErrors.load(); }
    uint32_t validationWarningCount() const { return m_validationWarnings.load(); }

    void waitIdle() const;

private:
    void createInstance(const ContextDesc& desc);
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();
    void destroy();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                         VkDebugUtilsMessageTypeFlagsEXT types,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                         void* userData);

    const Window* m_window = nullptr;
    bool m_debugUtils = false;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsFamily = UINT32_MAX;
    uint32_t m_presentFamily = UINT32_MAX;
    DeviceInfo m_deviceInfo;
    bool m_validationEnabled = false;
    std::atomic<uint32_t> m_validationErrors{0};
    std::atomic<uint32_t> m_validationWarnings{0};
};

} // namespace raven::gfx
