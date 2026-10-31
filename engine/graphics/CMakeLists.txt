# Low-level Vulkan 1.3 layer: instance/device/swapchain, pipelines, capability table,
# timeline, pipeline cache, VMA-backed buffers/images, uploads, deferred deletion and
# per-frame scratch memory. Split out of the renderer unchanged; headers keep their
# "Renderer/Vulkan/..." and "Renderer/Resources/..." include paths and the zodar::gfx namespace.
add_library(ZodarGraphics STATIC
    Renderer/Resources/GpuBuffer.cpp
    Renderer/Resources/GpuImage.cpp
    Renderer/Resources/DeletionQueue.cpp
    Renderer/Resources/FrameAllocator.cpp
    Renderer/Resources/UploadContext.cpp
    Renderer/Resources/UploadQueue.cpp
    Renderer/Vulkan/VulkanCheck.cpp
    Renderer/Vulkan/DeviceCapabilities.cpp
    Renderer/Vulkan/DeviceSelection.cpp
    Renderer/Vulkan/GpuTimeline.cpp
    Renderer/Vulkan/PipelineCache.cpp
    Renderer/Vulkan/SwapchainSupport.cpp
    Renderer/Vulkan/VulkanContext.cpp
    Renderer/Vulkan/VulkanSwapchain.cpp
    Renderer/Vulkan/VulkanPipeline.cpp
    Renderer/Vulkan/VmaImplementation.cpp)
add_library(Zodar::Graphics ALIAS ZodarGraphics)

target_include_directories(ZodarGraphics PUBLIC ${ZODAR_ENGINE_DIR}/graphics)
target_link_libraries(ZodarGraphics
    PUBLIC Zodar::Core Zodar::Assets Zodar::Platform Zodar::volk Zodar::VMA)
zodar_set_warnings(ZodarGraphics)
