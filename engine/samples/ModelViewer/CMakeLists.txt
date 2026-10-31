add_executable(ModelViewer main.cpp)
target_link_libraries(ModelViewer PRIVATE Zodar::Runtime SDL3::SDL3)
zodar_set_warnings(ModelViewer)
