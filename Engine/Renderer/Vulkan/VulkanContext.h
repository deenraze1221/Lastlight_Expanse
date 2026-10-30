#pragma once

#include "Renderer/Resources/DeletionQueue.h"
#include "Renderer/Vulkan/DeviceCapabilities.h"
#include "Renderer/Vulkan/GpuTimeline.h"
#include "Renderer/Vulkan/PipelineCache.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <atomic>
#include <memory>
#include <span>
#include <cstdint>
#include <filesystem>
#include <string>

namespace zodar {
class Window;
}

namespace zodar::gfx {

struct ContextDesc {
    std::string applicationName = "ZODAR Application";
    // Requests VK_LAYER_KHRONOS_validation. If the layer is missing a warning is
    // logged and the context continues without it.
    bool enableValidation = true;
    // Adds VK_EXT_validation_features synchronization validation (slower; for tests).
    bool synchronizationValidation = false;
    // Persistent pipeline cache file. Empty: in-memory cache only.
    std::filesystem::path pipelineCachePath;
};

// One graphics-queue submission. The context appends the timeline signal.
struct GraphicsSubmit {
    std::span<const VkCommandBufferSubmitInfo> commandBuffers;
    std::span<const VkSemaphoreSubmitInfo> waits;
    std::span<const VkSemaphoreSubmitInfo> signals;
    bool frameSubmission = false; // Completes the frame currently being recorded.
    VkFence fence = VK_NULL_HANDLE; // Optional, for host waits (see UploadContext).
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
    const DeviceCapabilities& capabilities() const { return m_caps; }
    const ResolvedFeatures& features() const { return m_features; }

    // Dedicated queues; VK_NULL_HANDLE when the device has none (use the graphics queue).
    VkQueue asyncComputeQueue() const { return m_computeQueue; }
    VkQueue transferQueue() const { return m_transferQueue; }
    bool validationEnabled() const { return m_validationEnabled; }
    bool isHeadless() const { return m_window == nullptr; }
    bool debugUtilsEnabled() const { return m_debugUtils; }

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

    // All graphics-queue work goes through here so the timeline and the deletion queue
    // stay consistent. Returns the timeline value signalled by this submission.
    uint64_t submitGraphics(const GraphicsSubmit& submit);
    VkPipelineCache pipelineCache() const { return m_pipelineCache ? m_pipelineCache->handle() : VK_NULL_HANDLE; }
    PipelineCache* pipelineCacheObject() { return m_pipelineCache.get(); }
    GpuTimeline& timeline() { return *m_timeline; }
    const GpuTimeline& timeline() const { return *m_timeline; }
    DeletionQueue& deletionQueue() { return m_deletionQueue; }
    // Retires completed deferred deletions; call once per frame and after blocking waits.
    void collectGarbage() { m_deletionQueue.collect(m_timeline->completedValue()); }
    // Set by the frame loop while a frame command buffer is being recorded.
    void setFrameRecording(bool recording) { m_frameRecording = recording; }

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
    DeviceCapabilities m_caps;
    ResolvedFeatures m_features;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    bool m_validationEnabled = false;
    std::unique_ptr<GpuTimeline> m_timeline;
    std::unique_ptr<PipelineCache> m_pipelineCache;
    DeletionQueue m_deletionQueue;
    bool m_frameRecording = false;
    std::atomic<uint32_t> m_validationErrors{0};
    std::atomic<uint32_t> m_validationWarnings{0};
};

} // namespace zodar::gfx
