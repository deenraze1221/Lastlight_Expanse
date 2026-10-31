add_library(ZodarRenderer STATIC
    Renderer.cpp
    Camera.cpp
    Mesh.cpp
    GeometryPool.cpp
    GpuScene.cpp
    BindlessRegistry.cpp
    Texture.cpp
    Material.cpp
    Frustum.cpp
    SceneRenderer.cpp
    Lighting/ClusterMath.cpp
    Lighting/ClusteredLighting.cpp
    Lighting/Environment.cpp
    Model.cpp
    GpuProfiler.cpp
    RenderGraph/ResourceState.cpp
    RenderGraph/GraphPlanner.cpp
    RenderGraph/RenderGraph.cpp
    Resources/GpuBuffer.cpp
    Resources/GpuImage.cpp
    Resources/DeletionQueue.cpp
    Resources/FrameAllocator.cpp
    Resources/UploadContext.cpp
    Resources/UploadQueue.cpp
    Vulkan/VulkanCheck.cpp
    Vulkan/DeviceCapabilities.cpp
    Vulkan/DeviceSelection.cpp
    Vulkan/GpuTimeline.cpp
    Vulkan/PipelineCache.cpp
    Vulkan/SwapchainSupport.cpp
    Vulkan/VulkanContext.cpp
    Vulkan/VulkanSwapchain.cpp
    Vulkan/VulkanPipeline.cpp
    Vulkan/VmaImplementation.cpp)
add_library(Zodar::Renderer ALIAS ZodarRenderer)

target_link_libraries(ZodarRenderer
    PUBLIC Zodar::Core Zodar::Assets Zodar::Platform Zodar::volk Zodar::VMA)
zodar_set_warnings(ZodarRenderer)

# Built-in engine shaders (compiled to bin/shaders).
zodar_compile_shaders(ZodarRenderer SOURCES
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/mesh.vert
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/pbr.frag
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/unlit.frag
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/resolve.vert
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/resolve.frag
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/cull_scan.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/cull_blocks.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/cull_scatter.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/cluster_cull.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/ibl_equirect.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/ibl_downsample.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/ibl_irradiance.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/ibl_prefilter.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/ibl_dfg.comp
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/sky.vert
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/sky.frag
    ${PROJECT_SOURCE_DIR}/Shaders/Engine/depth_mask.frag)
