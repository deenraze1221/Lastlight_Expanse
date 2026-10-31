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
  199 unit, 64 GPU cases, 0 warnings, 0 validation messages). Phase D not started.

## Current task
- **Phase D planning (not implementation).** Plan Phase D around building the Lastlight
  Expanse **bunker first** (first playable space), including a **unified Blockbench asset
  pipeline**: Blockbench -> glTF export -> `zodar.py import` (registry, texture standards,
  palette, lossless pixel art) -> scene/prefab files -> `LiveScene`/hot reload in the game.
  Inputs: `docs/ART_DIRECTION.md` (§3 Blockbench workflow, §4 requirements),
  `docs/AI_FIRST_ROADMAP.md` (Phase D: input, system scheduling, save/load),
  `docs/guide/ASSETS.md`, `docs/guide/SCENE_FILES.md`. Output: a written Phase D plan
  (bunker scope, pipeline steps, engine vs game split, sessions, tests) for approval.

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
1. Write the Phase D plan (see Current task): bunker-first, unified Blockbench pipeline;
   implement only after approval. Streaming budgets, FBX/audio import and skins stay in later
   phases. Game: re-author terrain textures to 32×32 (currently 256²/512², violate 64 max).
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
