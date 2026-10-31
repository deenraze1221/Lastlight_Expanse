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

- Phase A1: `Zodar::Runtime` (`engine/runtime/Runtime`): `zodar::Application` (owns Platform,
  Window, Renderer, UploadContext, SceneRenderer, RenderGraphResources, DebugOverlay, CpuProfiler,
  JobSystem; hooks onStart/onEvent/onFixedUpdate/onUpdate/onRender/onShutdown; exit codes
  0/1/2/3), `FixedTimestep` (accumulator, max steps, clamp, interpolation alpha), headless mode
  (SDL offscreen driver via hint, no env var) and deterministic mode (one fixed step per frame).
  Game migrated (`LastlightApp`; new `--headless`, `--deterministic`); ModelViewer/ZodarBench
  unchanged (A3). Tests: 5 `FixedTimestep*` unit cases, `gpu.runtime_headless`
  (`ZodarRuntimeSmoke`: driver, hook order, step/frame counts, frame time, screenshot pixel),
  `gpu.lastlight_smoke` now `--headless --deterministic`. Verified: GCC 13 Debug + Release and
  Clang 18 Debug, 0 warnings, 17/17 ctest each (128 unit cases), validation 0 errors; game
  screenshot byte-identical to pre-migration (640x360, 90 frames); lavapipe game FPS (Release,
  no validation, interleaved medians) 7.10 -> 7.27: within noise. Not verified this session:
  MinGW/Windows, MSVC.

- Phase A2: standard command line for `Application` apps (`Runtime/CommandLine`:
  `parseStandardOptions`, `runMain`, `--help`; --frames --headless --deterministic --fixed-dt
  --width/--height --screenshot --capture-frames --capture-dir --report --log-file --seed --camera
  --threads --no-validation --overlay); PNG screenshots (`Core/ImageIO` `writePng`/`writeImage`,
  stb_image_write static); capture sequences (one readback written to several paths:
  `Renderer::requestScreenshot(std::vector<path>)`); run report JSON (`Runtime/RunReport`,
  schema `zodar.run_report` v1: FPS p50/p95/p99, frame/CPU/GPU summaries, CPU scopes, GPU passes,
  render stats, memory last/peak, validation + log counts, captures); log warning/error counters
  (`Core/Log`). Game on `runMain` (seed default 1337, `--camera` applied in onStart; numeric
  options now strict: `--seed -1` or `10abc` are rejected). Exit 3 when a screenshot or capture
  is missing, 1 when the report cannot be written. Tests: CommandLine (4), RunReport (3), PNG and
  log-count unit cases; `gpu.runtime_capture_report` (PNG screenshot, captures incl. last frame,
  report content); `gpu.lastlight_smoke` writes PNG + report. Verified: GCC 13 Debug + Release,
  Clang 18 Debug, 0 warnings, 18/18 ctest each (137 unit cases), validation 0 errors; game
  screenshot byte-identical to pre-A1; lavapipe game FPS (Release, no validation, interleaved
  medians, pre-A1 vs A2) 7.50 vs 7.47: within noise. Not verified: MinGW/Windows, MSVC.
- Art direction documented: `docs/ART_DIRECTION.md` (voxel/pixel-art style, 256/512/1024 texture
  standards, palette/texel density, sampling + mips, Blockbench → glTF workflow, engine/game asset
  split, future technical requirements). Documentation only; no rendering or pipeline change.

- Phase A3 (Phase A complete): `CMakePresets.json` (debug, release, gpu-tests, ci + `ci`
  workflow; trees in `build/<preset>`); `tools/zodar.py` (build|test|run|shot|bench|report,
  stdlib Python, failures/summaries only, logs to files, timeouts exit 124);
  `docs/guide/RUNNING.md`; ModelViewer and ZodarBench migrated to `Application` (bench keeps
  its CLI: `--frames` = measured frames, mid-path `--screenshot`); `Application`: `onFrameEnd`,
  `requestCapture` (verified, exit 3), `RenderContext::frame` (reference paths), standard CPU
  scopes (`Wait + acquire`, `Build graph`, `Record`, `Submit + present`), sync-validation and
  transfer-queue desc fields, memory query only for overlay/report; JSON-lines log sink
  (`--log-json`, UTF-8 validated); `--sync-validation` standard. Tests: JSON log unit case,
  CLI cases extended, runtime smoke checks `onFrameEnd` + `requestCapture`,
  `gpu.zodar_shot_game` (exit criterion: `zodar.py shot game --frames 120` -> PNG + report).
  Verified: `ci` preset (GCC 13 Debug, warnings as errors) + GCC 13 Release + Clang 18 Debug,
  0 warnings, 19/19 ctest each (138 unit cases), validation 0 errors. Screenshots vs pre-A3:
  game byte-identical; ModelViewer identical with `--no-overlay` and `--reference-path` (overlay
  text varies run to run); ZodarBench grid/city/unique/reference screenshots and per-frame stats
  identical. lavapipe Release bench city (interleaved medians of 3): CPU 22.15 -> 22.12 ms,
  GPU 22.12 -> 22.10 ms: no regression. Behaviour changes: bench `cpuFrameMs` includes events +
  update, scope names standardised; ModelViewer animation on the runtime clock; bench rejects
  unknown `--present` and non-finite `--frame-cap`. Not verified: MinGW/Windows, MSVC.

- Phase B1.1: `Zodar::World` module (`engine/world/World`): EnTT v3.15.0 dependency
  (`Zodar::EnTT`, header-only); `zodar::Entity` handle (32-bit id, null = 0xFFFFFFFF);
  `zodar::World` (create, destroy -> bool, isAlive, entityCount, clear; movable, non-copyable).
  Tests: 7 `World*` unit cases (null handle, distinct ids, destroy/double destroy, stale handle
  after index reuse, clear, foreign world, move). Verified: GCC 13 Debug, warnings as errors,
  0 warnings, full build, 7/7 ctest (non-GPU), 145/145 unit cases. Not run this session: GPU
  tests (no renderer change), Release, Clang, MinGW, MSVC.

- Phase B1.2: `World` component API (`add/get/tryGet/has/remove<T>`, EnTT hidden), `Name` and
  `Transform` (`World/Components.h`, T*R*S local matrix), parent/child hierarchy (`setParent`,
  `parent`, `children`, `isDescendantOf`; cycles/self/dead rejected; `destroy` removes the
  subtree), on-demand `localMatrix`/`worldMatrix`. Tests: 7 new cases in
  `engine/tests/World/WorldComponentTests.cpp`. Verified: GCC 13 Debug, warnings as errors, full
  build 0 warnings, 152/152 unit cases, 7/7 ctest (non-GPU). Not run: GPU tests (no renderer
  change), Release, Clang, MinGW, MSVC.

## Engine audit: open items (from SECTOR-ENGINE docs)
- Renderer S4 awaiting RTX benchmarks; S5 shadows, S6 post (tonemap, TAA), S7 AO/GI, S8 volumetrics/fog, S9 HZB/meshlets planned.
- Asset streaming designed (docs/ASSET_STREAMING.md), not implemented. MSVC/Windows build unverified here.
- GeometryPool cannot free meshes; streamed terrain uses the CPU draw path.

## Completed (docs)
- `docs/AI_FIRST_ROADMAP.md`: AI-first audit + phases A–G (runtime/tooling, ECS + scene files,
  asset manager, gameplay, physics/audio/animation, introspection, agent docs).

## Current state
- `main`: Phase B1.3 committed and pushed (World -> renderer bridge, `Zodar::WorldRender`).
  Phases A, B1.1, B1.2 unchanged.

## Current task
- **Phase B1.3 done**: `World::each`; module `Zodar::WorldRender` (`MeshRenderer`,
  `collectDraws`, `WorldRenderSync` + `GpuSceneSink`). Tests: `world render:*` in ZodarTests
  (fake sink: add/update/re-create/remove/recycled ids) and ZodarGpuTests (pixel-identical to
  hand-built draws, CPU + GpuScene, after update/destroy). Full ctest 19/19, 0 validation msgs.
- Not done (deliberate): game/ModelViewer still use hand-built draws (no migration); cached
  world matrices/dirty flags (recomputed per sync); scene JSON (B2).
- **Next: Phase B2** (scene/material JSON) when requested.

## Next steps
1. Phase B2 scene/material JSON (optionally migrate game draws to World entities).
2. Optional tooling follow-ups: image diff tool / golden-image tests (roadmap audit), MSVC build.
3. Art-direction technical work (docs/ART_DIRECTION.md §4) when scheduled: glTF/Blockbench
   import checks, pixel-art texture rules, asset validation report; S5 shadows.

## Backlog (rendering / game visuals, interleave when needed)
- S5 shadows (CSM sun) in pbr.frag and voxel.frag.
- Weather (rain/snow), day-night cycle.
- Compact voxel vertex (8–16 B; meshes are 64 B/vertex, 108 MB for 0.8 M triangles at radius 6).
- GeometryPool free-list -> GPU-driven terrain.
- BC7 block textures via AssetPipeline.
- Real-GPU benchmarks (RTX 4070); MSVC build; 1.4 validation with layers >= 1.4.304.
