# Progress

## Completed
- Imported ZODAR engine (SECTOR-ENGINE) with full history, plus unmerged `ui/stage2` (Zodar::UI).
- Reorganised into `engine/{core,graphics,rendering,assets,scene,tools,ui,samples,tests}` + `game/`.
  No engine source changes needed: include paths preserved via per-module include roots.
- Split `Zodar::Graphics` (Vulkan layer) out of `Zodar::Renderer`.
- `Core/JobSystem`; `Zodar::Scene`: noise, voxel chunks, greedy AO mesher, LOD + sealing, streaming, edits, raycast.
- `Renderer/StreamedMeshSet`: async chunk uploads with deferred deletion.
- `game/`: LastlightExpanse executable (dusk highlands, caves, fly camera, dig/place) + `gpu.lastlight_smoke` test.
- Verified: GCC 13.3, 0 warnings; 15/15 ctest (119 unit cases, GPU tests on lavapipe, validation on, 0 errors).

- Visual style increment 1: engine `TextureArray` (+ GPU test), `zodarSampleArray`, engine fog
  (`SceneLighting::fog`, `zodar/fog.glsl`, pbr.frag); mesher face layers; game pixel-art textures
  (10 × 256², 1 × 512² hero, ~0.85 s generation), `lastlight/voxel` shader, dusk fog;
  `--untextured`/`--no-fog` A/B switches. lavapipe A/B: 9.9 vs 9.6 FPS (geometry-bound software
  rasterizer; real GPU numbers still needed).

- C++23 + optional Vulkan 1.4: `CMAKE_CXX_STANDARD 23` with compiler minimums (GCC 13, Clang 18,
  MSVC 19.38; override option); `ApiVersion.h` policy (1.3 minimum, 1.4 requested, limited by
  an older validation layer; effective = min(instance, device)) used by VMA and ImGui;
  `ResolvedFeatures::vulkan14` detection only, no 1.4 functionality used. Verified: GCC 13
  Debug/Release and Clang 18 Debug 16/16 ctest (123 unit, 45 GPU cases, validation 0 errors),
  MinGW-w64 GCC 13 Windows build 0 warnings + 123/123 unit tests under Wine, 1.4 path
  (`gpu.zodarbench_highest_api_smoke`, game) on lavapipe 1.4.318. Benchmarks vs baseline
  (lavapipe, interleaved medians): grid 316.9 -> 311.3 ms, unique 91.7 -> 91.1 ms, game
  9.8 -> 9.7 FPS: within noise (±5%), no regression. Not verified: MSVC; 1.4 path under
  validation (needs layers >= 1.4.304; Ubuntu 24.04 ships 1.3.275).

## Engine audit: open items (from SECTOR-ENGINE docs)
- Renderer S4 awaiting RTX benchmarks; S5 shadows, S6 post (tonemap, TAA), S7 AO/GI, S8 volumetrics/fog, S9 HZB/meshlets planned.
- Asset streaming designed (docs/ASSET_STREAMING.md), not implemented. MSVC/Windows build unverified here.
- GeometryPool cannot free meshes; streamed terrain uses the CPU draw path.

## Completed (docs)
- `docs/AI_FIRST_ROADMAP.md`: AI-first audit + phases A–G (runtime/tooling, ECS + scene files,
  asset manager, gameplay, physics/audio/animation, introspection, agent docs).

## Current state (commit f64a190)
- Branch `claude/peaceful-thompson-v8a01e`, clean tree, pushed.
- All checks green: GCC 13 Debug/Release + Clang 18 Debug 16/16 ctest, MinGW Windows build +
  123/123 unit tests (Wine), validation 0 errors, no performance regression.

## Current task
- **Next: Phase A1** (docs/AI_FIRST_ROADMAP.md §2 Phase A, §4 item 1): `zodar::Application`
  runtime (window or headless, fixed timestep, standard frame loop) in a new `Zodar::Runtime`
  module; migrate `game/` onto it, keeping existing samples/tools unchanged until A3.
  Not started.

## Next steps
1. Phase A1 (above).
2. Phase A2: standard CLI, PNG capture, run report JSON.
3. Phase A3: CMakePresets + `tools/zodar.py`, RUNNING guide; migrate ModelViewer/ZodarBench.
4. Phase B1/B2: EnTT world + transforms + renderer bridge; scene/material JSON.

## Backlog (rendering / game visuals, interleave when needed)
- S5 shadows (CSM sun) in pbr.frag and voxel.frag.
- Weather (rain/snow), day-night cycle.
- Compact voxel vertex (8–16 B; meshes are 64 B/vertex, 108 MB for 0.8 M triangles at radius 6).
- GeometryPool free-list -> GPU-driven terrain.
- BC7 block textures via AssetPipeline.
- Real-GPU benchmarks (RTX 4070); MSVC build; 1.4 validation with layers >= 1.4.304.
