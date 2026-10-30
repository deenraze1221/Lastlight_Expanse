# RAVEN Engine — Roadmap

Each milestone ends with: clean build (GCC/Clang, and MSVC once verified), passing tests,
validation-clean GPU runs, screenshots for graphics work, and updated docs. Nothing is marked
done before it is verified.

| # | Milestone | Status |
| --- | --- | --- |
| 1 | Engine foundation | **Done** (verified on Linux/lavapipe; Windows pending local run) |
| 2 | 3D renderer | Not started |
| 3 | Graphics profiles (HIGH QUALITY / RETRO) | Not started |
| 4 | First DEAD SECTOR prototype scene | Not started |
| 5 | Gameplay foundation | Not started |
| 6 | World and game systems | Not started |
| 7 | Networking | Not started |
| 8 | Optimization and release | Not started |

## M1 — Engine foundation ✅
CMake/Ninja, C++20, SDL3 window, Vulkan 1.3 instance/device/swapchain, validation layers,
logging, error handling, triangle, unit tests, GPU smoke test with readback.

## M2 — 3D renderer
- Engine math types on GLM; camera (perspective, reverse-Z depth).
- GPU buffers/images through VMA with RAII handles; staging uploads; deferred deletion queue.
- Depth buffer; vertex/index buffers; mesh rendering.
- Descriptor management (descriptor indexing / bindless textures).
- Texture loading (stb_image), mipmaps, samplers with anisotropy.
- Material system (data-driven parameters + textures).
- glTF loading (cgltf) into engine mesh/material structures.
- Directional + point lights; shadow mapping for the directional light.
- Dear ImGui debug overlay (frame timings, GPU info, render settings).
- Accept: Sponza-class glTF renders validation-clean; screenshots on GPU and lavapipe.

## M3 — Graphics profiles
- HIGH QUALITY: metallic-roughness PBR, HDR target, tone mapping (ACES/AgX), bloom, SSAO,
  colour grading LUT, distance + height fog, spot lights, configurable shadow resolution,
  render-scale.
- RETRO: internal resolution 320×180/426×240/640×360, nearest upscale, vertex snapping,
  affine UV option, colour quantization, ordered dithering, retro grading/fog.
- Runtime profile switching without reloading assets.
- Prepared (interfaces only): TAA jitter/history, volumetrics, reflections, GPU-driven culling.

## M4 — First DEAD SECTOR prototype
Small urban street, several buildings, concrete/asphalt materials, props, dark corridor,
first-person camera, flashlight (spot light + shadow), imported weapon model (FBX → glTF via
the asset compiler using ufbx), basic movement. Same scene in both profiles.

## M5 — Gameplay foundation
Scene/ECS, transform hierarchy, input mapping, Jolt character controller and collision,
weapons/shooting/reloading per `weapon-config.js` values, basic skeletal animation,
miniaudio sources, basic HUD.

## M6 — World and game systems
Procedural city + interiors (seed-compatible port), navigation grid, enemy AI, waves,
economy, inventory, loot, missions. Golden tests compare against the JS implementation.

## M7 — Networking
Server-authoritative client/server, snapshot replication + client prediction, 4-player co-op,
Steam networking integration, automated multi-client tests.

## M8 — Optimization and release
CPU/GPU/memory profiling (Tracy, RenderDoc, GPU timestamps), async loading, shader/pipeline
caches, Windows packaging, Steam depots, Linux/Steam Deck validation, soak tests.
