cmake_minimum_required(VERSION 3.25)

# Lastlight Expanse: the game (game/) and the reusable ZODAR engine it is built on (engine/).
project(LastlightExpanse
    VERSION 0.1.0
    DESCRIPTION "Lastlight Expanse, built on the ZODAR C++20 / Vulkan 1.3 engine"
    LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build -G Ninja")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
