add_library(ZodarPlatform STATIC
    Platform.cpp
    Window.cpp)
add_library(Zodar::Platform ALIAS ZodarPlatform)

target_link_libraries(ZodarPlatform
    PUBLIC Zodar::Core Zodar::volk
    PRIVATE SDL3::SDL3)
zodar_set_warnings(ZodarPlatform)
