# World-to-renderer bridge (Phase B1.3): MeshRenderer component, CPU draw collection
# (SceneView::draws) and incremental GpuScene synchronisation. Depends on World and Renderer;
# World itself stays GPU-independent.
add_library(ZodarWorldRender STATIC
    WorldRender/WorldRender.cpp)
add_library(Zodar::WorldRender ALIAS ZodarWorldRender)

target_include_directories(ZodarWorldRender PUBLIC ${ZODAR_ENGINE_DIR}/rendering/WorldRender)
target_link_libraries(ZodarWorldRender PUBLIC Zodar::World Zodar::Renderer)
zodar_set_warnings(ZodarWorldRender)
