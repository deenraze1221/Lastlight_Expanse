# ZODAR Engine — Roadmap

Each milestone ends with: clean build (GCC/Clang, and MSVC once verified), passing tests,
validation-clean GPU runs, screenshots for graphics work, and updated docs. Nothing is marked
done before it is verified.

| # | Milestone | Status |
| --- | --- | --- |
| 1 | Engine foundation | **Done** (verified on Linux/lavapipe; Windows pending local run) |
| 2+3 | Combined modern renderer (stages S1–S11 below) | **S1 complete, awaiting review** |
| 4 | First DEAD SECTOR prototype scene | Not started |
| 5 | Gameplay foundation | Not started |
| 6 | World and game systems | Not started |
| 7 | Networking | Not started |
| 8 | Optimization and release | Not started |

## M1 — Engine foundation ✅
CMake/Ninja, C++20, SDL3 window, Vulkan 1.3 instance/device/swapchain, validation layers,
logging, error handling, triangle, unit tests, GPU smoke test with readback.

## M2+M3 — Combined modern renderer (approved plan)

Done before the combined phase (M2 part 1): GPU resources, reverse-Z cameras, meshes, bindless
textures/samplers, materials (PBR/unlit + custom shaders), forward lighting, glTF 2.0.

| Stage | Contents | Tier | Status |
| --- | --- | --- | --- |
| S1 Foundation | Capability table + fallbacks, timeline semaphores + deletion queue, frame scratch allocator, render graph v1, scene/resolve ported (reference path kept), GPU timestamps + CPU timers, pipeline cache on disk, ImGui profiler, ZodarBench | 1 | **Done — awaiting review** |
| S2 GPU scene | Shared geometry buffers (meshoptimizer), vertex pulling, persistent instances, compute frustum culling, multi-draw indirect count | 1 | Planned |
| S3 Assets | Async transfer queue, KTX2 + BCn (asset compiler), mips, streaming design | 1 | Planned |
| S4 Lighting | Depth prepass (normals/roughness/velocity), clustered Forward+, spot lights, multi-scatter GGX, IBL, emissive | 1 | Planned |
| S5 Shadows | Stable CSM, spot/point shadow atlas, PCF, GPU caster culling, static caching, 1024–4096+ | 1 | Planned |
| S6 Post | Histogram auto-exposure, ACES/AgX, bloom, 3D LUT, TAA, dynamic resolution, `IUpscaler` + FSR | 1 | Planned |
| S7 AO / reflections / GI | GTAO, reflection probes, SSR with probe fallback, irradiance probe grid | 1–2 | Planned |
| S8 Volumetrics | Froxel fog + lighting with shadows, temporal accumulation, height fog | 2 | Planned |
| S9 Optimization | Two-phase HZB occlusion, meshlets + cluster culling, async compute, mesh-shader path | 2 | Planned |
| S10 Advanced | PCSS, contact shadows, LTC area lights, DOF, motion blur, chromatic aberration | 2 | Planned |
| S11 Experimental | DDGI / ray-traced GI, ray-traced reflections, SSGI, VSM, planar reflections, lightmaps | 3 | Planned (hardware/profiling dependent) |

Rules: one subsystem at a time; each stage ends with clean builds, passing unit + GPU tests
(synchronization validation on), screenshots, a Windows build and ZodarBench numbers measured on
a real GPU (RTX 4070). Software-rasterizer numbers are never reported as GPU performance. Every
optional GPU feature has a fallback; no vendor-specific feature is mandatory.

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

## Parallel track — ZODAR Tooling & UI

Runs alongside the milestones above without changing them. Engine-facing features use mock
services until the engine systems exist; no fabricated progress is shown in release builds.

| Stage | Contents | Status |
| --- | --- | --- |
| UI-1 Foundation | `Zodar::UI` (theme tokens, layout, scaling, fonts, SVG, draw lists, Button), temporary SDL sandbox, tests, Windows build | Done (`ui/stage1`) |
| UI-2 Loading window | Single window: loading at 960×540 / 1280×720 / 1920×1080, then resize + re-centre for the launcher; "Preparing shaders" without fabricated counts | UI side done — awaiting review. Demonstrated in the sandbox with mock data; the `Platform::Window` additions (borderless, size, centre, display scale) are deferred and need separate approval |
| UI-3 Launcher | Recent projects, templates (mock data) | Planned |
| UI-4 Create Project + Settings | After mockup approval; mock services | Planned |
| UI-5 Engine integration | Vulkan UI render-graph pass, real startup progress, settings storage, log viewer | Planned (as engine systems become ready) |
| Deferred | `.zodarproj` format and real project creation | Not started |
