# RAVEN Engine — Roadmap

Each milestone ends with: clean build (GCC/Clang, and MSVC once verified), passing tests,
validation-clean GPU runs, screenshots for graphics work, and updated docs. Nothing is marked
done before it is verified.

| # | Milestone | Status |
| --- | --- | --- |
| 1 | Engine foundation | **Done** (verified on Linux/lavapipe; Windows pending local run) |
| 2 | 3D renderer core | **In progress**: resources, camera/depth, meshes, textures, materials, glTF done; shadows + ImGui overlay remaining |
| 3 | Lighting, post-processing and graphics settings | Not started |
| 4 | First DEAD SECTOR prototype scene | Not started |
| 5 | Gameplay foundation | Not started |
| 6 | World and game systems | Not started |
| 7 | Networking | Not started |
| 8 | Optimization and release | Not started |

## M1 — Engine foundation ✅
CMake/Ninja, C++20, SDL3 window, Vulkan 1.3 instance/device/swapchain, validation layers,
logging, error handling, triangle, unit tests, GPU smoke test with readback.

## M2 — 3D renderer core (style-neutral)
- GPU resources: RAII buffers/images via VMA, staging uploads, deferred deletion per frame.
- Descriptors: bindless texture table (descriptor indexing) + per-frame uniform data.
- Depth buffer (reverse-Z), camera (perspective/orthographic), debug fly camera.
- Meshes: vertex/index buffers, one engine vertex format, instanced draws.
- Textures: stb_image load, mip generation, sRGB/linear handling, sampler descriptions
  (linear/nearest, anisotropy, wrap) chosen per material.
- Materials: shader + parameter block + texture slots as data. Built-in shading models:
  `Unlit` and `PBR (metallic-roughness)`. Games can register their own shaders.
- Shader pipeline: shared GLSL include library, pipeline cache keyed by material shader +
  render state; custom vertex functions supported by the material interface.
- glTF 2.0 loading (cgltf) into engine meshes/materials/textures.
- Lights: directional + point (forward shading, light list uploaded per frame).
- Shadow mapping for the directional light; resolution/filtering configurable.
- Offscreen HDR scene target → minimal resolve to swapchain (tone mapping arrives in M3).
- Dear ImGui debug overlay: frame time, GPU info, camera, light/shadow controls.
- Accept: a standard glTF test scene renders validation-clean; screenshots on lavapipe in the
  cloud and on a real GPU locally; unit tests for resource/material/glTF code paths.

## M3 — Lighting, post-processing and graphics settings (style-neutral)
- Spot lights, shadow quality tiers, cascaded or fitted directional shadows.
- Post-process stack: ordered data-driven passes; built-ins tone mapping (ACES/AgX/none),
  bloom, SSAO, distance + height fog, colour LUT; API for custom passes.
- Internal render resolution: scale factor or fixed size, upscale filter linear/nearest.
- `GraphicsSettings` struct + runtime apply without reloading assets.
- Prepared interfaces (not implementations): TAA jitter/history, volumetrics, reflections,
  GPU-driven culling.
- Accept: two deliberately different sample looks (e.g. realistic PBR scene and a flat
  unlit/low-res scene) built purely from settings + materials, no engine code changes.

## M4 — First DEAD SECTOR prototype
Game target `Games/DeadSector` created. Small urban street, several buildings,
concrete/asphalt materials, props, dark corridor, first-person camera, flashlight (spot light +
shadow), imported weapon model (FBX → glTF via the asset compiler using ufbx), basic movement.
DEAD SECTOR's own PSX-inspired style preset (game shaders: vertex snapping, affine UV option,
dither/quantize pass; low internal resolution with nearest upscale) built only on engine
capabilities. Improved textures/models/lighting while keeping the PSX art direction.

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
