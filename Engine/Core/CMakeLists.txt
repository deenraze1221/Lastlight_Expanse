add_library(RavenCore STATIC
    Log.cpp
    Error.cpp
    ImageIO.cpp)
add_library(Raven::Core ALIAS RavenCore)

target_include_directories(RavenCore PUBLIC ${PROJECT_SOURCE_DIR}/Engine)
target_link_libraries(RavenCore PUBLIC spdlog::spdlog glm::glm)
target_compile_definitions(RavenCore PUBLIC
    RAVEN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RAVEN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RAVEN_VERSION_PATCH=${PROJECT_VERSION_PATCH})
raven_set_warnings(RavenCore)
