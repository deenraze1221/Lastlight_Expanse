add_library(RavenPlatform STATIC
    Platform.cpp
    Window.cpp)
add_library(Raven::Platform ALIAS RavenPlatform)

target_link_libraries(RavenPlatform
    PUBLIC Raven::Core Raven::volk
    PRIVATE SDL3::SDL3)
raven_set_warnings(RavenPlatform)
