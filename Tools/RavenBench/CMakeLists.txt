add_executable(RavenBench main.cpp)
target_link_libraries(RavenBench PRIVATE Raven::Renderer Raven::Debug SDL3::SDL3)
raven_set_warnings(RavenBench)
