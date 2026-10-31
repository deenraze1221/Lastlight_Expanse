add_executable(HelloTriangle main.cpp)
target_link_libraries(HelloTriangle PRIVATE Zodar::Renderer SDL3::SDL3)
zodar_set_warnings(HelloTriangle)
zodar_compile_shaders(HelloTriangle SOURCES
    Shaders/triangle.vert
    Shaders/triangle.frag)
