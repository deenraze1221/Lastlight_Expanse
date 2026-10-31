# Lastlight Expanse. Game code only: everything engine-level lives in engine/ and is used
# through the Zodar:: libraries' public headers.
add_executable(LastlightExpanse
    src/main.cpp
    src/World/LastlightTerrain.cpp
    src/Rendering/BlockTextures.cpp)
target_include_directories(LastlightExpanse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(LastlightExpanse PRIVATE Zodar::Runtime Zodar::Scene SDL3::SDL3)
zodar_set_warnings(LastlightExpanse)
# Game shaders compile next to the engine's (bin/shaders) and use the engine shader library.
zodar_compile_shaders(LastlightExpanse SOURCES shaders/voxel.frag)

if(ZODAR_BUILD_TESTS AND ZODAR_GPU_TESTS)
    add_test(NAME gpu.lastlight_smoke
        COMMAND LastlightExpanse --headless --deterministic --frames 90 --width 640 --height 360 --view-radius 5 --threads 2
                --screenshot ${CMAKE_BINARY_DIR}/lastlight_smoke.png --report ${CMAKE_BINARY_DIR}/lastlight_smoke.json)
    set_tests_properties(gpu.lastlight_smoke PROPERTIES TIMEOUT 300)
    # Phase A exit criterion: `zodar.py shot game --frames 120` yields a PNG and a run report
    # headless (tools/zodar.py exits non-zero if the game fails or either file is missing).
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME gpu.zodar_shot_game
            COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/zodar.py --build-dir ${CMAKE_BINARY_DIR}
                    shot game --frames 120 --out ${CMAKE_BINARY_DIR}/zodar_shot
                    -- --width 320 --height 180 --view-radius 3 --threads 2)
        set_tests_properties(gpu.zodar_shot_game PROPERTIES TIMEOUT 300)
    endif()
endif()
