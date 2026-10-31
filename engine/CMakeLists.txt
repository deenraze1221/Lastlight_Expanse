# ZODAR engine. A nested project so engine version macros stay independent of the game.
project(ZODAR
    VERSION 0.1.0
    DESCRIPTION "ZODAR Engine - modular C++23 / Vulkan 1.3+ game engine"
    LANGUAGES C CXX)

# Each engine module is a separate static library. Dependencies only point downwards:
#   core:      Core  <-  Platform
#   assets:    Core  <-  Assets (CPU data, importers)
#              Assets  <-  AssetPipeline (offline: BCn encoding, mip baking; tools and tests only)
#   graphics:  Assets + Platform  <-  Graphics (Vulkan context, swapchain, pipelines, GPU resources)
#   rendering: Graphics  <-  Renderer (frame loop, render graph, scene renderer, lighting)
#   scene:     Core + Assets  <-  Scene (voxel terrain, chunk streaming, LOD; no GPU)
#   world:     Core  <-  World (entities: zodar::World / Entity over EnTT; no GPU)
#   rendering: Renderer + World  <-  WorldRender (MeshRenderer, World -> SceneView draws / GpuScene sync)
#   tools:     Renderer  <-  Debug (ImGui tools)
#   runtime:   Renderer + Debug  <-  Runtime (Application: window/headless, frame loop, fixed timestep)
#   ui:        Core + Platform  <-  UI (renderer-independent: produces plain draw lists)
# Nothing in engine/ depends on a game.
add_subdirectory(core/Core)
add_subdirectory(core/Platform)
add_subdirectory(assets/Assets)
add_subdirectory(assets/AssetPipeline)
add_subdirectory(graphics)
add_subdirectory(rendering/Renderer)
add_subdirectory(scene)
add_subdirectory(world)
add_subdirectory(rendering/WorldRender)
add_subdirectory(tools/Debug)
add_subdirectory(runtime)
add_subdirectory(ui/UI)

if(ZODAR_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(ZODAR_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(ZODAR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
