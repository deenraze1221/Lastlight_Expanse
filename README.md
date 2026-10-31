# Lastlight Expanse

Voxel game built on **ZODAR**, a reusable C++23 / Vulkan 1.3+ engine (1.4 used when available) (CMake, Ninja, SDL3).
The engine was imported from [SECTOR-ENGINE](https://github.com/deenraze1221/SECTOR-ENGINE)
with its full history; engine code lives in `engine/` and never depends on `game/`.

## Quick start

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/LastlightExpanse            # the game
./build/bin/ModelViewer model.glb       # engine sample
ctest --test-dir build --output-on-failure
```

Game controls: hold right mouse to look, WASD/Space/Ctrl to fly, Shift fast, left click digs,
F places stone, F1 profiler, Esc quits. Options: `--seed`, `--view-radius`, `--threads`.

## Layout

```
engine/core/        Core (log, errors, profiler, JobSystem), Platform (SDL3 window, WSI)
engine/graphics/    Vulkan 1.3 layer: context, swapchain, pipelines, GPU resources, uploads
engine/rendering/   Frame loop, render graph, scene renderer, lighting, IBL, shaders
engine/assets/      CPU asset data, glTF, KTX2, offline asset pipeline
engine/scene/       Noise, voxel chunks, greedy meshing, LOD, chunk streaming
engine/physics/     planned (Jolt)
engine/audio/       planned (miniaudio)
engine/tools/       ImGui debug overlay, ZodarBench, ZodarAssetCompiler, ZodarUISandbox
engine/ui/          Engine/tooling UI + resources
engine/samples/     HelloTriangle, ModelViewer
engine/tests/       doctest unit tests, GPU tests
game/               Lastlight Expanse
reference/          DEAD SECTOR JS prototype (not built)
docs/               Architecture, roadmap, reports, specs
```

| Document | Contents |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Modules, dependency rules, decisions |
| [docs/AI_FIRST_ROADMAP.md](docs/AI_FIRST_ROADMAP.md) | AI-first engine audit and phased plan |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Engine milestones and rendering stages |
| [BUILDING.md](BUILDING.md) | Toolchain, dependency pins, build/test commands |
| [PROGRESS.md](PROGRESS.md) | Completed work, current task, next steps |
