add_executable(ZodarBench main.cpp RegionScenario.cpp)
target_link_libraries(ZodarBench PRIVATE Zodar::Runtime Zodar::Scene SDL3::SDL3)
zodar_set_warnings(ZodarBench)
