add_executable(RavenTests
    main.cpp
    Core/LogTests.cpp
    Core/ErrorTests.cpp
    Core/ImageIOTests.cpp
    Renderer/DeviceSelectionTests.cpp
    Renderer/SwapchainSupportTests.cpp
    Renderer/VulkanCheckTests.cpp)
target_link_libraries(RavenTests PRIVATE Raven::Renderer doctest::doctest)
raven_set_warnings(RavenTests)

add_test(NAME unit COMMAND RavenTests)

# Runs the real sample on whatever Vulkan device and video driver are available
# (a GPU locally; lavapipe + SDL offscreen or Xvfb in the cloud). Fails on any
# validation error or if the frame cannot be read back.
if(RAVEN_GPU_TESTS)
    add_test(NAME gpu.hello_triangle
        COMMAND HelloTriangle --frames 120 --screenshot ${CMAKE_BINARY_DIR}/hello_triangle.ppm)
    set_tests_properties(gpu.hello_triangle PROPERTIES TIMEOUT 120)
endif()
