# Lastlight Expanse. Game code only: everything engine-level lives in engine/ and is used
# through the Zodar:: libraries' public headers.
# Game logic without the app (unit-tested by LastlightGameTests).
add_library(LastlightGameCore STATIC
    src/Physics/CollisionWorld.cpp
    src/Player/ExplorationWalker.cpp
    src/Bunker/LevelColliders.cpp)
target_include_directories(LastlightGameCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(LastlightGameCore PUBLIC Zodar::SceneFile Zodar::World PRIVATE nlohmann_json::nlohmann_json)
zodar_set_warnings(LastlightGameCore)

add_executable(LastlightExpanse
    src/main.cpp
    src/Bunker/BunkerApp.cpp
    src/World/LastlightTerrain.cpp
    src/Rendering/BlockTextures.cpp)
target_include_directories(LastlightExpanse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(LastlightExpanse PRIVATE LastlightGameCore Zodar::Runtime Zodar::Scene Zodar::AssetManager
    Zodar::SceneFile Zodar::World Zodar::WorldRender SDL3::SDL3)
# Default asset directory (--assets overrides): the bunker level, models, materials, registry.
target_compile_definitions(LastlightExpanse PRIVATE LASTLIGHT_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
zodar_set_warnings(LastlightExpanse)
# Game shaders compile next to the engine's (bin/shaders) and use the engine shader library.
zodar_compile_shaders(LastlightExpanse SOURCES shaders/voxel.frag)

if(ZODAR_BUILD_TESTS)
    add_executable(LastlightGameTests tests/main.cpp tests/CollisionTests.cpp tests/BunkerWalkTests.cpp
        tests/HighlandsMeshTests.cpp src/World/LastlightTerrain.cpp src/Rendering/BlockTextures.cpp)
    target_link_libraries(LastlightGameTests PRIVATE LastlightGameCore Zodar::Scene doctest::doctest)
    target_compile_definitions(LastlightGameTests PRIVATE LASTLIGHT_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    zodar_set_warnings(LastlightGameTests)
    add_test(NAME game.unit COMMAND LastlightGameTests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        # Source-file policy (regular Git, no LFS): .blend files <= 20 MiB, no .blend1/.blend2.
        add_test(NAME tools.game_source_sizes
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_source_sizes.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/assets)
        add_test(NAME tools.game_source_sizes_selftest
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_source_sizes.py --self-test)
        # The committed greybox files match their generator.
        add_test(NAME tools.greybox_kit_up_to_date
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_greybox_kit.py --check)
    endif()
    if(TARGET ZodarAssetTool)
        # Every game asset passes the strict check (sizes, palette, texel density, budgets,
        # bounds, importer warnings, stale hashes) for both tool paths.
        add_test(NAME tools.game_assets_check
            COMMAND ZodarAssetTool check --strict --registry ${CMAKE_CURRENT_SOURCE_DIR}/assets/assets.json
                    --report ${CMAKE_BINARY_DIR}/game_assets_check.json)
    endif()
endif()

if(ZODAR_BUILD_TESTS AND ZODAR_GPU_TESTS)
    # Highlands regression scene (screenshot byte-identical to before Phase D2).
    add_test(NAME gpu.lastlight_smoke
        COMMAND LastlightExpanse --level highlands --headless --deterministic --frames 90 --width 640 --height 360
                --view-radius 5 --threads 2
                --screenshot ${CMAKE_BINARY_DIR}/lastlight_smoke.png --report ${CMAKE_BINARY_DIR}/lastlight_smoke.json)
    set_tests_properties(gpu.lastlight_smoke PROPERTIES TIMEOUT 300)
    # Phase D2: the bunker level (default) loads every module, prefab and material and renders
    # with validation on (exit 2 on validation errors, 1 on a load failure).
    add_test(NAME gpu.lastlight_bunker_smoke
        COMMAND LastlightExpanse --level bunker --headless --deterministic --frames 30 --width 640 --height 360
                --threads 2 --screenshot ${CMAKE_BINARY_DIR}/lastlight_bunker_smoke.png
                --report ${CMAKE_BINARY_DIR}/lastlight_bunker_smoke.json)
    set_tests_properties(gpu.lastlight_bunker_smoke PROPERTIES TIMEOUT 300)
    # Exploration mode: colliders built, the walker spawns at the entrance, falls onto the floor
    # and ends neither inside geometry nor below the floor (exit 1 otherwise).
    add_test(NAME gpu.lastlight_bunker_explore_smoke
        COMMAND LastlightExpanse --level bunker --explore --headless --deterministic --frames 90 --width 320
                --height 180 --threads 2 --screenshot ${CMAKE_BINARY_DIR}/lastlight_bunker_explore.png)
    set_tests_properties(gpu.lastlight_bunker_explore_smoke PROPERTIES TIMEOUT 300
        PASS_REGULAR_EXPRESSION "Explorer at \\(-11\\.00, 0\\.00, 0\\.00\\), on the ground")
    # Phase A exit criterion: `zodar.py shot game --frames 120` yields a PNG and a run report
    # headless (tools/zodar.py exits non-zero if the game fails or either file is missing).
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME gpu.zodar_shot_game
            COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/zodar.py --build-dir ${CMAKE_BINARY_DIR}
                    shot game --frames 120 --out ${CMAKE_BINARY_DIR}/zodar_shot
                    -- --level highlands --width 320 --height 180 --view-radius 3 --threads 2)
        set_tests_properties(gpu.zodar_shot_game PROPERTIES TIMEOUT 300)
    endif()
endif()
