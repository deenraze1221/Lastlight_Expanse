# TEMPORARY review tool: draws Zodar::UI draw lists with SDL3's 2D renderer until the
# Vulkan UI render-graph pass exists. SDL3 is used here only, never inside Zodar::UI.
add_executable(ZodarUISandbox main.cpp SdlInput.cpp SdlRendererBackend.cpp)
target_link_libraries(ZodarUISandbox PRIVATE Zodar::UI Zodar::stb SDL3::SDL3)
zodar_set_warnings(ZodarUISandbox)
