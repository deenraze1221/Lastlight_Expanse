add_executable(RavenTests
    main.cpp
    Assets/GltfLoaderTests.cpp
    Assets/ImageDataTests.cpp
    Assets/MeshDataTests.cpp
    Core/LogTests.cpp
    Core/ErrorTests.cpp
    Core/ImageIOTests.cpp
    Renderer/CameraTests.cpp
    Renderer/FrustumTests.cpp
    Renderer/DeviceSelectionTests.cpp
    Renderer/SwapchainSupportTests.cpp
    Renderer/VulkanCheckTests.cpp)
target_link_libraries(RavenTests PRIVATE Raven::Renderer Raven::stb doctest::doctest)
raven_set_warnings(RavenTests)

add_test(NAME unit COMMAND RavenTests)

# Runs the real sample on whatever Vulkan device and video driver are available
# (a GPU locally; lavapipe + SDL offscreen or Xvfb in the cloud). Fails on any
# validation error or if the frame cannot be read back.
if(RAVEN_GPU_TESTS)
    # Engine subsystems on a headless Vulkan device (no window), validation enforced.
    add_executable(RavenGpuTests
        Gpu/main.cpp
        Gpu/ResourceTests.cpp
        Gpu/TextureTests.cpp
        Gpu/SceneRendererTests.cpp)
    target_link_libraries(RavenGpuTests PRIVATE Raven::Renderer doctest::doctest)
    raven_set_warnings(RavenGpuTests)
    target_compile_definitions(RavenGpuTests PRIVATE RAVEN_TEST_SHADER_DIR="${RAVEN_OUTPUT_DIR}/shaders")
    raven_compile_shaders(RavenGpuTests SOURCES
        Gpu/Shaders/test_sample.vert
        Gpu/Shaders/test_sample.frag)
    add_test(NAME gpu.subsystems COMMAND RavenGpuTests)
    set_tests_properties(gpu.subsystems PROPERTIES TIMEOUT 300)

    add_test(NAME gpu.hello_triangle
        COMMAND HelloTriangle --frames 120 --screenshot ${CMAKE_BINARY_DIR}/hello_triangle.ppm)
    set_tests_properties(gpu.hello_triangle PROPERTIES TIMEOUT 120)
    add_test(NAME gpu.model_viewer_demo
        COMMAND ModelViewer --frames 30 --width 640 --height 360 --screenshot ${CMAKE_BINARY_DIR}/model_viewer_demo.ppm)
    set_tests_properties(gpu.model_viewer_demo PROPERTIES TIMEOUT 300)
endif()
