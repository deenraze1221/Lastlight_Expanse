add_executable(ModelViewer main.cpp)
target_link_libraries(ModelViewer PRIVATE Raven::Renderer Raven::Debug SDL3::SDL3)
raven_set_warnings(ModelViewer)
