add_library(RavenCore STATIC
    Log.cpp
    Error.cpp
    ImageIO.cpp
    Profiler.cpp)
add_library(Raven::Core ALIAS RavenCore)

target_include_directories(RavenCore PUBLIC ${PROJECT_SOURCE_DIR}/Engine)
target_link_libraries(RavenCore PUBLIC spdlog::spdlog glm::glm)
target_compile_definitions(RavenCore PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL
    RAVEN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RAVEN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RAVEN_VERSION_PATCH=${PROJECT_VERSION_PATCH})
raven_set_warnings(RavenCore)
