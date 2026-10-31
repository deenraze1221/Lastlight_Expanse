add_library(ZodarRenderer STATIC
    Renderer.cpp
    Camera.cpp
    Mesh.cpp
    GeometryPool.cpp
    GpuScene.cpp
    BindlessRegistry.cpp
    Texture.cpp
    TextureArray.cpp
    Material.cpp
    Frustum.cpp
    SceneRenderer.cpp
    Lighting/ClusterMath.cpp
    Lighting/ClusteredLighting.cpp
    Lighting/Environment.cpp
    Model.cpp
    StreamedMeshSet.cpp
    GpuProfiler.cpp
    RenderGraph/ResourceState.cpp
    RenderGraph/GraphPlanner.cpp
    RenderGraph/RenderGraph.cpp)
add_library(Zodar::Renderer ALIAS ZodarRenderer)

# Headers are included as "Renderer/<File>.h" (same path as before the module split).
target_include_directories(ZodarRenderer PUBLIC ${ZODAR_ENGINE_DIR}/rendering)

target_link_libraries(ZodarRenderer
    PUBLIC Zodar::Graphics)
zodar_set_warnings(ZodarRenderer)

# Built-in engine shaders (compiled to bin/shaders).
zodar_compile_shaders(ZodarRenderer SOURCES
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/mesh.vert
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/pbr.frag
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/unlit.frag
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/resolve.vert
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/resolve.frag
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/cull_scan.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/cull_blocks.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/cull_scatter.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/cluster_cull.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/ibl_equirect.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/ibl_downsample.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/ibl_irradiance.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/ibl_prefilter.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/ibl_dfg.comp
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/sky.vert
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/sky.frag
    ${ZODAR_ENGINE_DIR}/rendering/Shaders/Engine/depth_mask.frag)
