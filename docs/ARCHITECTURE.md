# ZODAR Engine — Architecture

Lastlight Expanse repository (C++23, Vulkan 1.3 minimum / 1.4 when available): the reusable ZODAR engine lives in `engine/`, the game in `game/`.
Engine code never includes game headers or links game targets.

## Repository layout

| Directory | Module (CMake target) | Contents |
| --- | --- | --- |
| `engine/core/Core` | `Zodar::Core` | Log, Error, ImageIO, CPU profiler, `JobSystem` |
| `engine/core/Platform` | `Zodar::Platform` | SDL3 lifetime, window, Vulkan WSI glue |
| `engine/graphics/Renderer/{Vulkan,Resources}` | `Zodar::Graphics` | Vulkan context, swapchain, pipelines, capabilities, timeline, pipeline cache, GPU buffers/images, uploads, deletion queue, frame allocator |
| `engine/rendering/Renderer` | `Zodar::Renderer` | Frame loop, render graph, scene renderer, GPU scene, materials, lighting, IBL, `StreamedMeshSet` |
| `engine/rendering/Shaders` | — | Engine shader library (`Include/zodar/*.glsl`) and built-in shaders |
| `engine/assets/Assets` | `Zodar::Assets` | CPU meshes/images/models, glTF, KTX2, texture cache, `AssetCache<T>` (ref-counted handles), texture size standards (C) |
| `engine/assets/AssetPipeline` | `Zodar::AssetPipeline` | Offline BCn encoding, mip baking |
| `engine/scene/Scene` | `Zodar::Scene` | Noise, voxel chunks, greedy mesher, LOD, chunk streaming (no GPU) |
| `engine/world/World` | `Zodar::World` | Entities over EnTT: `zodar::World` / `zodar::Entity`, components, `Name`/`Transform`, hierarchy, `entities()`, component reflection (B1.1-B2; no GPU) |
| `engine/rendering/WorldRender` | `Zodar::WorldRender` | World -> renderer bridge: `MeshRenderer`, `collectDraws` (CPU path), `WorldRenderSync` (GpuScene) (B1.3); camera/light/environment/fog components, `collectLighting`/`collectCamera` (B2) |
| `engine/rendering/SceneFile` | `Zodar::SceneFile` | `*.scene.json` / `*.material.json`: parse + validate (`DataError`), serialize, `createSceneMaterials`, `instantiateScene` into a World (B2); material texture slots (C) |
| `engine/rendering/AssetManager` | `Zodar::AssetManager` | Runtime asset manager (C): cached GPU textures (PNG/JPEG/TGA/BMP), glTF models, material files with textures, scene materials; release via the deletion queue; async loads (C2); `AssetRegistry` (`assets.json`, import, hashes, checks; CPU only, C3) |
| `engine/physics`, `engine/audio` | — | Planned (Jolt, miniaudio); see their README files |
| `engine/runtime/Runtime` | `Zodar::Runtime` | `zodar::Application` (window or headless, standard frame loop, exit codes), `FixedTimestep`, standard command line + `runMain`, captures, run report JSON. Used by the game, ModelViewer and ZodarBench |
| `engine/tools` | `Zodar::Debug` + executables | ImGui profiler overlay, ZodarBench, ZodarAssetCompiler, ZodarAssetTool (asset registry CLI, C3), ZodarUISandbox |
| `engine/ui` | `Zodar::UI` | Engine/tooling UI, UI resources |
| `engine/samples`, `engine/tests` | — | HelloTriangle, ModelViewer; doctest unit + GPU tests |
| `game/` | `LastlightExpanse` | The game |
| `reference/DeadSector` | — | DEAD SECTOR JavaScript prototype (not built; migration reference) |

The renderer was split into `Zodar::Graphics` (low-level Vulkan) and `Zodar::Renderer` without
code changes: each module adds its own include root, so every header keeps its
`"Module/File.h"` include path (`"Renderer/Vulkan/VulkanContext.h"` etc.).

## Principles

1. **Universal engine, no enforced style.** The engine offers rendering, material, lighting
   and post-processing capabilities; art direction belongs to each game.
2. **Engine and games are separate.** Engine modules never contain game rules. A game is a
   separate CMake target (`game/` for Lastlight Expanse) that links engine modules through their public
   headers.
3. **One module = one static library** (`Zodar::Core`, `Zodar::Platform`, `Zodar::Renderer`, ...).
   Dependencies point downwards only and are enforced by CMake target links.
4. **Explicit ownership (RAII).** Every Vulkan object has exactly one owner whose destructor
   releases it. Owners are non-copyable. No global Vulkan state besides volk's function table.
5. **Fail loudly at init, recover at runtime.** Initialization failures throw `zodar::Error`
   with file/line. Expected runtime conditions (swapchain out of date, minimized window) are
   return values, not exceptions.
6. **Build only what a milestone needs.** Libraries and abstractions are added when a real
   feature requires them.

## Module graph

```
            game/          Samples/*   Tools/ZodarBench   Tests     Tools/ZodarAssetCompiler
              |                   |           /             /    \            |
          Runtime (Application)   |          /             /      \           |
                       \          |           /             /      \          |
                        +------ Debug (ImGui) ------+-------+      AssetPipeline (offline: BCn, mips)
                                     |                               |
                        +-------- Renderer -------+                  |
                                /          \                         |
                         Platform          Assets  <-----------------+  (CPU data, importers, KTX2; no GPU)
                                \          /
                                    Core
```

The runtime renderer never links `AssetPipeline` (encoders are offline-only).

Planned modules (by milestone) slot in as follows: `Scene`, `Assets`, `Physics`, `Audio`,
`Input`, `UI` depend on `Core` (and `Platform` where they touch the OS); `Renderer` consumes
`Scene`/`Assets` data through plain structs; `Networking` depends only on `Core`. `Debug`
(ImGui) depends on `Renderer` + `Platform`. Nothing depends on a game.

## Current modules

| Module | Files | Responsibility |
| --- | --- | --- |
| Core | `Log`, `Error`, `ImageIO` | spdlog-based engine logger, `zodar::Error` + `fail()` + `ZODAR_ENSURE`, PPM writer for screenshots |
| Platform | `Platform`, `Window` | SDL3 lifetime, window + event pump, Vulkan WSI glue (loader, instance extensions, surface) |
| Renderer | `Renderer` | Frames in flight, acquire/submit/present, swapchain rebuild, screenshot readback |
| Assets | `MeshData`, `ImageData`, `ModelData`, `Primitives`, `GltfLoader` | Engine vertex (64 B), mesh/tangent/normal generation, images (RGBA8 or BC1/3/4/5/7) with colour-space-correct mips, BCn→RGBA8 decode fallback (bcdec), glTF 2.0 import (cgltf, stb_image) |
| Assets | `Ktx2`, `TextureCache` (S3) | KTX 2.0 reader/writer (2D, no supercompression, levels smallest first, validated); compiled texture cache next to each model (`<model>.zodar/`), keyed by a hash of the source image + usage so stale data is never used |
| AssetPipeline | `TextureCompiler`, `ModelCompiler` (S3) | Offline: baked mips (engine filters), BC1/3/4/5 (rgbcx) and BC7 (bc7enc) encoding, deterministic across thread counts; per-model texture compilation; used by `ZodarAssetCompiler` and tests |
| Renderer/RenderGraph | `GraphPlanner`, `RenderGraph`, `RenderGraphResources` | Pure culling + barrier planning (unit tested); execution with dynamic rendering, debug labels, per-pass GPU timestamps; pooled transient images |
| Renderer | `GpuProfiler`, `FrameInfo` | Timestamp scopes per frame slot, optional pipeline-statistics scopes (fragment invocations, S2.1), VMA memory budget stats; per-frame recording context |
| Renderer/Resources | `DeletionQueue`, `FrameAllocator` | Timeline-based deferred destruction; per-frame linear scratch memory with device addresses |
| Renderer/Vulkan | `DeviceCapabilities`, `GpuTimeline`, `PipelineCache` | Capability table + feature resolution with fallbacks; graphics timeline semaphore; validated on-disk pipeline cache |
| Core | `Profiler` | CPU scope timers, frame-time history with percentiles |
| Debug | `DebugOverlay` | ImGui profiler overlay (F1), headless capable, drawn as a render-graph pass |
| Runtime | `Application`, `FixedTimestep` (A1) | Standard app runtime: owns Platform, Window, Renderer, UploadContext, SceneRenderer, RenderGraphResources, DebugOverlay, CpuProfiler, JobSystem; hooks `onStart/onEvent/onFixedUpdate/onUpdate/onRender/onShutdown`; fixed timestep with interpolation alpha; headless (SDL offscreen) and deterministic modes; exit codes 0/1/2/3. See "Application runtime" below |
| Renderer | `GpuScene` (S2) | Persistent GPU-resident instances (transform, normal matrix, material, world AABB); dirty-range uploads; ordered GPU frustum culling (cull + prefix-sum scan + scatter, S2.1); multi-draw indirect per pipeline; blended instances sorted on the CPU |
| Renderer | `GeometryPool` (S2) | Shared device-local vertex/index buffers (growable); pooled meshes can be drawn by one indirect call |
| Renderer/Vulkan | `ComputePipeline` (S2) | Compute pipelines with owned layout |
| UI | `Theme`, `DrawList`, `TextureStore`, `Layout`, `Scale`, `Font`, `Svg`, `Input`, `Context`, `Startup`, `LogBuffer`, `WindowPlan`, `Widgets/*`, `Screens/LoadingScreen` | Engine/tooling UI (ZODAR Tooling & UI track). Depends only on Core + Platform; no SDL3, no Renderer. Produces plain draw lists (sRGB colours) and CPU images; see [UI_SPEC.md](UI_SPEC.md) |
| Renderer | `SceneRenderer` | Forward renderer: HDR colour + reverse-Z depth targets, frame/instance buffers, culling, sorting, instancing, sun + point lights, resolve |
| | `MaterialSystem` | Named shaders, 128 B GPU material table, pipeline cache (shader × alpha mode × sidedness) |
| | `BindlessRegistry` | Global set 0: 4096 textures, 64 samplers, material table |
| | `Texture`, `Sampler`, `GpuMesh`, `Model`, `Camera`, `Frustum` | Sampled textures, sampler descriptions, GPU meshes, glTF models with override hooks, reverse-Z cameras, culling |
| Renderer/Resources | `GpuBuffer`, `GpuImage`, `UploadContext`, `UploadBatch` | VMA-backed RAII resources, batched staging uploads (blocking or async), graphics-queue readback |
| Renderer/Lighting | `ClusterMath`, `Lights`, `ClusteredLighting` (S4) | Light API (point/spot/sun, LightingSettings, debug views); pure cluster math (exponential slices, automatic ranges, spot cones; unit tested); GPU Clustered Forward+ (light upload, compute cull, per-cluster 16-bit lists, stats readback) |
| Renderer/Lighting | `Environment`, `IblBaker` (S4) | GPU IBL bake (compute): equirect → radiance cube + mips, irradiance cube, GGX-prefiltered cube, multi-scatter DFG LUT; bindless cube slots |
| Assets | `EnvironmentMaps` (S4) | Procedural sky / uniform equirect HDR images; `.hdr` loading in `ImageData` (Rgba16f) |
| Renderer/Resources | `UploadQueue` (S3) | Uploads on the dedicated transfer queue (fallback: graphics queue): non-blocking submit → ticket, timeline semaphore + fence, queue-family release/acquire barriers (pure `makeOwnershipTransfer`, unit tested), per-frame `acquireCompleted`, staging freed on completion |
| Renderer/Vulkan | `VulkanContext` | Instance, debug messenger, surface (optional: headless mode), GPU selection, device, queues, VMA allocator |
| | `VulkanSwapchain` | Swapchain, image views, per-image present semaphores, recreation |
| | `VulkanPipeline` | SPIR-V loading, `ShaderModule`, minimal `GraphicsPipeline` |
| | `DeviceSelection`, `SwapchainSupport` | Pure, unit-tested selection policies |
| | `VulkanCheck` | `ZODAR_VK_CHECK`, `VkResult` names |

## UI module boundary (Tooling & UI track, Stage 1)

- `Zodar::UI` links `Zodar::Core` and `Zodar::Platform`; third-party code (stb_truetype, nanosvg)
  is private and header-only. It never includes SDL3 or Renderer headers.
- Input: the UI has its own `ui::InputEvent` vocabulary. OS events are translated outside the
  module (today in the temporary sandbox; later in Platform, after approval).
- Output: `ui::DrawList` (indexed triangles, commands per texture + clip, colours sRGB with
  straight alpha) and `ui::TextureStore` (CPU RGBA8 images). A backend uploads and draws them.
  The future Vulkan UI pass must linearize colours for the sRGB swapchain, like `DebugOverlay`.
- Temporary backend: `Tools/ZodarUISandbox` draws with SDL3's 2D renderer. It is deleted once the
  Vulkan UI render-graph pass exists.
- Scaling is continuous (display scale x user scale, or fit-to-design for fixed compositions);
  glyph atlases and SVG rasters are generated for the scale in use.
- Engine state reaches the UI only through interfaces (`ui::IStartupProgress`, `ui::LogBuffer`).
  The UI never invents progress: without exact values it shows an indeterminate animation.
  Mock implementations live only in tests and the sandbox and label themselves as mock data.
- Window placement is computed by pure functions (`ui::WindowPlan`); applying it to a real
  window needs the deferred `Platform::Window` additions.

| Core | `JobSystem` | Fixed worker pool (std::jthread); 0 workers = inline, deterministic |
| Scene | `GradientNoise`, `hash64` | Seeded, platform-deterministic gradient noise, fBm, ridged noise |
| Scene/Voxel | `VoxelChunk` | 32³ `uint16` voxels (0 = empty), uniform chunks store no array |
| Scene/Voxel | `meshChunk` | Greedy mesher with per-vertex AO (merges only uniform-AO faces), LOD 0–5 by majority downsampling, boundary sealing |
| Scene/Voxel | `VoxelWorld` | Chunk streaming around an observer: nearest-first job scheduling with per-update budget, distance LOD, unload hysteresis, copy-on-write edits kept across unloads, DDA raycast, mesh events |
| Renderer | `TextureArray` | 2D array textures (`GpuImage` `arrayLayers`, per-layer uploads), bindless 2D_ARRAY view, `zodarSampleArray()` |
| Renderer/Lighting | `FogSettings` | Analytic distance + exponential height fog in frame constants (`zodar/fog.glsl`); applied by `pbr.frag`, off by default |
| Renderer | `StreamedMeshSet` | Runtime mesh residency: async transfer-queue uploads, old mesh drawn until the new one is ready, deferred deletion |

## Voxel terrain decisions

| Decision | Reason |
| --- | --- |
| Voxel data and meshing in `Zodar::Scene`, not the renderer | CPU-only, unit-testable, runs on workers; the renderer only sees `MeshData` |
| Copy-on-write chunks (`shared_ptr<const VoxelChunk>`) | Mesh jobs read 27 chunks lock-free while the main thread edits |
| Results applied sorted by chunk coordinate | Same mesh events with or without worker threads (tested) |
| LOD seams sealed with boundary walls; sealing skipped where the neighbour is empty or solid ≥ one coarse cell deep | Crack-free without stitching; no hidden walls inside solid ground |
| Below the lowest chunk layer reads as solid ("bedrock") | The world's underside is never meshed |
| Streamed chunk meshes own their buffers and use the CPU draw path | `GeometryPool` cannot free yet; per-mesh free + GPU-driven terrain is follow-up work |

## Lastlight Expanse visual style (game-side)

Official art direction, texture standards and the Blockbench → glTF asset workflow:
[ART_DIRECTION.md](ART_DIRECTION.md). The notes below describe what is implemented today.

Minecraft-inspired voxel look built only from engine capabilities (`game/`):
- Procedural pixel-art block textures (`game/src/Rendering/BlockTextures`): palette-quantised,
  tileable, 2×2-texel art pixels; 256² standard layers and 512² hero layers in two
  `TextureArray`s; alpha < 1 marks emissive pixels.
- `game/shaders/voxel.frag` (with the engine `mesh.vert`): world-aligned UVs, layer from vertex
  alpha (`VoxelPalette::faceLayers`, ≥ 1000 selects the hero array), point sampling with
  linear mips, engine PBR lighting + IBL, emissive, frame fog. One material, one draw per chunk.
- Fog is an engine capability (`SceneLighting::fog`), tuned by the game for dusk haze.

## Vulkan decisions

| Decision | Reason |
| --- | --- |
| Vulkan 1.3 core, **dynamic rendering** + **synchronization2** required | No `VkRenderPass`/`VkFramebuffer` objects; simpler render graph later. Supported by every current desktop driver and Steam Deck (RADV). |
| **volk** meta-loader, loader obtained from SDL | No link-time dependency on `vulkan-1`; SDL and engine share one `vkGetInstanceProcAddr`. Device-level function pointers skip the loader trampoline. |
| **VMA** for all memory | Standard, correct sub-allocation; first used by screenshot readback. |
| 2 frames in flight, per-frame command pool reset | Bounded CPU/GPU overlap, cheap command recording. |
| Present semaphores **per swapchain image** | A present may still hold a semaphore when its frame slot is reused; per-image avoids the reuse hazard flagged by current validation layers. |
| Fence reset **after** successful acquire | An out-of-date early return never leaves a fence unsignalled (deadlock). |
| sRGB swapchain (`B8G8R8A8_SRGB` preferred) | Shaders write linear colour. Scenes will render to an HDR target and resolve into this through the post-process stack. |
| **Reverse-Z** depth (D32, clear 0, GREATER_OR_EQUAL), infinite far plane by default | Best depth precision for large scenes. |
| **Bindless** textures/samplers (descriptor indexing) + material SSBO | Any shader indexes any texture; materials are table entries, not descriptor sets. Samplers are independent of textures. |
| Shared scene pipeline layout: set 0 bindless; push constants = frame-data, instance-data and visible-list addresses; material index stored per instance | One layout for every material shader. Per-frame data lives in `FrameAllocator` scratch memory read through buffer device addresses (no per-frame descriptor sets). |
| **GPU-driven rendering** (S2) | `SceneView::gpuScene` selects it. Per frame the CPU uploads only changed instances; three compute passes cull every instance with the same world bounds and frustum planes as the CPU path (plus a 1e-5 conservative margin) and compact the visible ones deterministically: (1) frustum test + workgroup exclusive scan over a group-sorted order array, (2) scan of the per-workgroup counts, (3) scatter to `visible[]` and write each group's `firstInstance`/`instanceCount`. The visible list therefore has exactly the CPU path's draw order (pipeline, material, mesh, submesh, insertion order) — verified by reading it back in a GPU test — so early-Z behaviour and fragment-shader invocations match the CPU path. Static command fields are uploaded only when the structure changes. Drawing is one `vkCmdDrawIndexedIndirect` per pipeline. The CPU draw list path (`SceneView::draws`) and the reference path are kept; all three render byte-identical images (GPU tests + benchmark screenshots). GPU statistics are read back without stalls and lag by framesInFlight frames. |
| **Render graph** (S1) | Passes declare usage; the planner derives culling, layouts and sync2 barriers. v1 executes in declaration order on one queue, without memory aliasing. The pre-graph path is kept (`SceneRenderer::render/resolve`, `--reference-path`) and must produce byte-identical output (GPU test). |
| **Uploads on the transfer queue** (S3) | `UploadBatch` records copies into an `UploadQueue` submission on the dedicated transfer family. Images and new buffers are `EXCLUSIVE`: the transfer queue releases them (with the final layout transition) and the graphics queue acquires them — at the start of a frame (`Renderer::attachUploads` → `acquireCompleted`) or in a small graphics submission for blocking `submit()`. Buffers written while the graphics queue uses other ranges (geometry pool) use `CONCURRENT` sharing (`BufferDesc::sharedWithTransfer`). Every later graphics submission waits on the upload timeline at the highest acquired value (already signalled → no stall), which makes all transfer writes visible. `ContextDesc::dedicatedTransferQueue = false` / `ZodarBench --no-transfer-queue` force the graphics-queue fallback for A/B tests. |
| **Compiled textures** (S3) | `ZodarAssetCompiler` bakes mips with the engine's filters and encodes BC7 (all model textures by default) into KTX2; `loadGltf` uses them when the source hash matches and falls back to decoding the source image with runtime mip generation (logged) otherwise. `Texture` uploads BCn directly; devices without BC sampling get a CPU decode (bcdec). Streaming is designed in `ASSET_STREAMING.md`, not implemented. |
| **Lighting** (S4) | Clustered Forward+ (Olsson 2012) with exponential depth slices; one compute thread per cluster streams all lights through shared memory and writes a fixed-capacity, ascending-order 16-bit light list, so clustered shading is byte-identical to evaluating every light (GPU test). Lights live in a device-local buffer (copied from frame scratch each frame); shaders read lights and lists through buffer device addresses in the frame constants. Shading: GGX + height-correlated Smith + Schlick + Lambert with multiple-scattering energy compensation from a baked DFG LUT; IBL via irradiance cube + GGX-prefiltered cube + DFG LUT (split sum), constant ambient without an environment. Optional depth-only prepass with `invariant gl_Position`; the shading pass keeps its pipelines (GREATER_OR_EQUAL, depth write of identical values), so images are identical with and without prepass. Design notes and results: `S4_REPORT.md`. |
| **Timeline semaphore** for all graphics submissions (`VulkanContext::submitGraphics`) | One ordering source for frames, uploads and deferred deletion. Blocking upload waits additionally use a fence because the 1.3.275 synchronization-validation layer does not model `vkWaitSemaphores` host waits. |
| **API version**: Vulkan 1.3 minimum, 1.4 requested (`ApiVersion.h`) | Instance requests 1.4, limited to the enabled validation layer's version; effective version = min(request, device); VMA/ImGui use the effective version. Vulkan 1.4 is detected (`ResolvedFeatures::vulkan14`) but nothing uses it yet, so behaviour is identical on 1.3 and 1.4 devices. Future 1.4 features (push descriptors, maintenance5/6, host image copy, 8-bit indices) must each get a capability entry with a 1.3 fallback. |
| **Capability baseline**: Vulkan 1.3 + dynamic rendering, sync2, descriptor indexing, timeline semaphores, buffer device address, indirect count/multi-draw/first-instance, shader draw parameters | Every current NVIDIA/AMD/Intel desktop driver, RADV (Steam Deck) and lavapipe. Optional features (mesh shaders, ray tracing, async compute, BC, timestamps, memory budget, anisotropy) resolve to an explicit fallback; mesh shaders/RT are detected but not enabled until their paths exist. |
| Display output encoding | The resolve writes sRGB-encoded values: hardware does it for `_SRGB` swapchains; the shader does it for UNORM-only surfaces. |
| Forward shading into an RGBA16F target | Simple, MSAA-friendly, enough for M2 light counts; resolve/post stack on top. |
| Deferred destruction by frame number | Render targets and bindless slots are released only after frames that used them completed. |
| Validation on by default, errors counted | `HelloTriangle` exits with code 2 if any validation error occurred; the GPU test relies on it. |

## Application runtime (Phase A1)

| Decision | Reason |
| --- | --- |
| One `zodar::Application` base class with virtual hooks | One obvious way to start/run/stop an app; removes ~150 copied lines per executable |
| Subsystems built in the constructor, hooks run only in `run()` | Hooks can rely on every subsystem; no virtual calls during construction |
| Derived members destroyed before engine subsystems; `run()` waits for the GPU first | Game-owned GPU resources (meshes, textures, environments) are released while the renderer is alive |
| Headless = SDL `offscreen` video driver (hint set with override priority before `SDL_Init`) | Same swapchain path as windowed runs (VK_EXT_headless_surface), no env var needed; requires `maxFrames` |
| `FixedTimestep`: accumulator, `maxSteps` per frame, `maxFrameTime` clamp, dropped-time counter | Deterministic simulation independent of frame rate; no spiral of death |
| `deterministic`: one fixed step per frame, frame dt = fixedDt | Same input -> same frames for agent tests and screenshots |
| Escape requests quit in the runtime; overlay gets events first | Same behaviour as the hand-written loops it replaces |

### Standard command line, captures and run report (Phase A2)

| Decision | Reason |
| --- | --- |
| One parser (`parseStandardOptions`) fills `ApplicationDesc`; unknown arguments go back to the app | Every app accepts the same flags; apps keep their own options without a second framework |
| `runMain()` owns `main()` duties: parse, `--help`, log file, construct, run, exception -> exit 1 | One obvious entry point; the log file opens before any subsystem logs |
| Screenshots by extension: `.png` (stb_image_write, compiled `STB_IMAGE_WRITE_STATIC` in Core) or PPM | Agents read PNG; PPM tests keep working; static stb avoids clashes with tools that define it too |
| Captures use the renderer's screenshot request; one readback can be written to several paths (`requestScreenshot(std::vector<path>)`), so the last frame can be both `--screenshot` and a capture | Additive renderer change; the readback is already synchronous in `endFrame`; each file is encoded in its own format |
| Missing screenshot or capture -> exit 3; unwritable report -> exit 1 | A run that asked for evidence and produced none must fail loudly |
| Run report JSON written by hand (`RunReport.cpp`, fmt) with a versioned schema | No JSON dependency for one writer; agents parse a stable layout |
| Samples are collected only with `--report` | Interactive runs do not grow memory |
| Log warning/error counts via a counting spdlog sink in `Core/Log` | The report shows log health without parsing logs |

Frame order: events -> `onFixedUpdate` x N -> `onUpdate` -> `beginFrame` -> `onRender` (records
into the frame's render graph, swapchain image imported as `output`) -> debug overlay pass ->
graph execute -> `endFrame`. The screenshot is requested before the last frame's `beginFrame`.
Migrated: `game/` (A1), ModelViewer and ZodarBench (A3). HelloTriangle (Milestone 1 raw
Vulkan sample) and ZodarUISandbox (SDL renderer) intentionally keep their own loops.

### Agent tooling and remaining migrations (Phase A3)

| Decision | Reason |
| --- | --- |
| `onFrameEnd(FrameTime)` hook after `endFrame` (rendered frames only) | Per-frame statistics and frame pacing (ZodarBench `--frame-cap`) outside the measured CPU frame |
| `RenderContext::frame` exposes the raw `FrameContext` | A/B reference paths (`--reference-path`, `--path reference`) record outside the render graph without a second loop; they hide the overlay as before |
| `requestCapture(path)`: next rendered frame, recorded and verified like `--capture-frames` (exit 3) | Apps choose capture frames (ZodarBench mid-path screenshot) without bypassing the evidence checks |
| Standard CPU scopes in the runtime: `Wait + acquire`, `Build graph`, `Record`, `Submit + present` | Same frame breakdown in every app's overlay, run report and benchmark report |
| `ApplicationDesc::synchronizationValidation`, `dedicatedTransferQueue`; `--sync-validation` is standard | Correctness runs and S3 A/B switches without app-owned renderer construction |
| ZodarBench keeps its CLI: `--frames` = measured frames, `--screenshot` = measured frame `--screenshot-frame`; runtime renders warm-up + measured + frames in flight | Existing benchmark commands and reports stay valid. Changed: `cpuFrameMs` now spans the whole runtime frame (events + update included) and `cpuScopesMs` uses the standard scope names; compare CPU numbers only against A3+ runs (measured equal within noise on lavapipe) |
| Animation in ModelViewer uses the runtime clock (`FrameTime::dt`/`time`, clamped to 0.1 s) | Deterministic runs render identical frames; after a stall spin/light phase follow simulation time, not wall time |
| JSON-lines log sink (`log::Config::jsonFilePath`, `--log-json`): own spdlog sink, ISO 8601 UTC ms, escaped message | One object per line is greppable and parseable; no JSON dependency; opt-in |
| `CMakePresets.json` (debug, release, gpu-tests, ci + `ci` workflow), trees in `build/<preset>` (git-ignored) | One documented way to configure; each preset has its own `_deps` |
| `tools/zodar.py`: Python stdlib only, prints failures/summaries, full logs to files, returns the underlying exit code | Agents spend few tokens per build/test/run; no extra install |

## Entity world (Phases B1.1-B1.3)

- Module `Zodar::World` (`engine/world/World`), depends on `Core` + `Zodar::EnTT` only (EnTT
  v3.15.0, header-only, MIT, pinned in `cmake/ZodarDependencies.cmake`). No GPU, no Renderer.
- `zodar::Entity`: value handle wrapping a 32-bit id (index + version, EnTT layout);
  `Entity::null()` / default = `kNullId` (0xFFFFFFFF). Stale handles stay invalid after
  index reuse. Handles are only meaningful for the World that created them.
- `zodar::World`: owns an `entt::registry` privately. API: `create()`, `destroy(e)` (false for
  null/stale/foreign handles), `isAlive(e)`, `entityCount()`, `clear()`. Non-copyable, movable,
  single-threaded. Callers never touch EnTT types; the backend stays replaceable.
- EnTT headers are visible through `World.h` (components are templates from B1.2 on), but
  only `World/*.h` may include `<entt/...>`.
- Components (B1.2): `add<T>(e, args...)` (adds or replaces; throws `std::invalid_argument`
  for dead entities), `get<T>` (throws `std::out_of_range` if dead/missing), `tryGet<T>`
  (nullptr), `has<T>`, `remove<T>` (false if dead/missing). One instance per type per entity;
  returned references are invalidated by later add/remove of that type and by destroy/clear.
- `World/Components.h`: `Name { std::string value; }`; `Transform` (local position, unit quat
  rotation, scale; `localMatrix()` = T * R * S, glm).
- Hierarchy: internal `detail::HierarchyNode` (parent + ordered child vector), changed only via
  `setParent(child, parent|null)`; rejects dead entities, self and cycles. `parent`, `children`
  (span), `isDescendantOf`. Policy: `destroy(e)` destroys the whole subtree (iterative) and
  detaches `e` from its parent.
- `localMatrix(e)` / `worldMatrix(e)`: computed on demand, O(depth), identity for entities
  without `Transform` or dead. No cached world matrix / dirty flags yet: add when the renderer
  bridge (B1.3) needs per-frame bulk updates.
- `each<T...>(fn(Entity, const T&...))` (B1.3): read-only iteration over entities with all T
  (non-empty components only), in EnTT storage order (deterministic, newest-first; not
  insertion order).
- Renderer bridge (B1.3): module `Zodar::WorldRender` (`engine/rendering/WorldRender`, depends
  on World + Renderer; World stays GPU-free). Component `MeshRenderer { mesh, submesh,
  material, visible }` (raw `GpuMesh*`; asset handles come with Phase C).
  - `collectDraws(world, out)`: stateless CPU path, one `MeshDraw` per renderable entity at
    `worldMatrix(e)`; pass as `SceneView::draws`.
  - `WorldRenderSync(sink)`: persistent path. `sync(world)` diffs against the last sync: adds
    new renderables, `setTransform`/`setMaterial` on change, re-creates on mesh/submesh change
    (GpuScene has no setMesh), removes destroyed/hidden/component-less entities in entity-id
    order. Keyed by full entity id (index + version), so recycled ids get fresh instances.
    `RenderInstanceSink` interface; `GpuSceneSink` adapts `GpuScene` (fakes in unit tests).
  - World matrices recomputed every sync (O(entities x depth)); cached world matrix + dirty
    propagation deferred until profiling needs it.
  - Verified: GPU test renders a World hierarchy pixel-identical to hand-built `MeshDraw`s via
    both paths, before and after update/destroy.
- Scene / material files (B2): module `Zodar::SceneFile` (depends on WorldRender; nlohmann/json
  v3.11.3 linked privately, public headers expose engine types only). Schema:
  `docs/guide/SCENE_FILES.md`.
  - Two steps: `parseScene`/`loadSceneFile` -> `SceneDocument` (pure CPU data, `operator==`,
    deterministic `serializeScene`, exact float round-trip via a float-typed ordered JSON);
    `createSceneMaterials` (MaterialSystem) + `instantiateScene(doc, world, SceneBindings)`.
  - Meshes are referenced by name and bound by code (`SceneBindings::meshes`) until the Phase C
    asset manager; materials by scene-local id (inline or `*.material.json` path relative to
    the scene). `MaterialData` = `MaterialDesc` without GPU handles (shader by name, no textures).
  - Validation collects all issues before throwing `DataError` (file, line:column for syntax,
    JSON pointer for schema). Unknown fields are errors. Instantiation/material creation check
    everything first, so failures leave the World / MaterialSystem unchanged.
  - Schema errors get line:column from `JsonLocator` (scanner over the accepted text mapping
    JSON pointers to offsets; missing fields resolve to the nearest ancestor).
  - Reflection (`World/Reflection.h`): `ComponentReflection<T>` static field tables
    (name, `FieldType`, accessor, description, range). Scene components
    (`WorldRender/SceneComponents.h`: camera, directional/point/spot light, environment, fog)
    are read/written generically from them; `sceneComponentReference()` generates
    `docs/guide/SCENE_COMPONENTS.md` (unit test fails when stale). `collectLighting` /
    `collectCamera` feed `SceneView` (entity index order, direction = world -Z).
  - Prefabs expanded at parse time (flat document; ids `<instance>/<id>`, materials
    `<prefab path>#<id>`; overrides patch typed fields; nesting with cycle detection).
  - `captureScene` + `invertBindings`: live World -> document (`World::entities()` added).
  - Mutable materials: `MaterialSystem::updateMaterial` changes the CPU desc now and queues the
    table slot; `recordPendingUpdates` (start of `SceneRenderer::render` / a `MaterialUpdates`
    graph pass) writes it with `vkCmdUpdateBuffer` between ALL_COMMANDS barriers, so earlier
    submissions finish reading first. Pipeline state is immutable (GpuScene buckets by it).
  - Not yet: prefab references kept on save (documents are flat), IBL environment in files.
    Material texture slots exist since Phase C1 (loaded by the AssetManager).
  - Verified: GPU tests render `examples/scene_from_json` (lights, prefab) pixel-identical to the
    equivalent C++ scene via both WorldRender paths and after capture/reload; mutable material
    updates match freshly created materials (CPU path and GpuScene, sync validation clean).

## Asset manager (Phases C1-C7)

- Module `Zodar::AssetManager` (`engine/rendering/AssetManager`, depends on Renderer + SceneFile).
  Guide: `docs/guide/ASSETS.md`; example `examples/asset_loading` + sample `ZodarAssetLoading`.
- Generic CPU core `Assets/AssetCache.h`: `AssetCache<T>` maps a key (resolved absolute path +
  load settings) to a shared record; `AssetHandle<T>` shares ownership (`shared_ptr` with a
  deleter). The last handle erases the entry and hands the value to a retire function. Failed
  loads are records too (state + message), cached while held. Single-threaded by contract.
- GPU lifetime: retire = `VulkanContext::deletionQueue()` (frames in flight finish first).
  Materials get a new `MaterialSystem::releaseMaterial` (free list, LIFO reuse in
  `createMaterial`; the slot is written directly because the GPU no longer reads it). Models
  release their materials + glTF fallback material. Material assets hold their texture handles.
  The destructor waits idle and flushes the deletion queue; handles must be released first.
- Models are uploaded standalone (CPU draw path): `GeometryPool` cannot free meshes yet.
- Sampling: `gfx::SamplerDesc::pixelArt()` (nearest mag/min, linear mip, no anisotropy).
  Engine default stays linear (`AssetManagerDesc::defaultSampler`); games pick pixel art.
  glTF sampler filters are honoured end to end (GPU test checks the material's sampler).
- Texture standards (`Assets/TextureStandards.h`): see the revised bullet below (32 default,
  64 max, 8/16 allowed); anything else loads with a warning (asset warnings + log), never an error.
- Async loads (C2): `AssetCache` entries have a `Loading` state (`getOrCreatePending`,
  `resolve`, `reject`). Decode jobs run on `AssetManagerDesc::jobs` and push results (by
  request id) into a mutex-protected queue; they never touch handles or caches, so record
  deleters always run on the render thread. `update()` sorts results by request id, uploads
  within `uploadBudgetBytes` via `submitAsync`, keeps the GPU object staged until
  `UploadQueue::isReady` (acquired by the frame loop) and only then resolves the handle, so no
  frame can sample an unacquired image. Async materials are created when their textures are
  ready. Cancellation = all user handles dropped (request holds the last one).
  `waitAll()` = `JobSystem::waitIdle` + `UploadQueue::wait` per ticket.
- Registry (C3, `AssetManager/AssetRegistry.h`): `assets.json` (`zodar.asset_registry` v1)
  next to the assets; entries sorted by id; paths relative to the registry directory.
  Dependencies are discovered by parsing (glTF buffer/image URIs incl. GLB JSON chunk, material
  texture slots, scene material files, prefab files recursively, prefab inline-material
  textures via `sceneMaterialDirectory`). Hash = FNV-1a 64 over source + dependencies (paths
  relative to the source, sizes, bytes): location independent, no crypto needed. `check` never
  throws: errors = unusable (missing source/dependency, parse failure), warnings = stale hash,
  size standards, texel density (target 32 texels/m, ±2×), missing compiled outputs. Report
  JSON `zodar.asset_report` v1. CLI `ZodarAssetTool` (CPU only; `--compile` uses the existing
  `compileModelTextures` cache), wrapped by `zodar.py import|assets`; ctest
  `tools.asset_check_example` keeps the example registry current. The runtime loads by id via
  `registry.sourcePath(entry)` + `modelLoadSettings/textureLoadSettings`; the AssetManager
  itself stays path-based.
- Texture standards (revised): 32×32 default, 64×64 maximum for all assets, 8/16 allowed;
  classes Small/Default/Maximum (allowed), Oversized/NonStandard (warnings, never resized).
  Texel density target 32 texels/m (±2×).
- Pixel-art compression: pixel art (NEAREST glTF magnification, `GltfLoadOptions::pixelArt`,
  `ModelCompileOptions::pixelArt`, `--pixel-art`) compiles to RGBA8 KTX2 only
  (`compileTexture` throws for BCn + pixelArt); `loadGltf` rejects BCn caches for pixel-art
  textures (`textureStats.lossyRejected`) and the compiler rewrites them;
  `kTextureCompilerVersion` 2 invalidates v1 caches. BC7 remains for non-pixel-art textures.
- Scene models (C4): `ModelRenderer {const gfx::Model*, visible}` in WorldRender
  (`collectDraws` appends `Model::appendDraws`; CPU path only, like AssetManager models).
  Scene files: `modelRenderer {model, visible}`; reference rule: `.gltf`/`.glb` suffix = path
  (relative to the file, existence checked at parse, prefab-rebased), otherwise registry id
  (resolved by `AssetManager::loadSceneModels[Async]` with an optional `AssetRegistry`).
  Bound via `SceneBindings::models`; captured via `SceneCaptureNames::models`.
- Palettes (C7, `AssetRegistry.h`): optional `PaletteConfig` (master + named sub-palettes,
  each a PNG swatch or `#rrggbb` list) in `assets.json`; entries name a sub-palette
  (`AssetEntry::palette`, extends the master). `checkAsset` counts non-transparent (alpha > 0) level-0 texels of
  colour textures (texture usage color, material base colour/emissive, model images used as
  base colour/emissive) not in the palette: warning + `PaletteDeviation` + report keys, all
  absent without a palette (no behaviour change). Swatches are not hash dependencies. CPU only.
- Hot reload (C5): `AssetCache::replace` swaps the new value into the existing record
  (`std::swap` of the asset structs), so handles and addresses users hold stay valid and
  `generation()` increases; the previous content is retired like a released asset. Materials
  keep their `MaterialSystem` slot (`updateMaterial`); because `MaterialDesc` stores texture
  addresses and `toGpu` reads bindless slots at record time, a reloaded texture only needs
  `updateMaterial` on its users. Pipeline-state changes (shader/alphaMode/doubleSided) are
  rejected: `GpuScene` caches blend flags and groups per pipeline. Failed reloads change
  nothing. Reloads are synchronous (debug feature; reuses the blocking load path).
  `Core/FileWatcher` (size + mtime polling, portable, no threads) drives opt-in watching
  (`AssetManagerDesc::watchFiles`), compiled only into Debug builds (`ZODAR_HOT_RELOAD`,
  `kHotReloadAvailable`). Model dependencies come from `fingerprintAsset` (C3).
- Shader hot reload (C6): `MaterialSystem::reloadShader` re-reads the registered SPIR-V
  (paths stored normalised), checks header + entry-point stage + instruction bounds (so the
  usual broken files never reach `vkCreateShaderModule`), builds every cached colour/depth
  pipeline of the shader first, then swaps the Vulkan objects into the existing
  `GraphicsPipeline` objects (`GraphicsPipeline::swap`): `GpuScene` groups and other holders of
  `const GraphicsPipeline*` stay valid. Old pipelines + modules are retired on the deletion
  queue; any failure destroys only the unused new objects. Only material shaders reload
  (compute, resolve, sky and IBL pipelines do not). The AssetManager watches the SPIR-V files
  (`reloadFiles`, `reloadShader(name)`); SPIR-V is produced by the normal build.
- Scene hot reload (C6, `AssetManager/LiveScene.h`): a LiveScene owns the entities it
  instantiated. Reload = parse (prefab file list from `loadSceneFile(path, &files)`) ->
  models -> materials -> `instantiateScene` (validated, world untouched on error) ->
  changed inline materials via atomic `updateMaterialData` (failure destroys the new entities)
  -> destroy old entities. So every failure leaves the previous entities and handles alive.
  Entities are recreated rather than patched (simple, matches file order/ids); material handles
  are stable. Its own `FileWatcher` covers scene + prefab files; asset files are the
  AssetManager's.
- Not yet (later phases):
  streaming budgets (`docs/ASSET_STREAMING.md`), audio/FBX importers, glTF skins/animations;
  `meshRenderer` meshes are still bound by name from code (`SceneBindings::meshes`; whole
  glTF models use `modelRenderer`, C4).

## Phase D decisions (approved plan, not implemented)

Binding plan: `docs/PHASE_D_PLAN.md`.
- New engine modules: `Zodar::Input` (`engine/input/Input`: action maps, record/replay) and
  `Zodar::Gameplay` (`engine/gameplay/Gameplay`: `SystemScheduler`, `EventBus`, `Timers`,
  `SaveGame`; depends on World + SceneFile); `Core/Random` (seeded named streams).
- Determinism: simulation reads input only per fixed step; replays store action states (not
  raw events); events sent in step N are delivered in step N+1; timers count ticks.
- Scene files gain a registry for game-defined components; unregistered names stay errors.
- Asset pipeline is tool-agnostic: Blockbench (primary) and Blender (secondary) both export
  `.glb` into the same registry/check/hot-reload path with one art standard; the engine reads
  glTF `asset.generator` only for reporting and never parses `.bbmodel`/`.blend`.
- Collision until Phase E: game-side static AABBs behind `CollisionWorld` (replaceable by Jolt).
- `bunker` is the game's default level; `highlands` is the regression scene.
- All sources are regular Git files, no Git LFS (revised 2026-11-01): `.bbmodel` (plain JSON)
  and `.blend` (`binary`, compressed saves, ≤ 20 MB each, enforced by ctest
  `tools.game_source_sizes`, which also rejects `.blend1`/`.blend2` backups; no blanket size
  limit on other assets). `game/assets/**` is `-text` (registry hashes cover exact bytes).

## Phase V decisions: hybrid voxel + model world (approved V0, 2026-11-01; not implemented)

Specification: `docs/guide/VOXELS.md`; game rules: GDD §2/§3 Amendment 1.
- Voxel blocks: architecture and structural environment. Imported 3D models (glTF, D1
  pipeline): weapons, characters, mutants, furniture, generators, machinery, computers,
  vehicles, equipment and decorative props.
  Shared materials standard, lighting and renderer.
- Engine grid is configurable (`StreamingSettings::voxelSize`, name kept in V1); Lastlight uses 0.5 m cells (Full Block = 2×2×2
  cells, Half Block / Slab 2×2×1, Vertical Half Block 1×2×2, Sub-block = 1 cell). Highlands
  keeps 1 m cells and its own rendering path (regression scene, not migrated).
- Extend, do not duplicate: 16-bit cell ids stay (`VoxelChunk`), `VoxelPalette` grows into the
  block registry (id → material, shape, rotation), `ChunkMesher` gains a shape path next to the
  greedy cube path (cube-only output byte-identical, hash-tested), `VoxelWorld` gains
  shape-aware collision queries and raycasts, layered generators and region storage.
- Shapes are registered templates (geometry, per-face occlusion, collision boxes, fill
  fraction): extensible without core changes.
- Meshing chunk = 32³ cells (16 m at 0.5 m); world region = 64 × 64 m (4 × 4 columns): the
  streaming, storage, authoring and sync unit (the GDD's "chunk").
- World data layers: deterministic base generator → stamped structure templates → authored
  edits → persistent runtime edits; regions store only differences. Procedural generation is
  an authoring tool for the handcrafted map, not an endless runtime world.
- Collision: architecture from voxel queries; imported models from components / `colliders.json` until
  D6/D7; Lastlight's `CollisionWorld` composes both (Jolt later).
- Conservative LOD for structural chunks; crack-free sealing (existing) extended to shapes.
- Texture standards become configurable per game (V2); Lastlight: 64×64 standard, 32×32 small
  details, 128×128 characters/weapons only, ≥ 256 prohibited without an approved exception;
  64 texels/m on voxel surfaces. Engine defaults stay as today for registries without a
  standard (examples, fixtures).
- Performance work (packed voxel vertices, GPU-driven chunks, model/prop instancing) only after
  benchmarks (V6).
- Phase order: V0 → V1–V5 → D3–D6 → D7 (voxel bunker) → V6 → D8.

### Phase V1 implementation notes (2026-11-01)
- `Scene/Voxel/BlockRegistry.h`: `VoxelPalette` (moved from `ChunkMesher.h`) is the block
  registry; `colors`/`faceLayers` are the material table. Empty `blocks` = legacy mode (cube,
  material = id), so existing palettes need no changes. `BlockTable` is the immutable per-id
  lookup (built once per `VoxelWorld`, shared read-only by mesh jobs).
- Shapes are data (`ShapeTemplate` polygons + boxes + `aoOccluder`); coverage, masks, normals
  and fill are derived and validated at registration; rotation = 4 yaw steps (flip reserved).
- Mesher: per-cell flags (full faces, AO occluder, cube) in the padded grid; greedy cube path
  unchanged (key = material + 1); separate unmerged shape path at LOD 0; shapes render as cubes
  at LOD > 0 until V3. Cube-only output byte-identical (golden hash tests, engine + Highlands).
- `VoxelWorld` opacity shortcuts (solid depth, trivial meshes, bedrock) count full cubes only.

## World production (approved in principle, 2026-11-01; planning only)

`docs/WORLD_PRODUCTION_ROADMAP.md`: Engine Complete, Area Production Complete (with a separate
Single-Player Complete milestone; final sign-off requires tested co-op) and Game Complete;
one area at a time, bunker first; Phase V / D order unchanged; rendering S5–S8 after D8;
generated assets final only after owner approval; proposals are not requirements.

## Phase D1 decisions (implemented)

- glTF importer messages have two levels: `ModelData::warnings` (skipped features that change
  how the model looks: texture transforms, a texture on `TEXCOORD_1+`, specular-glossiness,
  non-triangle primitives) and `ModelData::notes` (ignored data that leaves the static model
  intact: animations, skins, morph targets, cameras, lights, extra UV/colour sets, unread
  `KHR_materials_*`). One note per feature, naming its users. Asset checks copy notes into
  `AssetCheckResult::notes` and never count them as warnings, so typical exports pass
  `check --strict`.
- Required extensions are checked right after parsing, before buffers are loaded; unsupported
  ones fail with the export setting that avoids them (Draco, WebP, mesh quantization, meshopt,
  BasisU; any other unknown required extension is a generic error).
- `ModelNode::path` keeps the authored hierarchy (`root/group/cube`) next to the flattened
  world matrices; the mesh-instance list itself is unchanged (renderer and checks untouched).
- `SourceTool` (`blockbench|blender|other`) comes only from `asset.generator`; it is reported
  and never changes import behaviour.
- Registry schema stays v1 with optional fields: `defaults` (per type, applied to new entries
  only; entries always store explicit settings, so defaults never change how existing assets
  load), `budgets` (class -> max triangles) and model `class` (must name a budget). Registries
  without them serialize byte-identically.
- Bounds check: largest model extent outside 1/16..64 m is a warning (wrong export scale);
  limits in `AssetCheckOptions`.
- Batch import (`findAssetFiles`): sorted paths; skips hidden entries, `*.zodar` caches,
  palette swatches and files another found asset depends on.
- Fixtures are generated by stdlib Python with stored (uncompressed) deflate blocks, so the
  bytes do not depend on the zlib version; ctest `tools.gltf_fixtures_up_to_date` guards them
  and the committed fixture registry is checked strictly.

## Phase D2 decisions (implemented)

- Game assets live in `game/assets` (registry, palette + `glow` sub-palette, models,
  materials, prefabs, levels, `sources/`); layout and workflow in `game/assets/README.md`.
  The game binary finds them through `LASTLIGHT_ASSETS_DIR` (CMake, source tree) or `--assets`.
- `LastlightExpanse --level bunker|highlands` (default `bunker`). The highlands code path is
  untouched (`LastlightApp` in `main.cpp`); the bunker is a separate `Application`
  (`game/src/Bunker/BunkerApp.cpp`) selected by the factory, so highlands stays byte-identical.
- Bunker loading: AssetManager async pre-load of the level's models and materials
  (`loadSceneModelsAsync`, `createSceneMaterialsAsync`; `--deterministic` waits), then a
  `LiveScene` (registry ids, code mesh `sign`, file watching) instantiates the level from the
  cache. Rendering through `collectDraws` (CPU path) + `collectLighting` every frame (lights,
  ambient and fog follow scene reloads). Hot reload: `AssetManagerDesc::watchFiles` and
  `LiveSceneDesc::watch`, both active only in Debug builds.
- Greybox content is generated (`game/tools/make_greybox_kit.py`, deterministic); real exports
  replace modules under the same file names with their sources in `sources/`.
- Source files in regular Git: `tools.game_source_sizes` limits `.blend` only (20 MiB) and
  rejects `.blend1`/`.blend2`; `.gitattributes` marks `game/assets/**` `-text`, `*.blend`
  `binary`; `.gitignore` drops Blender backups and `*.zodar/` caches.
- Registry defaults `compile: false` until compiled caches are produced in the build instead
  of the source tree.
- Exploration mode (user request during D2, temporary): `CollisionWorld`
  (`game/src/Physics`: static AABBs; `sweep` per axis with skin, `raycast`, `addStatic`,
  `clear`) is the approved D7 interface, implemented early; D7 keeps it, Phase E swaps in
  Jolt behind it. `ExplorationWalker` (`game/src/Player`) is throwaway: the D7 player
  controller replaces it and its direct SDL keyboard polling (D3 input actions). Collision
  boxes per module come from `game/assets/colliders.json` (generated from the module
  geometry; model bounds as a fallback) until scene files carry Collider components (D6).
  Colliders are rebuilt when the level hot-reloads. `LastlightGameCore` (static library) holds
  the testable game logic; `LastlightGameTests` (ctest `game.unit`) walks the real level
  through every room.

## Frame flow (Milestone 1)

```
beginFrame()
  resize pending? -> waitIdle, recreate swapchain
  wait fence[slot] -> acquire(imageAvailable[slot]) -> reset fence, reset pool
  begin cmd, barrier UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
caller records: vkCmdBeginRendering ... draw ... vkCmdEndRendering
endFrame()
  [screenshot? copy image -> host buffer]
  barrier -> PRESENT_SRC, end cmd
  submit2(wait imageAvailable[slot] @ colour output, signal renderFinished[image], fence[slot])
  present(wait renderFinished[image]); OUT_OF_DATE/SUBOPTIMAL -> rebuild next frame
```

In Milestone 1 the sample records raw Vulkan commands between `beginFrame` and `endFrame`.
Milestone 2 introduces render passes/graph, meshes and materials so games no longer touch
Vulkan directly.

## Rendering: capabilities, not styles

ZODAR does not ship visual styles or "profiles". The engine provides general-purpose
rendering capabilities; each game composes them into its own look.

| Engine provides (style-neutral) | Game decides |
| --- | --- |
| Materials as data: shader + parameters + textures, user-authored shaders | Which shaders and materials exist, their look |
| Built-in PBR metallic-roughness shading model *as one option*, plus custom/unlit models | Whether to use PBR at all |
| Lights (directional, point, spot), shadow maps with configurable resolution/filtering | Light setup, shadow quality per scene |
| Render resolution scale or fixed internal resolution + selectable upscale filter (linear/nearest) | e.g. 640×360 nearest for a pixel look |
| Sampler control per texture/material (filtering, anisotropy, mip bias) | Point-sampled vs filtered textures |
| Vertex-stage material hooks (custom vertex functions) | Vertex snapping, wobble, deformation |
| Post-process stack of ordered, data-driven passes (built-ins: tone mapping, bloom, SSAO, fog, colour LUT) + custom passes | Which passes, their order and parameters (dither, quantize, CRT, ...) |
| `GraphicsSettings` (resolution, scale, shadow/AO/bloom quality, vsync, anisotropy) | Defaults and user-facing presets ("Low/High", "Authentic PSX") |

Rule: if a feature only makes sense for one art direction, it lives in the game. If several
very different games could use it, it is an engine capability with neutral defaults.

DEAD SECTOR's PSX-inspired look is therefore a **game-side style preset**
(`Games/DeadSector/Rendering/`): its own shaders (vertex snapping, affine UV option,
dither/quantize post pass), material conventions and settings presets, built only from
public engine capabilities.

## Shader interface

Engine shader library in `Shaders/Include/zodar/` (included as `#include "zodar/x.glsl"`):
`bindless.glsl`, `scene.glsl` (material table + buffer-reference frame/instance data), `vertex.glsl` (standard
inputs + `zodarTransformVertex()`), `surface.glsl` (base colour, alpha mask, normal mapping),
`lighting.glsl` (GGX PBR), `fullscreen.glsl`. A material shader is any vertex/fragment pair
registered with `MaterialSystem::registerShader`; games implement their own looks this way.

## Conventions

- Language: **C++23** (GCC 13, Clang 18, MinGW-w64 GCC 13 verified; MSVC 17.8+ unverified). Use
  only C++23 features available on all of them; BUILDING.md ("C++23 policy") lists what is
  usable and what is not yet.

- Namespaces: `zodar` (core/platform), `zodar::gfx` (renderer). Types `PascalCase`, functions
  `camelCase`, members `m_name`, constants `kName`, macros `ZODAR_`.
- Headers are included as `"Module/File.h"` relative to `Engine/`.
- Third-party code is fetched and compiled as SYSTEM; engine code is built with
  `-Wall -Wextra -Wpedantic -Wshadow` (or `/W4`), optionally `-Werror`.
- Shaders: GLSL → SPIR-V at build time (`zodar_compile_shaders`), output to `bin/shaders/`.
  Engine-wide shaders will live in top-level `Shaders/`; sample-only shaders stay with the sample.

## Known architectural debts (tracked)

- Phase C audit (2026-10-31): `FileWatcher` detects size + mtime only (same-size edits within
  coarse mtime resolution, e.g. FAT 2 s, are missed); a `gfx::Model` constructor that throws
  part-way (material table full) leaks the materials it created; async material requests are
  not cancelled when every user handle is dropped (textures still decode); `LiveScene` watches
  scene + prefab files only (material/texture/model files through the AssetManager's
  `watchFiles`; a missing texture of a failed scene load is not watched); the asset report
  contains the absolute registry path; registry source containment is lexical (symlinks
  inside the directory are followed).

- Textures must outlive materials referencing them (material pipeline state is immutable;
  factors and textures change through `updateMaterial`).
- `BindlessRegistry::releaseTexture` assumes the GPU no longer uses the slot (callers defer).
- Uploads (S3): `UploadQueue` is single-threaded (main thread); no streaming yet (design:
  `ASSET_STREAMING.md`); staging buffers are allocated per batch (streaming staging ring
  planned); the ZodarAssetCompiler up-to-date check still decodes the source images.
- S3 verification gaps (see `S3_REPORT.md`): the per-frame `acquireCompleted` path with
  a real cross-family ownership transfer has no hardware evidence yet (only the blocking path
  was validated on an RTX 4070); KTX2 output was not checked with Khronos `ktx validate`.
- KTX2: no supercompression (Zstandard/Basis), 2D only; BC5 normal maps need z
  reconstruction in the shader, so model normal maps use BC7 for now.
- Render graph v1: no transient aliasing, no reordering, single queue (async compute: S9).
- S4: cluster lists have a fixed capacity per preset (overflow drops lights; counted and shown);
  IBL bake is synchronous; depth prepass has no normal/roughness/velocity outputs yet; no tone
  mapping (S6), so bright HDR clips in the resolve.
- GpuScene (S2): frustum culling only (occlusion/HZB: S9); blended instances are CPU-sorted and
  drawn after all opaque geometry; GPU-scene and `draws` blended objects are not interleaved;
  geometry pool has no per-mesh free. Draw order within a group is submission order, not
  front-to-back (same as the CPU path; depth prepass: S4).
- Dear ImGui's Vulkan backend uploads its font texture with its own queue submit + wait.
- A pipeline is created for a fixed swapchain format; if a recreated swapchain changes format,
  pipelines must be rebuilt (not handled yet; formats do not change in practice).
- Swapchain recreation uses `vkDeviceWaitIdle`; `VK_EXT_swapchain_maintenance1` present fences
  would remove the stall and the theoretical semaphore-reuse gap on recreate.
- One window per process (`Platform` owns SDL init).
