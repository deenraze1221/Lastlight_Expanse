# Third-party dependencies, pinned to release tags (commit hashes recorded in BUILDING.md).
# Only libraries required by the current milestone are fetched; later milestones add
# Jolt, miniaudio, ufbx and meshoptimizer when their systems are built.
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
    SOURCE_SUBDIR  _zodar_unused
    SYSTEM)

FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.3.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _zodar_unused
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
    SOURCE_SUBDIR  _zodar_unused
    SYSTEM)

FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _zodar_unused
    SYSTEM)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.4
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _zodar_unused
    SYSTEM)

# BCn: bcdec decodes (runtime fallback + tests); bc7enc_rdo encodes (asset compiler only).
# Neither has release tags; pinned by commit. Both MIT / public domain (Unlicense).
FetchContent_Declare(bcdec
    GIT_REPOSITORY https://github.com/iOrange/bcdec.git
    GIT_TAG        80859ed3b7afb1c527a2a99d70c61457bea72d0c
    SOURCE_SUBDIR  _zodar_unused
    SYSTEM)

FetchContent_Declare(bc7enc_rdo
    GIT_REPOSITORY https://github.com/richgel999/bc7enc_rdo.git
    GIT_TAG        b9438627eef73a1157e84201b6fa6eb2ffd6d9f0
    SOURCE_SUBDIR  _zodar_unused
    SYSTEM)

# SVG parsing + rasterization for the UI module (original brand logos). No release tags; pinned by commit.
FetchContent_Declare(nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG        239e102ec2c691f2902e20ace2ed36ee4a35cfe6
    SOURCE_SUBDIR  _zodar_unused
    SYSTEM)

# ECS backend for Zodar::World (header-only, MIT). Used only behind zodar::World / zodar::Entity.
FetchContent_Declare(entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.15.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _zodar_unused
    SYSTEM)

# JSON for scene/material files (Phase B2; header-only, MIT). Release tarball, hash-pinned.
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

FetchContent_MakeAvailable(VulkanHeaders volk VulkanMemoryAllocator SDL3 spdlog glm stb cgltf imgui bcdec bc7enc_rdo nanosvg entt nlohmann_json)
if(ZODAR_BUILD_TESTS)
    FetchContent_MakeAvailable(doctest)
endif()

# volk: meta-loader. The engine never links the Vulkan loader directly; it is loaded at
# runtime through SDL so both use the same vkGetInstanceProcAddr.
add_library(zodar_volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(zodar_volk SYSTEM PUBLIC ${volk_SOURCE_DIR})
target_link_libraries(zodar_volk PUBLIC Vulkan::Headers ${CMAKE_DL_LIBS})
target_compile_definitions(zodar_volk PUBLIC VK_NO_PROTOTYPES)
add_library(Zodar::volk ALIAS zodar_volk)

# VMA is header-only; the implementation is compiled once in the renderer module.
add_library(zodar_vma INTERFACE)
target_include_directories(zodar_vma SYSTEM INTERFACE ${vulkanmemoryallocator_SOURCE_DIR}/include)
target_compile_definitions(zodar_vma INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
target_link_libraries(zodar_vma INTERFACE Zodar::volk)
add_library(Zodar::VMA ALIAS zodar_vma)

# Header-only importers; implementations are compiled once inside Zodar::Assets.
add_library(zodar_stb INTERFACE)
target_include_directories(zodar_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
add_library(Zodar::stb ALIAS zodar_stb)

add_library(zodar_entt INTERFACE)
target_include_directories(zodar_entt SYSTEM INTERFACE ${entt_SOURCE_DIR}/src)
add_library(Zodar::EnTT ALIAS zodar_entt)

# Header-only; the implementation is compiled once inside Zodar::UI.
add_library(zodar_nanosvg INTERFACE)
target_include_directories(zodar_nanosvg SYSTEM INTERFACE ${nanosvg_SOURCE_DIR}/src)
add_library(Zodar::nanosvg ALIAS zodar_nanosvg)

add_library(zodar_cgltf INTERFACE)
target_include_directories(zodar_cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})
add_library(Zodar::cgltf ALIAS zodar_cgltf)

add_library(zodar_bcdec INTERFACE)
target_include_directories(zodar_bcdec SYSTEM INTERFACE ${bcdec_SOURCE_DIR})
add_library(Zodar::bcdec ALIAS zodar_bcdec)

# BC1-5 (rgbcx) and BC7 (bc7enc) block encoders.
add_library(zodar_bcenc STATIC ${bc7enc_rdo_SOURCE_DIR}/rgbcx.cpp ${bc7enc_rdo_SOURCE_DIR}/bc7enc.cpp)
target_include_directories(zodar_bcenc SYSTEM PUBLIC ${bc7enc_rdo_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zodar_bcenc PRIVATE -w)
endif()
add_library(Zodar::bcenc ALIAS zodar_bcenc)

# Dear ImGui core + SDL3 platform backend + Vulkan renderer backend (via volk).
add_library(zodar_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(zodar_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(zodar_imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(zodar_imgui PUBLIC Zodar::volk SDL3::SDL3)
add_library(Zodar::imgui ALIAS zodar_imgui)
