# Engine debugging and profiling tools (Dear ImGui). Not a game UI system.
add_library(ZodarDebug STATIC DebugOverlay.cpp)
add_library(Zodar::Debug ALIAS ZodarDebug)
target_include_directories(ZodarDebug PUBLIC ${ZODAR_ENGINE_DIR}/tools)
target_link_libraries(ZodarDebug PUBLIC Zodar::Renderer PRIVATE Zodar::imgui SDL3::SDL3)
zodar_set_warnings(ZodarDebug)
