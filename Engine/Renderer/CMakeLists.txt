add_library(RavenRenderer STATIC
    Renderer.cpp
    Camera.cpp
    Mesh.cpp
    BindlessRegistry.cpp
    Texture.cpp
    Material.cpp
    Frustum.cpp
    SceneRenderer.cpp
    Model.cpp
    Resources/GpuBuffer.cpp
    Resources/GpuImage.cpp
    Resources/UploadContext.cpp
    Vulkan/VulkanCheck.cpp
    Vulkan/DeviceSelection.cpp
    Vulkan/SwapchainSupport.cpp
    Vulkan/VulkanContext.cpp
    Vulkan/VulkanSwapchain.cpp
    Vulkan/VulkanPipeline.cpp
    Vulkan/VmaImplementation.cpp)
add_library(Raven::Renderer ALIAS RavenRenderer)

target_link_libraries(RavenRenderer
    PUBLIC Raven::Core Raven::Assets Raven::Platform Raven::volk Raven::VMA)
raven_set_warnings(RavenRenderer)

# Built-in engine shaders (compiled to bin/shaders).
raven_compile_shaders(RavenRenderer SOURCES
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/mesh.vert
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/pbr.frag
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/unlit.frag
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/resolve.vert
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/resolve.frag)
