# Application runtime: one standard way to start, run and shut down an engine app (window
# or headless), with a fixed simulation timestep and validation-aware exit codes.
add_library(ZodarRuntime STATIC
    Runtime/FixedTimestep.cpp
    Runtime/Application.cpp
    Runtime/SaveRuntime.cpp
    Runtime/CommandLine.cpp
    Runtime/RunReport.cpp)
add_library(Zodar::Runtime ALIAS ZodarRuntime)

# Headers are included as "Runtime/<File>.h".
target_include_directories(ZodarRuntime PUBLIC ${ZODAR_ENGINE_DIR}/runtime)
target_link_libraries(ZodarRuntime PUBLIC Zodar::Gameplay Zodar::Input Zodar::Renderer Zodar::Debug PRIVATE nlohmann_json::nlohmann_json SDL3::SDL3)
zodar_set_warnings(ZodarRuntime)
