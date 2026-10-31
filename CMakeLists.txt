cmake_minimum_required(VERSION 3.25)

# Lastlight Expanse: the game (game/) and the reusable ZODAR engine it is built on (engine/).
project(LastlightExpanse
    VERSION 0.1.0
    DESCRIPTION "Lastlight Expanse, built on the ZODAR C++23 / Vulkan 1.3+ engine"
    LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build -G Ninja")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++23 toolchains verified for ZODAR: GCC 13, Clang 18 (with libstdc++ 13) and MinGW-w64 GCC 13.
# MSVC 2022 17.8+ is expected to work but is not verified yet. Only C++23 features available on
# all of them may be used (docs/ARCHITECTURE.md, "C++ standard").
option(ZODAR_ALLOW_UNSUPPORTED_COMPILER "Configure with compilers older than the verified minimums" OFF)
set(_zodar_min_version "")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_zodar_min_version 13.0)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(_zodar_min_version 18.0)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(_zodar_min_version 19.38) # Visual Studio 2022 17.8
endif()
if(_zodar_min_version AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS _zodar_min_version)
    set(_zodar_message "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} is older than the minimum "
                       "${_zodar_min_version} for C++23 (see BUILDING.md)")
    if(ZODAR_ALLOW_UNSUPPORTED_COMPILER)
        message(WARNING ${_zodar_message})
    else()
        message(FATAL_ERROR "${_zodar_message}. Set -DZODAR_ALLOW_UNSUPPORTED_COMPILER=ON to try anyway.")
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(ZODAR_BUILD_SAMPLES "Build engine samples" ON)
option(ZODAR_BUILD_TESTS "Build unit tests" ON)
option(ZODAR_BUILD_TOOLS "Build engine tools (ZodarBench)" ON)
option(ZODAR_GPU_TESTS "Register tests that need a Vulkan device and a video driver" OFF)
option(LASTLIGHT_BUILD_GAME "Build the Lastlight Expanse game" ON)
option(ZODAR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in engine code" OFF)

# All executables and compiled shaders land in one directory, for every configuration.
set(ZODAR_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ZODAR_OUTPUT_DIR})
foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${ZODAR_OUTPUT_DIR})
endforeach()

set(ZODAR_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(ZodarWarnings)
include(ZodarDependencies)
include(ZodarShaders)

if(ZODAR_BUILD_TESTS)
    enable_testing()
endif()

# Engine first: it must never depend on anything under game/.
add_subdirectory(engine)

if(LASTLIGHT_BUILD_GAME)
    add_subdirectory(game)
endif()
