add_library(ZodarCore STATIC
    Log.cpp
    Error.cpp
    ImageIO.cpp
    Profiler.cpp)
add_library(Zodar::Core ALIAS ZodarCore)

target_include_directories(ZodarCore PUBLIC ${PROJECT_SOURCE_DIR}/Engine)
target_link_libraries(ZodarCore PUBLIC spdlog::spdlog glm::glm)
target_compile_definitions(ZodarCore PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL
    ZODAR_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ZODAR_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ZODAR_VERSION_PATCH=${PROJECT_VERSION_PATCH})
zodar_set_warnings(ZodarCore)
