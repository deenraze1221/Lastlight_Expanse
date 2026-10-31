add_executable(ZodarBench main.cpp)
target_link_libraries(ZodarBench PRIVATE Zodar::Runtime SDL3::SDL3)
zodar_set_warnings(ZodarBench)
