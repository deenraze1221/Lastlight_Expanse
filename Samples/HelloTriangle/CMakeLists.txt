add_executable(HelloTriangle main.cpp)
target_link_libraries(HelloTriangle PRIVATE Raven::Renderer SDL3::SDL3)
raven_set_warnings(HelloTriangle)
raven_compile_shaders(HelloTriangle SOURCES
    Shaders/triangle.vert
    Shaders/triangle.frag)
