add_executable(ZodarTests
    main.cpp
    Assets/GltfLoaderTests.cpp
    Assets/GltfToolFixtureTests.cpp
    Assets/ImageDataTests.cpp
    Assets/TextureAssetTests.cpp
    Assets/EnvironmentMapTests.cpp
    Assets/MeshDataTests.cpp
    Assets/AssetCacheTests.cpp
    Assets/AssetRegistryTests.cpp
    Assets/PaletteTests.cpp
    Core/LogTests.cpp
    Core/ProfilerTests.cpp
    Core/ErrorTests.cpp
    Core/ImageIOTests.cpp
    Core/FileWatcherTests.cpp
    Renderer/CameraTests.cpp
    Renderer/DeletionQueueTests.cpp
    Renderer/FrustumTests.cpp
    Renderer/GraphPlannerTests.cpp
    Renderer/PipelineCacheTests.cpp
    Renderer/DeviceSelectionTests.cpp
    Renderer/SwapchainSupportTests.cpp
    Renderer/VulkanCheckTests.cpp
    Renderer/UploadQueueTests.cpp
    Renderer/ClusterMathTests.cpp
    Renderer/ApiVersionTests.cpp
    Scene/VoxelTests.cpp
    Scene/VoxelRegressionTests.cpp
    Scene/VoxelShapeTests.cpp
    Scene/VoxelShapeMeshTests.cpp
    Scene/VoxelQueryTests.cpp
    Scene/VoxelLodTests.cpp
    World/WorldTests.cpp
    World/WorldComponentTests.cpp
    World/WorldRenderTests.cpp
    SceneFile/BlockFileTests.cpp
    SceneFile/SceneFileTests.cpp
    SceneFile/MaterialTextureTests.cpp
    SceneFile/ModelReferenceTests.cpp
    Runtime/FixedTimestepTests.cpp
    Runtime/CommandLineTests.cpp
    Runtime/RunReportTests.cpp
    UI/ThemeTests.cpp
    UI/DrawListTests.cpp
    UI/LayoutTests.cpp
    UI/ScaleTests.cpp
    UI/FontTests.cpp
    UI/SvgTests.cpp
    UI/AssetIntegrityTests.cpp
    UI/StartupTests.cpp
    UI/LogBufferTests.cpp
    UI/WindowPlanTests.cpp
    UI/ProgressBarTests.cpp
    UI/SvgFilterTests.cpp
    UI/LoadingScreenTests.cpp)
target_link_libraries(ZodarTests PRIVATE Zodar::Runtime Zodar::Renderer Zodar::AssetPipeline Zodar::Scene Zodar::World Zodar::WorldRender Zodar::SceneFile Zodar::AssetManager Zodar::UI Zodar::stb doctest::doctest)
# UI tests read the committed resources directly (fonts, original logos, manifest).
target_compile_definitions(ZodarTests PRIVATE ZODAR_TEST_UI_DIR="${ZODAR_ENGINE_DIR}/ui/Resources/UI"
    ZODAR_EXAMPLES_DIR="${ZODAR_ENGINE_DIR}/../examples" ZODAR_TEST_DATA_DIR="${ZODAR_ENGINE_DIR}/tests/data")
zodar_set_warnings(ZodarTests)

add_test(NAME unit COMMAND ZodarTests)
# Asset registry CLI on the example (CPU only): fails if the committed assets.json is stale.
if(TARGET ZodarAssetTool)
    add_test(NAME tools.asset_check_example
        COMMAND ZodarAssetTool check --strict --registry ${ZODAR_ENGINE_DIR}/../examples/asset_loading/assets.json
                --report ${CMAKE_BINARY_DIR}/asset_check_example.json)
    # --palette override with colours the example does not use: --strict must fail (exit 1).
    add_test(NAME tools.asset_check_palette_strict
        COMMAND ZodarAssetTool check --strict --palette "#000000,#ffffff"
                --registry ${ZODAR_ENGINE_DIR}/../examples/asset_loading/assets.json)
    set_tests_properties(tools.asset_check_palette_strict PROPERTIES WILL_FAIL TRUE)
    # Malformed numbers are a usage error, not a crash (std::terminate).
    add_test(NAME tools.asset_tool_bad_arguments
        COMMAND ZodarAssetTool check --density-target abc
                --registry ${ZODAR_ENGINE_DIR}/../examples/asset_loading/assets.json)
    set_tests_properties(tools.asset_tool_bad_arguments PROPERTIES
        PASS_REGULAR_EXPRESSION "--density-target needs a positive number")
    # Phase D1: the generated Blockbench/Blender fixtures pass the strict check (committed
    # registry with palette, defaults and budgets), and batch import of a directory works.
    set(_gltf_tools ${ZODAR_ENGINE_DIR}/tests/data/gltf_tools)
    add_test(NAME tools.asset_check_gltf_tools
        COMMAND ZodarAssetTool check --strict --registry ${_gltf_tools}/assets.json
                --report ${CMAKE_BINARY_DIR}/asset_check_gltf_tools.json)
    add_test(NAME tools.asset_import_dir_setup
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/asset_import_dir)
    add_test(NAME tools.asset_import_dir_copy
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_gltf_tools}/valid ${CMAKE_BINARY_DIR}/asset_import_dir/valid)
    add_test(NAME tools.asset_import_dir
        COMMAND ZodarAssetTool import ${CMAKE_BINARY_DIR}/asset_import_dir
                --registry ${CMAKE_BINARY_DIR}/asset_import_dir/assets.json)
    set_tests_properties(tools.asset_import_dir_setup PROPERTIES FIXTURES_SETUP asset_import_dir_clean)
    set_tests_properties(tools.asset_import_dir_copy PROPERTIES FIXTURES_SETUP asset_import_dir_files
        FIXTURES_REQUIRED asset_import_dir_clean)
    set_tests_properties(tools.asset_import_dir PROPERTIES FIXTURES_REQUIRED asset_import_dir_files
        PASS_REGULAR_EXPRESSION "imported blender_wall .*imported blockbench_crate .*imported blockbench_crate_embedded .*\\(3 assets\\)")
endif()
# The committed glTF fixtures match their generator (stdlib Python, deterministic).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME tools.gltf_fixtures_up_to_date
        COMMAND ${Python3_EXECUTABLE} ${ZODAR_ENGINE_DIR}/tests/data/gltf_tools/make_fixtures.py --check)
endif()

# Temporary UI sandbox (SDL3 software renderer, offscreen): renders the component gallery
# and checks token colours at known pixels. Needs no GPU.
if(TARGET ZodarUISandbox)
    add_test(NAME ui.sandbox_smoke
        COMMAND ZodarUISandbox --frames 3 --check --software --scale 1.33
                --screenshot ${CMAKE_BINARY_DIR}/ui_sandbox_smoke.png)
    set_tests_properties(ui.sandbox_smoke PROPERTIES TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")

    # Loading window (Stage UI-2) with MOCK startup data, one test per scenario and monitor
    # profile, plus the live single-window flow (loading -> resize + re-centre -> launcher).
    set(_ui_loading
        "normal|normal|8|2560x1440|"
        "slow_shaders|slow-shaders|36.5|1920x1080|"
        "failure|failure|3.6|3840x2160|--open-log"
        "indeterminate|indeterminate|5|2560x1440|")
    foreach(_case IN LISTS _ui_loading)
        string(REPLACE "|" ";" _f "${_case}")
        list(GET _f 0 _name)
        list(GET _f 1 _scenario)
        list(GET _f 2 _at)
        list(GET _f 3 _monitor)
        list(LENGTH _f _n)
        set(_extra)
        if(_n GREATER 4)
            list(GET _f 4 _extra)
        endif()
        add_test(NAME ui.loading_${_name}
            COMMAND ZodarUISandbox --screen loading --scenario ${_scenario} --at ${_at} --monitor ${_monitor}
                    ${_extra} --frames 3 --check --software --screenshot ${CMAKE_BINARY_DIR}/ui_loading_${_name}.png)
        set_tests_properties(ui.loading_${_name} PROPERTIES TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
    endforeach()
    add_test(NAME ui.loading_flow
        COMMAND ZodarUISandbox --screen loading --scenario normal --speed 25 --monitor 2560x1440
                --exit-after-launcher --software --screenshot ${CMAKE_BINARY_DIR}/ui_loading_flow_launcher.png)
    set_tests_properties(ui.loading_flow PROPERTIES TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
endif()

# Runs the real sample on whatever Vulkan device and video driver are available
# (a GPU locally; lavapipe + SDL offscreen or Xvfb in the cloud). Fails on any
# validation error or if the frame cannot be read back.
if(ZODAR_GPU_TESTS)
    # Engine subsystems on a headless Vulkan device (no window), validation enforced.
    add_executable(ZodarGpuTests
        Gpu/main.cpp
        Gpu/ResourceTests.cpp
        Gpu/TextureTests.cpp
        Gpu/SceneRendererTests.cpp
        Gpu/RenderGraphTests.cpp
        Gpu/DebugOverlayTests.cpp
        Gpu/GpuSceneTests.cpp
        Gpu/UploadQueueTests.cpp
        Gpu/LightingTests.cpp
        Gpu/IblTests.cpp
        Gpu/WorldRenderTests.cpp
        Gpu/AssetManagerTests.cpp
        Gpu/VoxelMaterialTests.cpp)
    target_link_libraries(ZodarGpuTests PRIVATE Zodar::Renderer Zodar::WorldRender Zodar::SceneFile Zodar::AssetManager Zodar::Debug Zodar::AssetPipeline Zodar::Scene doctest::doctest)
    zodar_set_warnings(ZodarGpuTests)
    target_compile_definitions(ZodarGpuTests PRIVATE ZODAR_TEST_SHADER_DIR="${ZODAR_OUTPUT_DIR}/shaders"
        ZODAR_EXAMPLES_DIR="${ZODAR_ENGINE_DIR}/../examples" ZODAR_TEST_DATA_DIR="${ZODAR_ENGINE_DIR}/tests/data")
    zodar_compile_shaders(ZodarGpuTests SOURCES
        Gpu/Shaders/test_sample.vert
        Gpu/Shaders/test_sample.frag
        Gpu/Shaders/test_sample_array.frag)
    add_test(NAME gpu.subsystems COMMAND ZodarGpuTests)
    set_tests_properties(gpu.subsystems PROPERTIES TIMEOUT 300)

    # zodar::Application end to end: headless (no SDL_VIDEO_DRIVER needed), deterministic
    # fixed steps, hook order, scene render + screenshot, validation exit code.
    add_executable(ZodarRuntimeSmoke Runtime/RuntimeSmoke.cpp)
    target_link_libraries(ZodarRuntimeSmoke PRIVATE Zodar::Runtime Zodar::stb SDL3::SDL3)
    zodar_set_warnings(ZodarRuntimeSmoke)
    add_test(NAME gpu.runtime_headless
        COMMAND ZodarRuntimeSmoke --screenshot ${CMAKE_BINARY_DIR}/runtime_headless.ppm)
    # Standard CLI: PNG screenshot, capture sequence (incl. the last frame) and run report.
    add_test(NAME gpu.runtime_capture_report
        COMMAND ZodarRuntimeSmoke --frames 8 --screenshot ${CMAKE_BINARY_DIR}/runtime_capture/last.png
                --capture-frames 2,5,8 --capture-dir ${CMAKE_BINARY_DIR}/runtime_capture
                --report ${CMAKE_BINARY_DIR}/runtime_capture/report.json)
    set_tests_properties(gpu.runtime_headless gpu.runtime_capture_report PROPERTIES TIMEOUT 120)

    add_test(NAME gpu.hello_triangle
        COMMAND HelloTriangle --frames 120 --screenshot ${CMAKE_BINARY_DIR}/hello_triangle.ppm)
    set_tests_properties(gpu.hello_triangle PROPERTIES TIMEOUT 120)
    add_test(NAME gpu.model_viewer_demo
        COMMAND ModelViewer --frames 30 --width 640 --height 360 --screenshot ${CMAKE_BINARY_DIR}/model_viewer_demo.ppm)
    set_tests_properties(gpu.model_viewer_demo PROPERTIES TIMEOUT 300)
    add_test(NAME gpu.asset_loading_example
        COMMAND ZodarAssetLoading --headless --deterministic --frames 30 --width 640 --height 360
                --screenshot ${CMAKE_BINARY_DIR}/asset_loading_example.png)
    set_tests_properties(gpu.asset_loading_example PROPERTIES TIMEOUT 300)
    # Async path through the real frame loop (assets appear as uploads are acquired).
    add_test(NAME gpu.asset_loading_async
        COMMAND ZodarAssetLoading --headless --frames 90 --width 320 --height 180
                --screenshot ${CMAKE_BINARY_DIR}/asset_loading_async.png)
    set_tests_properties(gpu.asset_loading_async PROPERTIES TIMEOUT 300)
    add_test(NAME gpu.zodarbench_smoke
        COMMAND ZodarBench --objects 256 --lights 8 --width 640 --height 360 --warmup 5 --frames 20
                --output ${CMAKE_BINARY_DIR}/zodarbench_smoke.json)
    set_tests_properties(gpu.zodarbench_smoke PROPERTIES TIMEOUT 300)
    add_test(NAME gpu.zodarbench_unique_smoke
        COMMAND ZodarBench --scene unique --objects 256 --width 640 --height 360 --warmup 3 --frames 10
                --sync-validation --output ${CMAKE_BINARY_DIR}/zodarbench_unique_smoke.json)
    add_test(NAME gpu.zodarbench_city_smoke
        COMMAND ZodarBench --scene city --blocks 6 --width 640 --height 360 --warmup 3 --frames 10
                --sync-validation --output ${CMAKE_BINARY_DIR}/zodarbench_city_smoke.json)
    # The CPU-driven fallback path stays covered too.
    add_test(NAME gpu.zodarbench_city_cpu_path_smoke
        COMMAND ZodarBench --scene city --blocks 6 --path graph --width 640 --height 360 --warmup 3 --frames 10
                --sync-validation --output ${CMAKE_BINARY_DIR}/zodarbench_city_cpu_smoke.json)
    # Highest API path: without validation the instance requests Vulkan 1.4, so 1.4 drivers run
    # with effective version 1.4 (validated runs are limited to the installed layer's version).
    add_test(NAME gpu.zodarbench_highest_api_smoke
        COMMAND ZodarBench --scene city --blocks 6 --width 640 --height 360 --warmup 3 --frames 10
                --no-validation --output ${CMAKE_BINARY_DIR}/zodarbench_highest_api_smoke.json)
    set_tests_properties(gpu.zodarbench_unique_smoke gpu.zodarbench_city_smoke gpu.zodarbench_city_cpu_path_smoke
                         gpu.zodarbench_highest_api_smoke PROPERTIES TIMEOUT 300)
endif()
