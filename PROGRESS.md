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
- `main`: **Phase C complete** (C1-C7 + audit, commit `54eae81`; Debug + Release 24/24 ctest,
  199 unit, 64 GPU cases, 0 warnings, 0 validation messages).
- **Phase D plan approved (rev 2, 2026-11-01), not started:** `docs/PHASE_D_PLAN.md`
  (8 sessions D1-D8, read lists, tests, acceptance criteria); roadmap updated.

- **GDD added (2026-11-01):** `docs/GDD/Lastlight_Expanse_GDD_v1.1.md` is the authoritative
  game specification (CLAUDE.md, `docs/PHASE_D_PLAN.md`). GDD alignment approved
  (2026-11-01, `docs/PHASE_D_PLAN.md`): D2 greybox adds five merchant areas, locker room and
  common area (static only); D6 adds rotating backups + configurable autosave; no networking in
  Phase D, host-authoritative compatible, player ids and player logic game-side only; D7 adds
  repairable electrical panels + emergency lighting; palette muted/faded post-war (GDD §3).

- **Phase D1 done (2026-11-01):** unified GLB/glTF pipeline for Blockbench and Blender
  (engine, generic), verified with synthetic fixtures only; real-export validation pending
  (user supplies one export per tool). See the D1 handoff below.

- **Phase D2 done (2026-11-01):** game asset layout + static greybox bunker (default level),
  revised to a modern spacious shelter, plus a temporary first-person exploration mode
  (`zodar.py explore`); `--level highlands` regression scene byte-identical. See the D2
  handoffs below.

- **Phase V0 done (2026-11-01):** hybrid voxel + model architecture approved and documented;
  GDD Amendment 1. See the V0 handoff below.

- **Integration (2026-11-01):** D1, D2, V0 (+ roadmap docs) fast-forwarded into `main`
  (`2090cbd..ad2283d`, no history rewrite). V1 developed on `claude/v1-voxel-foundation`
  (not merged into `main`).

- **Phase V1 done (2026-11-01):** block registry, shape library, mesher shapes (engine).
  See the V1 handoff below.

- **Phase V2 done (2026-11-01):** block definition files, block catalogue, configurable
  texture standards, `blocks` asset type, engine voxel shader `zodar/voxel` (engine only, no
  `game/` changes). Branch `claude/v2-block-format`, not merged. See the V2 handoff below.

## Current task
- **V1 merged:** `main` fast-forwarded `ad2283d..620ad40` (2026-11-01, no history rewrite)
  after the final V1 review (no critical issues). Minor review findings are recorded as
  follow-ups under V2 and V3 in `docs/PHASE_D_PLAN.md`.
- **V2 done** on `claude/v2-block-format` (from `main` `620ad40`); awaits review and merge.
- **Next session: V3 — voxel collision, raycasts, conservative LOD (engine)**; waits for the
  user's go (and the V2 review/merge). Order: V3–V5 → D3–D6 → D7 → V6 → D8.
- World Production Roadmap (`docs/WORLD_PRODUCTION_ROADMAP.md`) approved in principle
  (2026-11-01), planning only; its proposals (areas, bunker boundary, optional NPCs, story
  items, post-D8 track order) await the user's review — see its "Unresolved decisions".

## Handoff (Phase V2 session, 2026-11-01)
- Branch `claude/v2-block-format` (from `main` `620ad40` + docs `7226731`), one commit per step:
  1. V1 follow-ups: star-shaped polygon check, face-overlap rejection (sample-based), UV tie
     rule + √2 slope stretch documented and tested, `<utility>`, `Support/MeshHash.h`;
  2. registry-mode unknown ids → `VoxelPalette::kFallbackMaterial` (legacy unchanged);
  3. `TextureStandard` (Assets) + verdicts (Violation / Prohibited ≥ 256);
  4. registry `textureStandards` + `textureClass` (+ defaults, `--texture-class`), density
     target from the registry, runtime opt-in `AssetManagerDesc::textureStandard`;
  5. `SceneFile/BlockFile` (`zodar.blocks` v1) → `BlockCatalogue`, `checkBlockLayerSizes`;
  6. asset type `blocks` (deps, sizes, palette, texture-array dims, density, report keys);
  7. `zodar/voxel` (`voxel_pbr.frag`), `gfx::Vec4Table` (RGBA32F), `AssetManager/BlockMaterials`;
  8. docs + verification.
- Decisions (user, 2026-11-01): separate `textureClass`; class violations = warnings
  (`--strict` errors), ≥ 256 always errors (engine default included); Lastlight declares its
  standard in V5 (`tools.game_assets_check --strict` stays green); `"rotations": 4` →
  `name`, `name@1..@3`, ids in file order (append-only until V4); custom JSON shapes deferred.
- Implementation decisions: block files use arrays with `name` (order explicit, duplicates
  detectable); material index = file order; layers deduplicated by (texture, roughness,
  metallic, emissive); defaults roughness 0.8, metallic 0, emissive 0, tint white,
  `uvPerMetre` 1; engine shader output named `voxel_pbr.frag.spv` (the game already ships
  `voxel.frag.spv`); `SceneFile` now links `Zodar::Scene`; checks resolve shapes with the
  built-in library unless `AssetCheckOptions::blockShapes` is set.
- Float upload investigation: `GpuImage` + `UploadBatch::uploadImage` already supported
  `R32G32B32A32_SFLOAT` (16 B/texel); `Texture` only wraps `PixelFormat` (RGBA8/BCn/RGBA16F).
  `Vec4Table` creates the image directly, checks sampled-image support and size limits. No
  precision reduction: RGBA8 (0.3 → 0.302, emissive > 1 impossible) and RGBA16F (0.3 →
  0.2998) are shown insufficient in a test; the table reads back bit-exact.
- **V2 verification** (lavapipe Mesa 25.2.8, validation on): GCC 13 Debug + Release 36/36
  ctest, 0 warnings, 0 validation messages after every step (gate script: build both, ctest
  both, Highlands screenshot); Clang 18 Release 36/36 ctest, engine/catalogue/Highlands golden
  hashes green, Highlands screenshot identical (Clang build: 3 pre-existing
  `-Wunused-result` warnings in `engine/tests/Assets/PaletteTests.cpp`, untouched by V2).
  Highlands screenshot (640×360, 90 frames) sha256 `61ab33fa…` in Debug, Release and Clang,
  identical to the pre-V2 baseline in this container. `ZodarAssetTool check --strict` reports
  of `examples/asset_loading` and `game/assets` byte-identical before/after step 4.
  New tests: 15 engine unit cases (shape validation ×2, sloped UVs, unknown-id fallback,
  texture standards, registry standards ×3, blocks assets ×2, block file ×5), 5 GPU cases (runtime standard, parameter precision, emissive vs
  unlit, lit + fog vs pbr, layer-size errors). New golden hash: catalogue mesh
  `0x2444B9C0D80D2BD6` (GCC + Clang).
- Known issues (not V2): `gpu.subsystems` failed 2 of ~25 full `ctest -j4` runs in
  `ResourceTests.cpp:156` ("deletion queue": `completedValue() == lastSubmitted()` 417 vs 418
  right after `UploadContext::immediate`); not reproducible in 10 sequential runs under load.
  Seen at step 3 too (no GPU code changed), so pre-existing: likely the timeline value lags
  the fence on lavapipe under load. Not fixed (unrelated system). Recorded Highlands sha256
  `1e6b461a…` came from another container's Mesa; in this container (Mesa 25.2.8, LLVM 20)
  the pre-V2 baseline (`7226731`) is `61ab33fa…` and stays identical after every V2 step.
- Open: MinGW and MSVC golden-hash verification; the game's Highlands test still has its own
  FNV helper (moving it to `Support/MeshHash.h` needs a `game/` change, deferred).
- **V3 read list:** `CLAUDE.md`, `PROGRESS.md`, `docs/guide/VOXELS.md` §3–6,
  `docs/PHASE_D_PLAN.md` (V3 + follow-ups), `engine/scene/Scene/Voxel/*.h`,
  `engine/scene/Scene/Voxel/VoxelWorld.cpp`, `engine/tests/Scene/VoxelShapeTests.cpp`.

## Handoff (Phase V1 session, 2026-11-01)
- Branch `claude/v1-voxel-foundation` (from `main` `ad2283d`), one commit per step:
  1. golden hash tests of the legacy cube mesher, captured before any change (engine:
     integer-hashed terrain, all LODs, AO, voxelSize 0.5, sealing, layers, streamed world;
     game: Highlands seed 1337 chunks + palette/layers; `LastlightGameTests` compiles the
     generator sources, the game executable is unchanged) + skipped benchmark case
     `voxel bench: cube-only meshChunk`;
  2. `Scene/Voxel/BlockRegistry.h/.cpp`, `Shapes.cpp`: `VoxelPalette` (moved from
     `ChunkMesher.h`) + `BlockState`, `ShapeTemplate`/`Shape`/`ShapeLibrary` (9 built-ins + cube),
     `rotateShape`, `BlockTable`;
  3. mesher on `BlockTable` (full-face culling, `aoOccluder`, material merge key, fill-weighted
     LOD with a cube-only fast path) — hashes unchanged;
  4. shape path (LOD 0), `MeshOptions::uvPerMetre`, `MeshStats::shapePolygons`;
  5. `VoxelWorld`: one `BlockTable`, opacity shortcuts count full cubes only, bedrock = first
     cube id, `blocks()` accessor;
  6. docs (VOXELS, ARCHITECTURE, PHASE_D_PLAN, roadmap, CLAUDE.md, this file).
- Decisions (user, 2026-11-01): `voxelSize` kept (no `cellSize` rename; docs fixed); stair
  risers 0.25 m (half a cell); 4 yaw rotations, flip reserved; explicit `aoOccluder` per
  shape; shapes render as cubes at LOD > 0 (until V3); ≤ 5 % cube-only meshing regression;
  CPU geometry/hash tests, screenshot comparison stays a separate visual check; doc conflicts
  fixed without changing approved requirements (VOXELS §2/§3/§9, PHASE_D_PLAN V1,
  AI_FIRST_ROADMAP §4 and texture-standard notes, stale "Next steps").
- Performance (bench workload, callgrind instruction counts; wall time on this shared VM
  varied ±50 %): meshChunk 4.036e9 → 4.183e9 after step 3 (+3.7 %); final +0.8 % (see verification).
- Verification (GCC 13, lavapipe, validation on): Debug + Release 36/36 ctest, 231 engine unit
  cases (+1 skipped benchmark), 7 game unit cases, 65 GPU cases, 0 warnings, 0 validation
  messages; Highlands screenshot sha256 `1e6b461a…` unchanged (Debug + Release); golden hashes
  green in Debug and Release. Cube-only meshing, bench workload (callgrind): 4.036e9 → 4.069e9
  instructions (+0.8 %, limit 5 %). Not verified: Clang, MinGW, MSVC.
- Not done (by plan): `*.blocks.json`, materials, texture standards, voxel shader (V2);
  shape-aware collision/raycasts, conservative LOD, `structural` flag (V3); regions and
  structures (V4); bunker migration (V5). Golden hashes assume x86-64 without FMA contraction
  (GCC/Clang/MinGW defaults); MSVC unverified.
- **V2 read list:** `CLAUDE.md`, `PROGRESS.md`, `docs/guide/VOXELS.md` §3/§9,
  `docs/PHASE_D_PLAN.md` (V2), `docs/guide/ASSETS.md`, `Scene/Voxel/BlockRegistry.h`,
  `Assets/TextureStandards.*`, `AssetRegistry.*`.

## Handoff (Phase V0 session, 2026-11-01)
- Done (documentation only, no code): GDD Amendment 1 (§2 region = 64 × 64 m, handcrafted
  world with procedural generation as an authoring tool; §3 hybrid geometry, 0.5 m grid, Full
  Block / Half Block (Slab) / Vertical Half Block / Sub-block, extensible shapes, imported
  imported 3D models for weapons, characters, mutants, furniture, generators, machinery,
  computers, vehicles, equipment and decorative props; final texture standard 32/64/128,
  ≥ 256 prohibited, 8×8/16×16 not approved); new
  `docs/guide/VOXELS.md` (binding spec); ART_DIRECTION §1–5; ARCHITECTURE "Phase V
  decisions"; PHASE_D_PLAN (Phase V sessions, dependencies, effect on D7/D8); roadmap;
  MODELING; ASSETS; CLAUDE.md; `game/assets/README.md` (pending changes).
- Decisions (user, 2026-11-01): see GDD A1 and VOXELS.md; bunker on voxels: 4 m corridor,
  4.0 m ceiling, 0.5 m walls; Highlands stays the 1 m regression scene.
- Implemented state unchanged: engine texture checks still enforce 32 default / 64 maximum
  (configurable per game in V2); game textures are 32×32 (re-made at 64×64 in V5).
- V0 review corrections (user, 2026-11-01): 8×8/16×16 not approved (invalid for new assets);
  model usage wording unified (voxel blocks: architecture and structural environment;
  imported models: weapons, characters, mutants, furniture, generators, machinery, computers,
  vehicles, equipment and decorative props).
- **V1 read list:** `CLAUDE.md`, `PROGRESS.md`, `docs/guide/VOXELS.md` §2–5,
  `docs/PHASE_D_PLAN.md` (Phase V, V1), `engine/scene/Scene/Voxel/*.h`,
  `engine/scene/Scene/Voxel/ChunkMesher.cpp`, `engine/tests/Scene/VoxelTests.cpp`.
- **V1 first step:** add the cube-only mesher hash regression test on the current code (must
  pass unchanged), then the block registry and shape path.

## Handoff (D2 revision: modern shelter + exploration mode, 2026-11-01)
- User requests during D2 (after commit 4663edd): (1) redesign the bunker as a spacious,
  modern, maintained military shelter; (2) a temporary first-person exploration mode.
- Done:
  - Generator rewritten for a 4 m grid: corridor 60×4 m, 3.6 m ceilings, 0.3 m walls with
    0.5 m pilasters, beams, suspended cable tray, vents, neutral LED overhead lights, warm wall
    lights, lit exit signs, room and merchant signs, sliding doors (2.0 m), wide openings
    (2.8 m); rooms enlarged (dormitory 12×8, locker room 8×8, common area 24×12, generator
    16×8, five merchant zones 12×8, airlock 8×12, exit passage with stairs). 28 modules
    (27 Blockbench-style + Blender-style arch), 10 signs, 4 prefabs; clean GDD §3 palette with
    a neutral light colour added to the glow sub-palette. Registry: 43 entries, strict OK.
  - Exploration: `--explore` / `zodar.py explore` (window, Windows + Linux): spawn at the
    entrance, WASD, mouse look (captured; Tab frees), Shift sprint, Space jump, collision,
    stair step-up, smoothed eye height. `game/src/Physics/CollisionWorld` (D7 interface,
    kept), temporary `game/src/Player/ExplorationWalker`, `game/src/Bunker/LevelColliders`
    (+ generated `game/assets/colliders.json`), `LastlightGameCore` library,
    `LastlightGameTests`.
- Tests: Debug and Release 0 warnings, 36/36 ctest each (new `game.unit`: 6 cases incl. a
  walk from the entrance through every room; `gpu.lastlight_bunker_explore_smoke`), 212 unit
  + 65 GPU cases, validation 0 errors / 0 warnings (GPU tests, bunker, bunker --explore,
  highlands); highlands screenshot sha256 `1e6b461a…` unchanged (Debug, Release).
- Not verified: interactive play on a real window and GPU (container is headless; mouse
  capture and feel need the user's check); no real exports; Clang/MinGW/MSVC not built.
- 64×64 texture question still open (kept 32×32 per GDD §3).

## Handoff (Phase D2 session, 2026-11-01)
- Done:
  - Source-file policy (regular Git, no LFS): `.gitattributes` `game/assets/** -text`,
    `*.blend binary`; `.gitignore` `*.blend1`, `*.blend2`, `game/assets/**/*.zodar/`;
    `game/tools/check_source_sizes.py` (20 MiB limit on `.blend` only, rejects
    `.blend1`/`.blend2`, no limit on other types) + self-test.
  - `game/assets/` layout per plan: registry (palette + `glow` sub-palette, defaults, budgets,
    29 entries: 21 models, 5 materials, 2 prefabs, 1 scene), `sources/blockbench`,
    `sources/blender(/textures)` (READMEs only; no real sources yet), `game/assets/README.md`.
  - `game/tools/make_greybox_kit.py` (deterministic): 20 Blockbench-style modules (floor, wall,
    doorway, corner, ceiling, pillar, stairs, door leaf, lamp, crate, bunk, locker, terminal,
    generator, electrical panel, emergency light, merchant counter, shelf, table, bench) + the
    Blender-style curved `airlock_arch`; 13 32×32 textures from the master palette; 5 merchant
    signs; prefabs `ceiling_lamp`, `merchant_stall`; `levels/bunker.scene.json`.
  - Level (GDD §10, static): corridor, dormitory, locker room (private lockers as props),
    common area, generator room, five merchant areas (weapon dealer, medic, resource trader,
    technician, scout), airlock with the arch, exit passage with stairs. Room point lights
    (range <= room), emissive lamps and screens, emergency lights as unlit props (D7), dim fog,
    player start camera. No NPC, merchant, trading, locker or power functionality.
  - Game: `--level bunker|highlands` (default bunker), `--assets DIR`;
    `game/src/Bunker/BunkerApp.cpp` (AssetManager async pre-load + LiveScene, hot reload in
    Debug, fly camera); highlands code untouched.
  - Docs: `game/assets/README.md`, MODELING.md (layout, policy test), RUNNING.md, CLAUDE.md,
    ARCHITECTURE "Phase D2 decisions", plan/roadmap status.
- Tests (GCC 13, warnings as errors): Debug and Release 0 warnings, 34/34 ctest each (new:
  `tools.game_source_sizes`, `tools.game_source_sizes_selftest`, `tools.greybox_kit_up_to_date`,
  `tools.game_assets_check` (strict, 29 ok), `gpu.lastlight_bunker_smoke`), 212 unit + 65 GPU
  cases, validation 0 errors / 0 warnings (GPU tests, bunker, highlands). Highlands screenshot
  (640×360, 90 frames) sha256 `1e6b461a…` identical before/after D2 in Debug and Release.
  Bunker screenshot identical Debug vs Release.
- Manual check (Debug, game running with a copy of the assets): edited sign material, `.glb`
  (truncated -> "previous version kept", restored -> reloaded), level scene and lamp prefab
  -> live reloads; broken scene JSON kept the previous level.
- Decisions: see `docs/PHASE_D_PLAN.md` D2 scope notes (compile false, explicit material and
  prefab imports, generated level, async pre-load + LiveScene).
- Open debts: no real Blockbench/Blender exports or sources yet (generated "-style" modules
  only, no tool compatibility claimed); no ctest golden for highlands (manual comparison);
  emergency lights unlit until D7; no collision (D7); bunker FPS on real GPUs not measured.
  Not run: Clang, MinGW, MSVC.
- **D3 read list:** `CLAUDE.md`, `PROGRESS.md`, `docs/PHASE_D_PLAN.md` (D3, GDD alignment item
  3), `engine/runtime/Runtime/Application.h`, `engine/runtime/Runtime/CommandLine.h`,
  `game/src/main.cpp` and `game/src/Bunker/BunkerApp.cpp` (current SDL input).
- **D3 first step:** per plan (`Zodar::Input` actions/bindings), engine-only, no player concept.

## Handoff (Phase D1 session, 2026-11-01)
- Done:
  - Fixtures: `engine/tests/data/gltf_tools/make_fixtures.py` (stdlib, deterministic, stored
    deflate) writes `valid/blockbench_crate.glb`, `valid/blockbench_crate_embedded.gltf`
    (data: URIs), `valid/blender_wall.glb`, `rejected/{draco,webp,quantization}.glb`;
    `assets.json` there (palette, defaults, budgets, classes) made with `zodar.py import valid`.
  - Importer (`GltfLoader.cpp`, `ModelData.h`): `notes` vs `warnings` (one note per ignored
    feature naming its users), `generator` + `SourceTool` (`detectSourceTool`, `toString`),
    `ModelNode::path` (hierarchy), required extensions checked before buffers with actionable
    errors (Draco, WebP, quantization, meshopt, BasisU), optional Draco uses the fallback.
  - Registry (`AssetRegistry.h/.cpp`): optional `defaults` (texture/model, new entries only),
    `budgets` + model `class` (validated), bounds check (1/16..64 m), check result/report
    fields `sourceTool`, `notes`, `size`, `class`, `triangleBudget`; `findAssetFiles` for
    batch import. Schema v1 unchanged; old registries serialize identically.
  - Tool: `ZodarAssetTool import DIR` / `--class`; `zodar.py import FILE|DIR`.
  - Docs: new `docs/guide/MODELING.md`; `docs/guide/ASSETS.md` (defaults, budgets, batch,
    notes, report fields); `docs/ART_DIRECTION.md` §3-5 (Blender secondary, one standard);
    ARCHITECTURE "Phase D1 decisions"; plan/roadmap/CLAUDE.md status.
- Tests: new `engine/tests/Assets/GltfToolFixtureTests.cpp` (13 cases: source tool, both
  Blockbench variants, Blender, rejected messages, optional real exports, defaults/budgets
  round-trip + errors, defaults on import, report fields + budgets, bounds, batch import,
  committed registry); GPU case "gltf tools: ... render byte-exact with pixel-art textures"
  (GPU mip 0 == source texels, compiled RGBA8 KTX2 renders identically to source, renders
  deterministic); ctests `tools.asset_check_gltf_tools` (strict), `tools.asset_import_dir*`,
  `tools.gltf_fixtures_up_to_date`.
- Results: GCC 13 Debug (warnings as errors) 0 warnings, 29/29 ctest, 212 unit and 65 GPU
  cases, validation 0 errors / 0 warnings.
  GCC 13 Release (warnings as errors): 0 warnings, 29/29 ctest, 212 unit + 65 GPU cases,
  validation 0 messages. `make_fixtures.py --check` reproduces the committed bytes.
- Decisions: notes do not fail `--strict` (needed for the acceptance "valid fixtures pass
  --strict" with animations / Blender material extensions); fixture registry uses
  `compile: false` (no KTX2 caches committed; the GPU case covers compile via defaults).
- Verified vs not verified: everything above is verified against synthetic fixtures modelled
  on each exporter's structure only. Real Blockbench and Blender compatibility is NOT
  verified; it requires the user's real exports in `engine/tests/data/gltf_tools/real/`.
- Open debts: real Blockbench/Blender exports not provided yet (drop them into
  `engine/tests/data/gltf_tools/real/` as `blockbench_*.glb` / `blender_*.glb`; the unit case
  checks them automatically, then import them into the fixture registry). Fixtures only
  imitate exporter output (Blender menu names in MODELING.md are from Blender 4.x docs, not
  verified in this session). Not run: Clang, MinGW, MSVC.
- **D2 read list:** `CLAUDE.md`, `PROGRESS.md`, `docs/PHASE_D_PLAN.md` (D2, GDD alignment),
  `docs/guide/MODELING.md`, `docs/guide/SCENE_FILES.md`, GDD §1, §3, §9, §10,
  `game/src/main.cpp`, `game/CMakeLists.txt`.
- **D2 first step:** step 0 source-file policy (`.gitattributes` `-text` + `*.blend binary`,
  `.gitignore` for `.blend1`/`.blend2` and `*.zodar/`, ctest `tools.game_source_sizes`: 20 MB
  limit on `.blend` only, rejects `.blend1`/`.blend2`). No Git LFS.

## Handoff (Phase D planning session, 2026-11-01)
- Done: Phase D planned and approved; `docs/PHASE_D_PLAN.md` written; roadmap Phase D
  rewritten (8 sessions); ARCHITECTURE "Phase D decisions"; CLAUDE.md links the plan.
  Documentation only: no code changed, no build or tests run (none needed).
- Approved decisions: (1) script-generated greybox kit first, replaced later by real exports;
  the user supplies one real Blockbench `.glb` and one real Blender `.glb` for D1 validation;
  (2) temporary game-side AABB collision behind `CollisionWorld` (Jolt in Phase E);
  (3) `bunker` default level, `highlands` kept as regression scene; (4) Phase D = 8 sessions;
  (5) Git LFS for `.blend` after a support + quota check. *Superseded 2026-11-01:* no Git
  LFS; `.blend` in regular Git under the D2 source-file policy (`docs/PHASE_D_PLAN.md`).
- Workflow: Caveman for chat only; docs/code/commits in normal prose; one session per D-step;
  handoff block here at the end of each session.
- Open: real exports not yet provided (D1 can finish with generated fixtures; real-export test
  added on arrival).
- **D1 read list:** `CLAUDE.md`, `PROGRESS.md`, `docs/PHASE_D_PLAN.md` (D1 + asset workflow;
  GDD alignment section), GDD §3 (art direction),
  `docs/guide/ASSETS.md`, `docs/ART_DIRECTION.md` §3-5,
  `engine/assets/Assets/GltfLoader.cpp`,
  `engine/rendering/AssetManager/AssetManager/AssetRegistry.h`.
- **D1 first step:** write `engine/tests/data/gltf_tools/make_fixtures.py` (stdlib only,
  deterministic) generating the Blockbench-style and Blender-style fixtures plus the three
  rejected-extension cases, then add failing unit cases that load each fixture and assert the
  expected meshes, textures, samplers, hierarchy/bounds and notes, before changing
  `GltfLoader.cpp`.

## Phase C audit (C1-C7, done)
- Two independent reviews (runtime/lifetime; registry/pipeline/palette) + doc pass. No Vulkan
  use-after-free, material-slot double release or Texture move bug found.
- Fixed (runtime): async create/upload exceptions failed every later update() (now reject the
  asset); model material slots leaked when a model upload/reload threw; watcher baseline taken
  at first poll instead of at read time (AssetManager + LiveScene; `FileWatcher::refresh`);
  changes during async loads were lost (now deferred and retried in `update()`); `#` in
  material file names treated as inline material; model dependency listing hashed all bytes
  and was not path-normalised (`assetDependencies` + `resolve`); `updateMaterialData` could
  partially commit on a full material table; source bookkeeping grew without bound;
  `reload(empty handle)` threw.
- Fixed (registry/tools): `--density-target abc` crashed ZodarAssetTool; empty `--palette`
  silently disabled checks; report write unchecked; texel density read out-of-range indices of
  malformed glTF; scene palette check skipped colour textures first seen as normal maps;
  absolute/`..` registry sources accepted; `..name.png` rejected; `--id` reuse copied another
  file's settings / allowed duplicate sources; non-atomic `assets.json` save; malformed `%XX`
  URIs threw or decoded wrongly; fixed-size message buffers truncated; corrupt KTX2 cache
  failed the whole model (now falls back to the source image).
- Docs: stale texture-standard (128/256), texel-density (64) and mesh-binding notes; palette
  alpha rule wording (alpha > 0); new debts listed in ARCHITECTURE.md.
- Tests added: registry hardening, scene palette dedupe + long names, file watcher refresh,
  deferred async reload / `#` material / empty handles, read-time baseline,
  `tools.asset_tool_bad_arguments`. Verified GCC 13 Debug and Release: 0 warnings, 24/24 ctest,
  199/199 unit, 64/64 GPU cases, 0 validation messages. Not run: Clang, MinGW, MSVC.

## Phase C6 — shader + scene hot reload (done)
- `MaterialSystem::reloadShader(handle, DeletionQueue&)` -> `ShaderReloadResult`: SPIR-V
  checks (header, entry-point stage, instruction bounds), every cached colour/depth pipeline
  of the shader rebuilt first, then swapped into the existing objects
  (`GraphicsPipeline::swap`), old pipelines/modules retired; failure changes nothing.
  `shaderCount/shaderName/vertexFile/fragmentFile`; pipeline build code factored
  (`buildPipeline`, `buildDepthPipeline`), keys unchanged.
- AssetManager: watches material shader SPIR-V files, `reloadFiles` reloads them
  (`"shader:<name>"`), `reloadShader(name)`.
- `LiveScene` (`AssetManager/LiveScene.h`): load/reload/update (own watcher over scene +
  prefab files, opt-in, Debug only), atomic reload (old entities destroyed only after
  success; changed inline materials via `updateMaterialData`, same gfx handles).
  `loadSceneFile(path, &files)` (optional, default keeps old signature behaviour).
- Tests: unit `scene file: loadSceneFile lists ...`; GPU `hot reload: material shaders ...`
  (pbr <-> unlit SPIR-V swap, garbage, wrong stage, truncated, missing, unknown) and
  `hot reload: live scenes ...` (transform + inline material, 4 failure kinds, prefab via
  watching, broken prefab, destructor). Verified GCC 13 Debug, 0 warnings, 196/196 unit,
  63/63 GPU cases, 23/23 ctest, 0 validation messages; hot-reload GPU cases 5x stable.
  GCC 13 Release (watching compiled out): 0 warnings, 23/23 ctest, hot-reload cases pass
  (watching case skips). Not run: Clang, MinGW, MSVC.

## Phase C5 — asset hot reload (done)
- `AssetCache::replace` (in-place swap of the asset struct, previous content returned,
  failed -> ready) + `AssetHandle::generation()`.
- `AssetManager::reload(texture|model|material)`, `reloadFiles(paths)` (textures, then file
  materials incl. quiet retry of failed ones, then models via glTF dependencies),
  `updateMaterialData` (atomic, data materials), `watchedFiles`, `pollFileChanges`,
  `AssetReloadResult`, stats `reloads`/`reloadFailures`. Texture reload refreshes dependent
  materials via `updateMaterial`; old GPU objects retired on the deletion queue; pipeline-state
  changes rejected; failures keep the previous version and are logged.
- Opt-in watching: `AssetManagerDesc::watchFiles` + `watchInterval`, polled in `update()`;
  compiled only into Debug builds (`ZODAR_HOT_RELOAD` CMake option, `kHotReloadAvailable`);
  `Core/FileWatcher` (size + mtime polling).
- Tests: unit `asset cache: replace ...`, `file watcher:*` (2); GPU `hot reload:*` (4:
  textures + material files incl. failures/recovery, atomic data materials, glTF models with
  texture dependencies, opt-in watching). Verified GCC 13 Debug, 0 warnings, 195/195 unit,
  61/61 GPU cases, 23/23 ctest, 0 validation messages.

## Phase C7 — palette validation (done)
- `AssetRegistry`: optional `"palette"` (swatch PNG | `#rrggbb` list | `{colors, subPalettes}`),
  entry `"palette": "<sub>"` (sub-palettes extend the master), validated parse, round-trip;
  nothing written or checked without a palette. API: `PaletteSource`, `PaletteConfig`,
  `Palette`, `loadPalette`, `parseHexColor`, `hexColor`, `parsePaletteArgument`,
  `AssetCheckOptions::palette`, `AssetCheckResult::paletteDeviation`, texture palette counts,
  `AssetImportOptions::palette`.
- Checks: non-transparent (alpha > 0) level-0 texels of colour textures only (texture usage color, material base
  colour/emissive, model base colour/emissive images); transparent texels ignored; warning with
  count, percent and example colours; report `paletteDeviation` + per-texture counts.
- Tool: `check --palette swatch.png|#rrggbb,...` (overrides master), palette summary line,
  `--strict` fails on deviations; `import --sub-palette NAME`.
- Example: `examples/asset_loading/palette.png` (master, 6 colours from make_assets.py) +
  sub-palette `glow` (`#5adcff`) for the rune assets: 0 off-palette texels. ctests
  `tools.asset_check_example` (strict, now palette-checked) and
  `tools.asset_check_palette_strict` (override palette must fail --strict).
- Tests: 4 `palette:*` unit cases (colours/swatches/argument; registry forms + validation +
  round-trip + unchanged files without palette; counting incl. transparency, sub-palettes,
  normal maps skipped, material slots, report keys, override, broken palette; example
  registry clean). Verified GCC 13 Debug, 0 warnings, 192/192 unit, 57/57 GPU cases, 23/23
  ctest, 0 validation messages. Not run: Release, Clang, MinGW, MSVC.

## Asset pipeline fixes + Phase C4 (done)
- Texture standards: 32×32 default, 64×64 absolute maximum for all assets, 8/16 allowed
  (`TextureSizeClass` Small/Default/Maximum/Oversized/NonStandard); warnings only, never
  resized; texel density target 32 texels/m. Example crate texture regenerated at 32×32.
  Reported violation: game procedural terrain textures 10 × 256² + 1 × 512²
  (`game/src/Rendering/BlockTextures.h`), not changed (game art work).
- Pixel-art compression audit: model compile used BC7 for every texture and the loader used any
  matching cache. Now pixel-art textures compile to lossless RGBA8 KTX2 (`optionsFor(usage,
  pixelArt)`, `ModelCompileOptions::pixelArt`, `ZodarAssetCompiler --pixel-art`,
  `ZodarAssetTool --compile` from registry sampling); `compileTexture` refuses BCn for pixel
  art; `loadGltf` rejects BCn caches of pixel-art textures (`lossyRejected`); compiler rewrites
  them; `kTextureCompilerVersion` 2. BC7 kept for non-pixel-art.
- C4: scene `modelRenderer {model, visible}` (registry id, or `.gltf`/`.glb` path relative to
  the file; prefab paths rebased; parse checks paths exist, renderer exclusivity);
  `ModelRenderer` component (`collectDraws`; CPU path only); `SceneBindings::models`,
  `SceneCaptureNames::models`; `AssetManager::loadSceneModels[Async]` (+ `SceneModelAssets`);
  registry hashes path-referenced models and checks ids; `Model::textures()` accessor.
  Example scene: crate by id + small crate by path; sample spawns them from the scene.
- Tests: texture standards (rewritten), `pixel art:*` (2 unit: byte-exact compile, lossy cache
  rejection/recompile), GPU `asset manager: pixel-art textures reach the GPU byte-exact`
  (readback of source and compiled KTX2), `model references:*` (4 unit), GPU `scene file:
  modelRenderer ...`, registry oversized/density updates. Verified GCC 13 Debug, 0 warnings,
  188/188 unit, 57/57 GPU cases, 22/22 ctest, 0 validation messages.
  Not run: Release, Clang, MinGW, MSVC.

## Phase C3 — asset registry and CLI (done)
- `AssetManager/AssetRegistry.h`: `assets.json` (`zodar.asset_registry` v1: id, type, source,
  import settings, dependencies, FNV-1a hash, compiled outputs), parse with DataError
  (line:column + JSON path), deterministic serialize, `importAsset`, `fingerprintAsset`
  (dependency discovery: glTF/GLB URIs, material textures, scene materials, prefabs
  recursively), `checkAsset`/`checkRegistry` + `assetReportJson` (`zodar.asset_report` v1:
  size classes, triangle/vertex counts, texel density, stale/missing), `textureLoadSettings`
  / `modelLoadSettings` for loading by id.
- Tool `ZodarAssetTool import|list|check` (`--compile` bakes KTX2 via `compileModelTextures`);
  `zodar.py import FILE...` and `zodar.py assets list|check`.
- `sceneMaterialDirectory` (SceneFile): inline prefab materials now resolve their textures
  next to the prefab (fixes AssetManager scene materials from prefabs too).
- Example registry `examples/asset_loading/assets.json` (5 assets, all ok); sample loads the
  crate by id; ctest `tools.asset_check_example` (`check --strict`).
- Tests: 5 `asset registry:*` unit cases (types/settings, parse validation + round-trip,
  import/dependencies/hash/stale/missing/size warnings/report, density + prefab dirs, example
  registry current). Verified GCC 13 Debug, 0 warnings, 182/182 unit, 55/55 GPU cases, 22/22
  ctest, 0 validation messages; `--compile` path checked manually on a temp copy.
  Not run: Release, Clang, MinGW, MSVC.

## Phase C2 — async asset loading (done)
- `AssetCache`: `Loading` state, `getOrCreatePending`, `resolve`, `reject`; handles
  `loading()`, `state()`; `get()` throws while loading.
- `AssetManager`: `loadTextureAsync`, `loadModelAsync`, `loadMaterialAsync`,
  `createMaterialAsync`, `createSceneMaterialsAsync` (+ `SceneMaterialAssets::loading/errors`),
  `update()` (per frame), `waitAll()`, `pendingCount()`, stats `pending`/`uploading`;
  `AssetManagerDesc::jobs` (decode workers), `uploadBudgetBytes` (default 32 MiB per update).
  Decode on JobSystem -> budgeted `submitAsync` uploads -> ready after the frame loop's acquire.
  Blocking loads share decode/create code and wait for pending async loads of the same key.
  Cancellation by dropping handles. Blocking API unchanged.
- Sample `ZodarAssetLoading` now loads asynchronously (`--deterministic` waits in onStart);
  new ctest `gpu.asset_loading_async` (real frame loop, no wait: scene ready at frame 2,
  fails if anything is still loading at exit).
- Tests: unit `asset cache: pending entries ...`; GPU `asset manager: async*` (3: workers +
  waitAll + rendering, upload budget + blocking wait + cancel while decoding/uploading,
  async failures incl. scene errors); async GPU cases run 30x without failure. Verified GCC 13
  Debug, 0 warnings, 177/177 unit, 55/55 GPU cases, 21/21 ctest, 0 validation messages.
  Not run: ThreadSanitizer, Release, Clang, MinGW, MSVC.

## Phase C1 — asset manager (done)
- Module `Zodar::AssetManager` (`engine/rendering/AssetManager`): `loadTexture` (PNG/JPEG/TGA/BMP,
  usage + mips), `loadModel` (glTF 2.0 .gltf/.glb, optional sampler override), `loadMaterial`
  (`*.material.json` with textures), `createMaterial`, `createSceneMaterials`; cache by
  resolved path + settings; failures as handle state + message (no throw); stats.
- `Assets/AssetCache.h` (`AssetCache<T>`, `AssetHandle<T>`: shared ownership, retire on last
  release, failed records cached while held); `Assets/TextureStandards.h` (32/64/128/256
  classes, warnings for other sizes).
- Lifetime: release through the device deletion queue; `MaterialSystem::releaseMaterial` + free
  list (slot reuse); `Model::fallbackMaterial()`; models release their materials.
- Material files: texture slots (`baseColorTexture` ... `emissiveTexture`, string or
  `{path, filter, wrap}`), `samplerFor`; serialization unchanged for untextured materials.
- `gfx::SamplerDesc::pixelArt()` (nearest mag/min, linear mips, no anisotropy); glTF sampler
  filters verified end to end.
- Example `examples/asset_loading` (glTF crate 64², rune 32² material, scene with pixel-art and
  linear materials) + sample `ZodarAssetLoading` (ctest `gpu.asset_loading_example`).
- Docs: `docs/guide/ASSETS.md`; SCENE_FILES (texture slots), ARCHITECTURE (module + decisions),
  ART_DIRECTION (new size standards 64/32/128/256, texel density 1/64 m), roadmap.
- Tests: unit `asset cache:*` (3), `texture standards:*`, `material textures:*` (4), updated
  material validation case; GPU `asset manager:*` (4: pixel-art vs linear rendering, caching +
  shared textures + deferred release + slot reuse, failures/warnings, example glTF + scene
  materials). Verified GCC 13 Debug, 0 warnings, 176/176 unit, 52/52 GPU cases, 20/20 ctest,
  0 validation messages. Not run: Release, Clang, MinGW, MSVC.
- Behaviour change: `*Texture` keys in material files are now parsed (previously errors).

## Phase B2 (done)
- **Phase B2 core done**: module `Zodar::SceneFile` (`engine/rendering/SceneFile`, nlohmann/json
  v3.11.3 private, hash-pinned tarball). `*.material.json` (`MaterialData`, parse/serialize,
  `toMaterialDesc`, `createMaterial`), `*.scene.json` (`SceneDocument`: materials inline or by
  path, entities with id/parent/name/transform/meshRenderer), `createSceneMaterials`,
  `instantiateScene` (validated first, World unchanged on error). `DataError` with all issues
  (file:line:column for syntax, JSON pointer for schema). `Transform::operator==` added.
  Docs `docs/guide/SCENE_FILES.md`, example `examples/scene_from_json`.
- Tests: 7 `scene file:*` unit cases (`engine/tests/SceneFile/SceneFileTests.cpp`: defaults,
  validation messages, syntax line/column, example load, exact round-trip, transform forms,
  hierarchy/reference errors, instantiate rollback) + GPU case (JSON scene pixel-identical to
  C++ scene, CPU + GpuScene; unknown shader / bad submesh). Verified GCC 13 Debug, 0 warnings,
  162/162 unit, 47/47 GPU cases, 19/19 ctest, 0 validation messages. Not run: Release, Clang,
  MinGW, MSVC.
- **Phase B2 remainder done**: schema-error line:column (`JsonLocator`); reflection
  (`World/Reflection.h`, generated `docs/guide/SCENE_COMPONENTS.md`); scene components
  `CameraComponent`, `DirectionalLight/PointLight/SpotLightComponent`, `EnvironmentComponent`,
  `FogComponent` + `collectLighting`/`collectCamera` (`WorldRender/SceneComponents.h`); prefabs
  (`*.prefab.json`, overrides, nesting, cycle check); `captureScene`/`invertBindings`
  (`World::entities()`); mutable materials (`MaterialSystem::updateMaterial`, deferred
  `vkCmdUpdateBuffer`, `zodar::updateMaterial` for MaterialData). Example scene extended (lamp
  prefab, sky, camera).
- Tests: 13 `scene file:*` unit cases; GPU: JSON scene (lights, prefab) pixel-identical to C++,
  capture/reload identical, mutable material case. Verified GCC 13 Debug, 0 warnings, 168/168
  unit, 48/48 GPU cases, 19/19 ctest, 0 validation messages (core + synchronization). Not run:
  Release, Clang, MinGW, MSVC.
- Deliberately not done: prefab references preserved on save (flat documents), IBL map /
  textures in files and hot reload (Phase C), game migration to scene files.

## Next steps
1. Phase V and D per `docs/PHASE_D_PLAN.md`: V3 next, then V4–V5 → D3–D6 → D7 → V6 → D8.
   Streaming budgets, FBX/audio import and skins stay in later phases. Highlands keeps its
   legacy 256²/512² terrain textures (regression scene, not migrated; VOXELS §12).
2. Optional tooling follow-ups: image diff tool / golden-image tests (roadmap audit), MSVC build.
3. S5 shadows when scheduled.

## Backlog (rendering / game visuals, interleave when needed)
- S5 shadows (CSM sun) in pbr.frag and voxel.frag.
- Weather (rain/snow), day-night cycle.
- Compact voxel vertex (8–16 B; meshes are 64 B/vertex, 108 MB for 0.8 M triangles at radius 6).
- GeometryPool free-list -> GPU-driven terrain.
- BC7 block textures via AssetPipeline.
- Real-GPU benchmarks (RTX 4070); MSVC build; 1.4 validation with layers >= 1.4.304.
