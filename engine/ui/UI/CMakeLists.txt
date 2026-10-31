# Engine UI: theme tokens, layout, text, SVG images, widgets. Renderer- and SDL-independent:
# it produces plain draw lists and CPU images that a backend (temporary SDL sandbox now,
# a Vulkan render-graph pass later) turns into pixels.
add_library(ZodarUI STATIC
    Theme.cpp
    DrawList.cpp
    TextureStore.cpp
    Layout.cpp
    Scale.cpp
    Font.cpp
    Svg.cpp
    Input.cpp
    Context.cpp
    Widgets/Label.cpp
    Widgets/Image.cpp
    Widgets/Button.cpp
    Startup.cpp
    LogBuffer.cpp
    WindowPlan.cpp
    Widgets/ProgressBar.cpp
    Screens/LoadingScreen.cpp)
add_library(Zodar::UI ALIAS ZodarUI)
target_include_directories(ZodarUI PUBLIC ${ZODAR_ENGINE_DIR}/ui)

target_link_libraries(ZodarUI
    PUBLIC Zodar::Core Zodar::Platform
    PRIVATE Zodar::stb Zodar::nanosvg)
zodar_set_warnings(ZodarUI)

# Fonts, licenses and original logos are copied next to the executables (bin/ui).
set(ZODAR_UI_RESOURCE_DIR ${ZODAR_ENGINE_DIR}/ui/Resources/UI CACHE INTERNAL "")
add_custom_target(ZodarUIResources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${ZODAR_UI_RESOURCE_DIR} ${ZODAR_OUTPUT_DIR}/ui
    COMMENT "Copying UI resources to ${ZODAR_OUTPUT_DIR}/ui")
add_dependencies(ZodarUI ZodarUIResources)
