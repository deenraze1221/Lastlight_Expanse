# RAVEN Engine

Modular C++20 game engine with its own Vulkan 1.3 renderer. Built to be reused across
multiple games; **DEAD SECTOR** (currently HTML/Three.js) is the first game that will be
migrated onto it.

**Status: Milestone 2 in progress.** Vulkan 1.3 foundation (M1) plus a style-neutral 3D
renderer core: GPU resource management, reverse-Z cameras, meshes, bindless textures and
samplers, data-driven materials (built-in PBR and unlit shaders, custom shaders supported),
forward lighting (sun + point lights), and glTF 2.0 loading. Shadows and the debug overlay are
next. No physics, audio or game code yet — see [ROADMAP.md](ROADMAP.md).

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
Engine/Renderer/        Frame loop, scene renderer, materials, textures, meshes; Resources/, Vulkan/
Shaders/                Engine shader library (Include/raven/*.glsl) and built-in shaders
Samples/                HelloTriangle (M1), ModelViewer (M2)
Tests/                  doctest unit tests + GPU smoke test registration
Docs/                   Design and migration documents
```

Directories listed in the long-term layout (Scene, Assets, Physics, Audio, Games/DeadSector,
Tools/AssetCompiler, ...) are created when their first real code lands, not as empty folders.
