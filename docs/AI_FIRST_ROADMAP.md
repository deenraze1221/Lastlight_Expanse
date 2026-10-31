# ZODAR — AI-First Engine Roadmap

**Status:** audit done; C++23 / optional Vulkan 1.4 done (f64a190); **Phase A1 done**
(`zodar::Application`, `FixedTimestep`, headless/deterministic modes, game migrated).
**Phase A2 done** (standard CLI + `runMain`, PNG screenshots, capture sequences, `--camera`,
run report JSON). **Phase A3 done** (CMake presets, `tools/zodar.py`, `docs/guide/RUNNING.md`,
ModelViewer + ZodarBench on `Application`, JSON-lines log sink): **Phase A complete**.
**Phase B1.2 done** (`Zodar::World` components, `Name`/`Transform`, hierarchy). **Next: Phase B1.3** (renderer bridge).

Goal: ZODAR is driven by Claude Code (code, CLI, files, logs, screenshots), not by a visual
editor. Every engine capability must be reachable through documented C++ APIs, text data
files and command-line tools whose output an agent can read. No visual editor and no
standalone SDK until this roadmap's phases are done.

Principles
1. **Text in, text out.** Scenes, materials, lights, input maps and asset manifests are
   human-readable files (JSON) that an agent can write, diff and review. Tools print concise
   summaries and write machine-readable reports (JSON) and images (PNG).
2. **Everything headless and deterministic on demand.** Fixed timestep, inline jobs, seeded
   RNG, offscreen rendering, scripted input: same input -> same frames -> testable.
3. **One obvious way.** One application runtime, one entity model, one asset handle type.
   Fewer choices = fewer agent mistakes.
4. **Preserve working systems.** New layers wrap the existing renderer, scene and asset code;
   nothing working is rewritten. Old entry points stay until callers migrate.
5. **Every phase ships docs + examples + tests** an agent can copy.

## 1. Audit (current state)

| Area | Exists | Missing for AI-first use |
| --- | --- | --- |
| Application runtime | `Platform`, `Window`, `Renderer` frame loop | No `Application` class: ModelViewer, ZodarBench and the game each copy ~150 lines of setup + loop (render graph, overlay, profiler, screenshot, validation exit codes). No fixed timestep, no headless app mode |
| Scene / entities | `SceneView` + `MeshDraw` lists, `GpuScene` instances, `Camera`, voxel `VoxelWorld` | No entity/component model, no transform hierarchy, no scene file format, no prefabs, no queries |
| Materials | `MaterialSystem` (data-driven, custom shaders, `custom0/1`), `TextureArray` | Created only in C++; immutable after creation; no material files; no shader hot reload |
| Lighting | Sun, point, spot, clustered Forward+, IBL bake, fog | No light components / files; no shadows (S5); no post-processing / tonemapping (S6) |
| Assets | glTF import, KTX2/BCn, compiled texture cache, `ZodarAssetCompiler`, streaming *design* | No asset manager (handles, ref counts, async load, unload), no asset registry/manifest, no hot reload, no FBX (ufbx planned), no audio assets |
| Physics | — | Everything (Jolt planned); voxel collision |
| Audio | — | Everything (miniaudio planned) |
| Animation | — | Skinning, clips, blending; glTF skins/animations are not imported |
| Gameplay | Game-specific code in `game/` | Input action mapping, events, timers, gameplay system scheduling, save/load |
| Build / run / test | CMake + Ninja, doctest unit + GPU tests, ctest, lavapipe offscreen runs | No CMake presets, no single `zodar` CLI; output of builds/tests is long; no golden-image tests |
| Logging | spdlog, optional log file | No structured (JSON lines) log, no per-run log directory |
| Screenshots | PPM via `Renderer::requestScreenshot`, `--screenshot` per executable | PPM only (agents read PNG), no capture sequences, no camera/frame selection, no image diff tool |
| Profiling | CPU scopes, GPU timestamps, ImGui overlay, ZodarBench JSON | No run report for normal apps; overlay is visual-only |
| Introspection | ImGui overlay | No way to query a running app (entities, stats) or to command it (set component, screenshot) |
| Docs | ARCHITECTURE, ROADMAP, BUILDING, stage reports, UI spec, CLAUDE.md, PROGRESS.md | No per-module API reference, no recipes ("how do I add a light"), no example per feature |

Strong foundations to build on: modular CMake libraries with downward-only dependencies,
validated Vulkan 1.3 renderer with GPU tests, `JobSystem`, `StreamedMeshSet`, deterministic
voxel world, bindless materials, headless GPU test context.

## 2. Phased roadmap

Sizes are in focused work sessions (one session ≈ one reviewed, tested, pushed increment).
Phases run in order; rendering stages (S5+) continue in parallel when a phase needs them.

### Phase A — Runtime + agent tooling (3–4 sessions) — *first*
Make every app start, run, capture and report the same way.
- `Zodar::Runtime` module: `zodar::Application` owning Platform, Window (or headless
  offscreen), Renderer, UploadContext, SceneRenderer, RenderGraphResources, DebugOverlay,
  JobSystem; virtual hooks `onStart/onFixedUpdate/onUpdate/onRender/onShutdown`; fixed timestep
  with interpolation; validation-error exit codes. Game, ModelViewer, ZodarBench migrate one at
  a time (old code kept until each migration passes its tests).
- Standard command line for every app: `--frames N --fixed-dt --deterministic --headless
  --width/--height --screenshot path.png --capture-frames 10,60,120 --capture-dir
  --log-file --report run.json --seed`.
- PNG screenshots (stb_image_write is already pinned); capture sequences; `--camera` override.
- Run report JSON at exit: frames, fps p50/p95/p99, CPU scopes, GPU passes, memory, draw
  stats, validation counts, warnings/errors, screenshot paths.
- `CMakePresets.json` (debug, release, gpu-tests, ci) and `tools/zodar.py`:
  `zodar build|test|run|shot|bench|report` with short summaries (failures only, last N log
  lines) so agents spend few tokens.
- Structured JSON-lines log sink (opt-in) next to the human log.
- Exit criteria: game + 2 samples on `Application`; `zodar shot game --frames 120` yields a
  PNG + report.json headless on lavapipe; docs `docs/guide/RUNNING.md`.

### Phase B — Entities, scenes and data files (4–5 sessions)
- ECS: **EnTT** (pinned, header-only, MIT) behind a thin `zodar::World`/`Entity` API so the
  backend stays replaceable. Core components: `Name`, `Transform` (+ hierarchy, cached world
  matrix), `MeshRenderer` (mesh + material), `Camera`, `DirectionalLight`, `PointLight`,
  `SpotLight`, `Environment`, `Fog`, `VoxelTerrain`.
- Render bridge system: components -> `SceneView` / `GpuScene` (existing renderer unchanged).
- Scene files (`*.scene.json`, nlohmann/json pinned): entities, components, material and
  light definitions, asset references by path; load/save round-trip; prefabs (`*.prefab.json`)
  with overrides. Schema documented and validated with clear error messages (file:line, field).
- Material files (`*.material.json`) and mutable materials (deferred GPU update).
- Component reflection (static field tables) so serialization, the console (Phase F) and docs
  share one description.
- Exit criteria: a scene authored only as JSON renders identically to the equivalent C++ (GPU
  test); `examples/scene_from_json`.

### Phase C — Asset manager (3–4 sessions)
- `AssetManager` with typed handles (`Handle<Mesh>`, `Handle<Texture>`, `Handle<Material>`,
  `Handle<AudioClip>`...), ref counting, async load on `JobSystem` + `UploadQueue`, unload via
  deletion queue, failure states with messages.
- Asset registry (`assets.json`): ids, source paths, import settings, compiled outputs, hashes
  (extends the existing texture cache). `zodar import <file>` / `zodar assets list|check`.
- Implement `docs/ASSET_STREAMING.md` (priorities, budgets, residency).
- Importers: glTF (existing, plus skins/animations), FBX via ufbx -> glTF-equivalent data,
  textures (existing), audio (WAV/OGG/MP3 via miniaudio decoders).
- Hot reload of textures, materials, shaders and scene files (file watcher, debug builds).

### Phase D — Gameplay foundation (3 sessions)
- Input: action maps (`input.json`), keyboard/mouse/gamepad (SDL3), recorded input files that
  replay deterministically (agents test gameplay without a human).
- System scheduling (ordered phases: input, fixed simulation, animation, late update, render),
  events/messages, timers, deterministic RNG service.
- Save/load built on scene serialization.
- Gameplay stays C++ (agents write C++ well, and it is fastest); a scripting language is
  deferred until a real need appears.

### Phase E — Physics, audio, animation (6–8 sessions)
- **Physics (Jolt):** `Zodar::Physics` — rigid bodies, colliders, character controller,
  ray/shape casts, triggers, voxel chunk colliders from `VoxelWorld`, fixed-step determinism,
  debug draw; components + scene-file support.
- **Audio (miniaudio):** `Zodar::Audio` — device or null backend (headless tests), clips,
  streamed music, 3D sources/listener components, buses/volumes, audio-level meters in the run
  report so agents can verify sound without hearing it.
- **Animation:** glTF skins + clips, `Animator` component, blending/crossfades, GPU skinning
  path in the renderer, simple property tweens.

### Phase F — Introspection and verification (2–3 sessions)
- Runtime command channel (JSON over stdin or a local socket, debug builds): `list entities`,
  `get/set component`, `spawn prefab`, `screenshot`, `stats`, `step N`, `quit` — lets an agent
  drive and inspect a running game.
- Golden-image tests with tolerance + diff images (`zodar shot --compare`), perf regression
  thresholds from run reports (ZodarBench + game benchmark scene).
- `zodar doctor`: checks toolchain, Vulkan device, validation layers, shader compiler.

### Phase G — Documentation for agents (continuous; consolidated after B)
- `docs/api/<module>.md`: purpose, key types, ownership/lifetime rules, threading rules,
  minimal example — generated skeleton + hand-written notes; every public header gets a
  top-of-file usage comment.
- `docs/guide/RECIPES.md`: task-oriented snippets ("add a light", "load a glTF", "make a
  material", "spawn physics box", "play a sound", "take a screenshot", "write a GPU test").
- `examples/`: one small, tested program per feature (`hello_scene`, `materials`, `lighting`,
  `voxel_world`, `physics`, `audio`, `animation`, `input_replay`), each run in ctest headless.
- CLAUDE.md links the guide and lists the standard commands.

### Rendering work interleaved
S5 shadows (needed by the Lastlight look and Phase B light components), S6 tonemapping/TAA,
compact voxel vertices, GeometryPool free-list (GPU-driven terrain), then S7+ per the existing
ROADMAP.md.

## 3. Explicitly out of scope (for now)
Visual editor, scene-graph GUI, standalone SDK/installer, marketplace, scripting language,
networking (DEAD SECTOR M7 stays on the engine roadmap but after Phase E).

## 4. Suggested order of the next sessions
1. ~~A1 `Application` + headless mode; migrate the game.~~ Done.
2. ~~A2 standard CLI, PNG capture, run report JSON.~~ Done.
3. **A3 (next)** CMake presets + `tools/zodar.py` + RUNNING guide; migrate ModelViewer/ZodarBench.
4. B1 EnTT world, Transform hierarchy, renderer bridge.
5. B2 scene/material JSON + reflection + round-trip tests.
