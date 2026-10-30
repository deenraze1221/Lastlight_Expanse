# RAVEN Engine

Modular C++20 game engine with its own Vulkan 1.3 renderer. Built to be reused across
multiple games; **DEAD SECTOR** (currently HTML/Three.js) is the first game that will be
migrated onto it.

**Status: Milestone 1 (engine foundation) complete.** The engine opens an SDL3 window,
initializes Vulkan 1.3 with validation, creates a swapchain and renders a triangle using
dynamic rendering and synchronization2. There is no 3D renderer, asset pipeline, physics,
audio or game code yet — see [ROADMAP.md](ROADMAP.md).

## Quick start

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/bin/HelloTriangle
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
Engine/Platform/        SDL3 platform + window, Vulkan surface glue
Engine/Renderer/        Frame orchestration (Renderer) and Vulkan backend (Vulkan/)
Samples/HelloTriangle/  Milestone 1 sample + its GLSL shaders
Tests/                  doctest unit tests + GPU smoke test registration
Docs/                   Design and migration documents
```

Directories listed in the long-term layout (Scene, Assets, Physics, Audio, Games/DeadSector,
Tools/AssetCompiler, ...) are created when their first real code lands, not as empty folders.
