# Engine debugging and profiling tools (Dear ImGui). Not a game UI system.
add_library(RavenDebug STATIC DebugOverlay.cpp)
add_library(Raven::Debug ALIAS RavenDebug)
target_link_libraries(RavenDebug PUBLIC Raven::Renderer PRIVATE Raven::imgui SDL3::SDL3)
raven_set_warnings(RavenDebug)
