# ZODAR Engine

> Formerly named RAVEN Engine (renamed after stage S1; commit history before the rename uses the old name).

Modular C++20 game engine with its own Vulkan 1.3 renderer. Built to be reused across
multiple games; **DEAD SECTOR** (currently HTML/Three.js) is the first game that will be
migrated onto it.

**Status: combined M2+M3 modern renderer, stage S1 (foundation) complete.** Vulkan 1.3
foundation, style-neutral 3D renderer core (GPU resources, reverse-Z cameras, meshes, bindless
textures, data-driven materials, forward lighting, glTF 2.0), and the S1 infrastructure:
device capability table with fallbacks, timeline semaphores and deferred deletion, frame
scratch memory, render graph v1, GPU/CPU profiling, on-disk pipeline cache, ImGui profiler
overlay and the ZodarBench benchmark. GPU-driven rendering, advanced lighting, shadows and
post-processing follow (S2–S11). No physics, audio or game code yet — see [ROADMAP.md](ROADMAP.md).

## Quick start

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/bin/ModelViewer            # procedural demo scene
./build/bin/ModelViewer model.glb  # any glTF 2.0 file
```

Windows, Linux, dependencies and test instructions: [BUILDING.md](BUILDING.md).

## Documents

| Document | Contents |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Module layout, dependency rules, ownership, frame flow, decisions |
| [ROADMAP.md](ROADMAP.md) | Milestones 1–8 and their acceptance criteria |
| [BUILDING.md](BUILDING.md) | Toolchain, dependency pins, build/test commands, cloud vs local testing |
| [Docs/MIGRATION_DEAD_SECTOR.md](Docs/MIGRATION_DEAD_SECTOR.md) | Map of every DEAD SECTOR JavaScript system to its future C++ home |

## Repository layout (current)

```
CMakeLists.txt          Root build
cmake/                  Dependency pins, warnings, shader compilation
Engine/Core/            Logging, errors, image output
Engine/Assets/          CPU meshes/images/models, primitives, glTF import
Engine/Platform/        SDL3 platform + window, Vulkan surface glue
Engine/Renderer/        Frame loop, scene renderer, materials, textures, meshes; RenderGraph/, Resources/, Vulkan/
Engine/Debug/           ImGui profiler overlay (engine tool)
Shaders/                Engine shader library (Include/zodar/*.glsl) and built-in shaders
Samples/                HelloTriangle (M1), ModelViewer (M2)
Tools/ZodarBench/       Deterministic benchmark with JSON reports
Tests/                  doctest unit tests + GPU smoke test registration
Docs/                   Design and migration documents
```

Directories listed in the long-term layout (Scene, Assets, Physics, Audio, Games/DeadSector,
Tools/AssetCompiler, ...) are created when their first real code lands, not as empty folders.
