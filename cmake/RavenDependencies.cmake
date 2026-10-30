# Third-party dependencies, pinned to release tags (commit hashes recorded in BUILDING.md).
# Only libraries required by the current milestone are fetched; later milestones add
# Dear ImGui, Jolt, miniaudio and ufbx when their systems are built.
include(FetchContent)

FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.4.304
    GIT_SHALLOW    TRUE
    SYSTEM)

# volk and VMA are built by us; SOURCE_SUBDIR points at a missing directory so their
# own CMakeLists (install rules, tests) are not processed.
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        1.4.304
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _raven_unused
    SYSTEM)

FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.3.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _raven_unused
    SYSTEM)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.24
    GIT_SHALLOW    TRUE
    SYSTEM)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
    GIT_SHALLOW    TRUE
    SYSTEM)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
    SYSTEM)

set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.12
    GIT_SHALLOW    TRUE
    SYSTEM)

# stb has no release tags; pinned by commit.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5
    SOURCE_SUBDIR  _raven_unused
    SYSTEM)

FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _raven_unused
    SYSTEM)

FetchContent_MakeAvailable(VulkanHeaders volk VulkanMemoryAllocator SDL3 spdlog glm stb cgltf)
if(RAVEN_BUILD_TESTS)
    FetchContent_MakeAvailable(doctest)
endif()

# volk: meta-loader. The engine never links the Vulkan loader directly; it is loaded at
# runtime through SDL so both use the same vkGetInstanceProcAddr.
add_library(raven_volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(raven_volk SYSTEM PUBLIC ${volk_SOURCE_DIR})
target_link_libraries(raven_volk PUBLIC Vulkan::Headers ${CMAKE_DL_LIBS})
target_compile_definitions(raven_volk PUBLIC VK_NO_PROTOTYPES)
add_library(Raven::volk ALIAS raven_volk)

# VMA is header-only; the implementation is compiled once in the renderer module.
add_library(raven_vma INTERFACE)
target_include_directories(raven_vma SYSTEM INTERFACE ${vulkanmemoryallocator_SOURCE_DIR}/include)
target_compile_definitions(raven_vma INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
target_link_libraries(raven_vma INTERFACE Raven::volk)
add_library(Raven::VMA ALIAS raven_vma)

# Header-only importers; implementations are compiled once inside Raven::Assets.
add_library(raven_stb INTERFACE)
target_include_directories(raven_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
add_library(Raven::stb ALIAS raven_stb)

add_library(raven_cgltf INTERFACE)
target_include_directories(raven_cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})
add_library(Raven::cgltf ALIAS raven_cgltf)
