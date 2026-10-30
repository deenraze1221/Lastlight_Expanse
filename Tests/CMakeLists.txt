add_executable(ZodarTests
    main.cpp
    Assets/GltfLoaderTests.cpp
    Assets/ImageDataTests.cpp
    Assets/MeshDataTests.cpp
    Core/LogTests.cpp
    Core/ProfilerTests.cpp
    Core/ErrorTests.cpp
    Core/ImageIOTests.cpp
    Renderer/CameraTests.cpp
    Renderer/DeletionQueueTests.cpp
    Renderer/FrustumTests.cpp
    Renderer/GraphPlannerTests.cpp
    Renderer/PipelineCacheTests.cpp
    Renderer/DeviceSelectionTests.cpp
    Renderer/SwapchainSupportTests.cpp
    Renderer/VulkanCheckTests.cpp
    UI/ThemeTests.cpp
    UI/DrawListTests.cpp
    UI/LayoutTests.cpp
    UI/ScaleTests.cpp
    UI/FontTests.cpp
    UI/SvgTests.cpp
    UI/AssetIntegrityTests.cpp)
target_link_libraries(ZodarTests PRIVATE Zodar::Renderer Zodar::UI Zodar::stb doctest::doctest)
# UI tests read the committed resources directly (fonts, original logos, manifest).
target_compile_definitions(ZodarTests PRIVATE ZODAR_TEST_UI_DIR="${PROJECT_SOURCE_DIR}/Resources/UI")
zodar_set_warnings(ZodarTests)

add_test(NAME unit COMMAND ZodarTests)

# Temporary UI sandbox (SDL3 software renderer, offscreen): renders the component gallery
# and checks token colours at known pixels. Needs no GPU.
if(TARGET ZodarUISandbox)
    add_test(NAME ui.sandbox_smoke
        COMMAND ZodarUISandbox --frames 3 --check --software --scale 1.33
                --screenshot ${CMAKE_BINARY_DIR}/ui_sandbox_smoke.png)
    set_tests_properties(ui.sandbox_smoke PROPERTIES TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
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
        Gpu/DebugOverlayTests.cpp)
    target_link_libraries(ZodarGpuTests PRIVATE Zodar::Renderer Zodar::Debug doctest::doctest)
    zodar_set_warnings(ZodarGpuTests)
    target_compile_definitions(ZodarGpuTests PRIVATE ZODAR_TEST_SHADER_DIR="${ZODAR_OUTPUT_DIR}/shaders")
    zodar_compile_shaders(ZodarGpuTests SOURCES
        Gpu/Shaders/test_sample.vert
        Gpu/Shaders/test_sample.frag)
    add_test(NAME gpu.subsystems COMMAND ZodarGpuTests)
    set_tests_properties(gpu.subsystems PROPERTIES TIMEOUT 300)

    add_test(NAME gpu.hello_triangle
        COMMAND HelloTriangle --frames 120 --screenshot ${CMAKE_BINARY_DIR}/hello_triangle.ppm)
    set_tests_properties(gpu.hello_triangle PROPERTIES TIMEOUT 120)
    add_test(NAME gpu.model_viewer_demo
        COMMAND ModelViewer --frames 30 --width 640 --height 360 --screenshot ${CMAKE_BINARY_DIR}/model_viewer_demo.ppm)
    set_tests_properties(gpu.model_viewer_demo PROPERTIES TIMEOUT 300)
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
    set_tests_properties(gpu.zodarbench_unique_smoke gpu.zodarbench_city_smoke PROPERTIES TIMEOUT 300)
endif()
