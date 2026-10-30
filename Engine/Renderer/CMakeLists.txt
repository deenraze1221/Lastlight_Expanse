add_library(RavenRenderer STATIC
    Renderer.cpp
    Vulkan/VulkanCheck.cpp
    Vulkan/DeviceSelection.cpp
    Vulkan/SwapchainSupport.cpp
    Vulkan/VulkanContext.cpp
    Vulkan/VulkanSwapchain.cpp
    Vulkan/VulkanPipeline.cpp
    Vulkan/VmaImplementation.cpp)
add_library(Raven::Renderer ALIAS RavenRenderer)

target_link_libraries(RavenRenderer
    PUBLIC Raven::Core Raven::Platform Raven::volk Raven::VMA)
raven_set_warnings(RavenRenderer)
