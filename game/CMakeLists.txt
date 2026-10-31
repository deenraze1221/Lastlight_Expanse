# Lastlight Expanse. Game code only: everything engine-level lives in engine/ and is used
# through the Zodar:: libraries' public headers.
add_executable(LastlightExpanse
    src/main.cpp
    src/World/LastlightTerrain.cpp)
target_include_directories(LastlightExpanse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(LastlightExpanse PRIVATE Zodar::Renderer Zodar::Scene Zodar::Debug SDL3::SDL3)
zodar_set_warnings(LastlightExpanse)

if(ZODAR_BUILD_TESTS AND ZODAR_GPU_TESTS)
    add_test(NAME gpu.lastlight_smoke
        COMMAND LastlightExpanse --frames 90 --width 640 --height 360 --view-radius 5 --threads 2
                --screenshot ${CMAKE_BINARY_DIR}/lastlight_smoke.ppm)
    set_tests_properties(gpu.lastlight_smoke PROPERTIES TIMEOUT 300)
endif()
