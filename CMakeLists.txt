cmake_minimum_required(VERSION 3.25)

project(RAVEN
    VERSION 0.1.0
    DESCRIPTION "RAVEN Engine - modular C++20 / Vulkan 1.3 game engine"
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

option(RAVEN_BUILD_SAMPLES "Build engine samples" ON)
option(RAVEN_BUILD_TESTS "Build unit tests" ON)
option(RAVEN_BUILD_TOOLS "Build engine tools (RavenBench)" ON)
option(RAVEN_GPU_TESTS "Register tests that need a Vulkan device and a video driver" OFF)
option(RAVEN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in engine code" OFF)

# All executables and compiled shaders land in one directory, for every configuration.
set(RAVEN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${RAVEN_OUTPUT_DIR})
foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${RAVEN_OUTPUT_DIR})
endforeach()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(RavenWarnings)
include(RavenDependencies)
include(RavenShaders)

add_subdirectory(Engine)

if(RAVEN_BUILD_SAMPLES)
    add_subdirectory(Samples)
endif()

if(RAVEN_BUILD_TOOLS)
    add_subdirectory(Tools)
endif()

if(RAVEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
