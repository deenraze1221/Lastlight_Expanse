# Phase D plan — Lastlight bunker first (approved rev 2, 2026-11-01)

Status: **approved; D1 done (2026-11-01).** Next session: D2. Roadmap summary:
`docs/AI_FIRST_ROADMAP.md` (Phase D). This file is the binding plan; update it when a session
changes scope, and record completed sessions in `PROGRESS.md`.

## Authoritative game specification

`docs/GDD/Lastlight_Expanse_GDD_v1.1.md` is the authoritative specification for Lastlight
Expanse gameplay, world, art direction and features.
- All game-specific work in this plan (D2, D7 and the game parts of D3-D6, D8) must follow the
  GDD. Read the relevant GDD sections before implementing a game-specific feature (bunker:
  §1, §3, §4 electricity, §5, §10; saving: §11; art: §3).
- Never silently change a GDD requirement. A conflict or gap between this plan and the GDD is
  listed in "GDD alignment" below and decided by the user before implementation.
- ZODAR Engine stays generic: engine sessions (D1, D3-D6) add no Lastlight names, content or
  rules to `engine/`; game-specific code, data and assets go to `game/`.

## Approved decisions

1. **Bunker art:** a script-generated greybox kit comes first (D2). Real Blockbench and Blender
   exports replace modules later under the same file names (hot reload picks them up). The user
   supplies one real `.glb` from Blockbench and one from Blender to validate D1.
2. **Collision:** temporary game-side static AABB collision behind a replaceable interface
   (`game/src/Physics/CollisionWorld.h`: `sweep`, `raycast`, `addStatic`, `clear`) so that
   Phase E can swap in Jolt without changing the player controller or interaction systems.
3. **Default level:** `bunker`. `--level highlands` stays and is the regression scene
   (byte-identical screenshot tests keep using it).
4. **Phase D size:** 8 sessions (D1–D8), roadmap updated.
5. **`.blend` sources:** regular Git, no Git LFS in Phase D (revised 2026-11-01, user
   decision). Every project file, `.blend` included, is a normal Git file; no LFS
   configuration or tracking. Repository growth is limited by the source-file policy in D2
   step 0 (compressed saves, unpacked textures, 20 MB per `.blend`, no backup files).

## GDD alignment (approved 2026-11-01)

The plan was approved before the GDD was added; these decisions align it with the GDD and are
part of the binding plan.
1. **Bunker content (GDD §10, §9) — D2:** the greybox adds merchant areas (five roles: weapon
   dealer, medic, resource trader, technician, scout), a locker room and a common area. Static
   spaces only: no NPC, merchant, trading or locker functionality in Phase D. No construction,
   upgrades or expansion (GDD §10).
2. **Saving (GDD §11) — D6:** rotating backups and a configurable autosave interval (game sets
   5 minutes and saves on normal exit). Feature-specific save data (inventories, lockers,
   missions, world changes) is added later by the systems that own it.
3. **Multiplayer (GDD §11) — D3-D7 constraint:** no networking in Phase D. The architecture
   stays compatible with host-authoritative multiplayer (simulation driven only by fixed-step
   actions, events and saved state; no hidden global state in systems), but player ids and
   all player-specific logic live in the game layer. Generic engine APIs (Input, scheduler,
   events, save) have no player concept.
4. **Electricity (GDD §4) — D7:** besides the generator, repairable electrical panels and
   emergency lighting (on while main power is off).
5. **Art (GDD §3) — D2 onward:** the master palette follows GDD §3: muted, faded colours,
   Eastern European post-war atmosphere; 32×32 default, 64×64 maximum, nearest filtering, PBR.

## Findings that shape the plan

- `game/` has no `game/assets/`; input is hard-coded SDL in `game/src/main.cpp` (fly camera,
  dig/place).
- The engine has no input, scheduler, events, timers, RNG service or save system;
  `Application` only has hooks.
- Scene files reject unknown fields, so the game cannot add components (doors, colliders) yet:
  the engine needs an extension point.
- `ModelRenderer` draws only through the CPU draw path; sufficient for bunker scale.
- Physics is Phase E (Jolt); the bunker uses decision 2.
- Point lights cast no shadows (S5 is sun only); light ranges must stay inside rooms.

## Asset workflow: one pipeline, two tools

| | Blockbench (primary) | Blender (secondary) |
| --- | --- | --- |
| Use | Props, items, weapons, characters, mutants, modular bunker kit | Complex architecture (curved or large structures), organic models |
| Source | `game/assets/sources/blockbench/*.bbmodel` (plain JSON, normal Git) | `game/assets/sources/blender/*.blend` (regular Git, compressed, ≤ 20 MB each) |
| Export | `.glb`; 16 Blockbench units = 1 block = 1 m | `.glb`; 1 unit = 1 m, +Y up, transforms applied, Draco off, image interpolation "Closest", no WebP |

Shared pipeline: export `.glb` to `game/assets/models/` → `zodar.py import` (registry defaults:
pixel-art sampling, lossless compile) → `zodar.py assets check --strict` (size, palette, texel
density, triangle budget, importer notes) → prefab/scene references by registry id →
`LiveScene` + `AssetManager` hot reload in the game.

One standard for both tools: textures 32×32 default, 64×64 maximum; master palette +
sub-palettes; 32 texels/m (16..64 accepted); nearest sampling with linear mips; no BCn on pixel
art; triangle budget per asset class. The engine stays generic: it reads glTF
`asset.generator` only to report the source tool and never parses `.bbmodel` or `.blend`.

## Engine vs game split

| Engine (reusable, no Lastlight or tool-project knowledge) | Game (`game/`) |
| --- | --- |
| `Zodar::Input` (`engine/input/Input`): actions, `input.json`, keyboard/mouse/gamepad, contexts, record/replay | `game/assets/input.json`, game actions |
| `Core/Random`: seeded named streams | Gameplay randomness |
| `Zodar::Gameplay` (`engine/gameplay/Gameplay`, depends on World + SceneFile): `SystemScheduler`, `EventBus`, `Timers`, `SaveGame` | Systems (player, doors, power), events, save handlers |
| SceneFile registry for game-defined components | `Collider`, `Door`, `PowerSwitch`, `Interactable`, `PlayerSpawn` |
| glTF import hardened for Blockbench and Blender exports; registry defaults, budgets, bounds checks | Sources, exports, palette, budgets, kit, prefabs, levels |
| `zodar.py` batch import, `replay` | Bunker level, player controller, `CollisionWorld` (AABB) |

## Sessions

Every session reads `CLAUDE.md` and `PROGRESS.md` plus its own read list, and ends with the
handoff block described under "Session workflow".

### D1 — Unified GLB/glTF pipeline for Blockbench and Blender (engine, generic)
- Read: `docs/guide/ASSETS.md`, `docs/ART_DIRECTION.md` §3–5,
  `engine/assets/Assets/GltfLoader.cpp`, `engine/rendering/AssetManager/AssetManager/AssetRegistry.h`.
- Inputs from the user: one real Blockbench `.glb` and one real Blender `.glb` (to
  `engine/tests/data/gltf_tools/real/`, small, committed with `-text`). If not yet provided,
  D1 finishes with the generated fixtures and the real-export test is added when they arrive.
- Fixtures generated by script (`engine/tests/data/gltf_tools/make_fixtures.py`, stdlib only)
  that mirror each exporter's structure:
  - Blockbench: images in the `.glb` binary buffer and as `data:` URIs; several textures per
    model (one primitive per texture); node hierarchy with pivots; per-face UVs; `NEAREST`
    samplers; clashing and space-containing names.
  - Blender: Principled BSDF metallic-roughness materials; `KHR_materials_emissive_strength`;
    other `KHR_materials_*` (specular, ior, transmission); vertex colours; second UV set;
    tangents; smooth normals; non-uniform node scale.
  - Rejected: `KHR_draco_mesh_compression`, `EXT_texture_webp`, `KHR_mesh_quantization` in
    `extensionsRequired` → error naming the extension and the export setting that fixes it.
- Importer: one note per unsupported feature (animations/skins until Phase E, cameras, morph
  targets, extra UV sets, ignored material extensions); hierarchy and pivots preserved
  (bounds tests); report field `sourceTool` (`blockbench|blender|other`, from
  `asset.generator`).
- Registry (`assets.json`, schema stays v1 with optional fields): `"defaults"` per type
  (e.g. `{"model": {"sampling": "pixelArt", "compile": true}}`); `"budgets"` (asset class →
  max triangles) with entry field `class` (`prop|character|architecture|...`), over budget =
  warning; bounds check (< 1/16 m or > 64 m = warning, catches wrong export scale).
- Tool: `zodar.py import DIR` (batch, deterministic order).
- Docs: new `docs/guide/MODELING.md` (export settings for both tools, units, folders,
  standards, check, hot reload, checklist); `docs/ART_DIRECTION.md` §3–5 (Blender as
  secondary tool, one standard).
- Tests: unit cases per fixture, rejected extensions and messages, defaults round-trip,
  budgets, bounds, batch import; GPU case: Blockbench-style and Blender-style `.glb` render
  byte-exact with pixel-art textures.
- Acceptance: all valid fixtures import and pass `assets check --strict`; rejected cases fail
  with actionable messages; the report shows the source tool and budget/palette deviations;
  existing registry files load unchanged.
- **Done (2026-11-01)** with generated fixtures. Scope notes: importer messages split into
  `warnings` (the model looks different) and `notes` (ignored data; not a check warning, so
  exports with animations or Blender material extensions pass `--strict`); optional
  (non-required) Draco falls back to the uncompressed data with a note; `EXT_meshopt_compression`
  and `KHR_texture_basisu` are rejected like the three planned cases. Open: the real-export
  test runs automatically once files exist in `engine/tests/data/gltf_tools/real/`.

### D2 — Game asset layout + static bunker (game)
- Read: `docs/guide/MODELING.md`, `docs/guide/SCENE_FILES.md`, `game/src/main.cpp`,
  `game/CMakeLists.txt`.
- Step 0 (source-file policy, decision 5; no Git LFS, nothing to confirm):
  - `.gitattributes` (plain Git rules, no LFS filters): `game/assets/** -text` (registry
    hashes cover exact bytes) and `*.blend binary` (no text diff or merge).
  - `.gitignore`: `*.blend1`, `*.blend2` (Blender backups) and `game/assets/**/*.zodar/`
    (compiled texture caches, rebuilt by `zodar.py import --compile`).
  - `.blend` rules: save with *File > Save > Compress*; textures stay separate PNGs in
    `sources/blender/textures/` (not packed); at most 20 MB per `.blend` (GitHub warns at
    50 MB and rejects files above 100 MB).
  - ctest `tools.game_source_sizes` (stdlib Python): fails if any `.blend` under
    `game/assets/` exceeds 20 MB, or if any `.blend1`/`.blend2` backup exists there. It
    applies no size limit to `.glb`, PNG or other assets: their limits come from the asset
    checks where applicable (texture size standards, triangle budgets per class, bounds).
  - Document the policy in `MODELING.md`.
- Source-file preservation: every `.glb` in `models/` has its source committed next to it
  under the same base name (`sources/blockbench/<name>.bbmodel` or
  `sources/blender/<name>.blend`); script-generated greybox modules have the committed
  generator script as their source; real exports replace modules under the same file names,
  together with their sources.
- Read also: GDD §1, §3, §9, §10.
- Layout: `game/assets/{assets.json, palette.png, sources/blockbench/, sources/blender/,
  models/, materials/, prefabs/, levels/bunker.scene.json}`; `assets.json` holds defaults,
  budgets, master palette and sub-palettes.
- Greybox kit (`game/tools/make_greybox_kit.py`, stdlib only, deterministic bytes):
  Blockbench-style floor, wall, doorway, corner, ceiling, pillar, stairs, door leaf, lamp,
  crate, bunk, locker, terminal, generator, electrical panel, emergency light, merchant counter,
  shelf, table, bench; one Blender-style curved airlock arch. 32×32 textures from the master
  palette (muted, faded post-war colours, GDD §3). File names are the long-term names real exports will replace.
- Level (static greybox, GDD §10): dormitory, corridor, generator room, airlock, five merchant
  areas (weapon dealer, medic, resource trader, technician, scout), locker room (private
  lockers as props), common area. Point lights limited to their rooms, emissive lamps,
  emergency lights (props until D7), dim fog. No NPC, merchant or locker functionality.
- Game: `--level bunker|highlands` (default `bunker`); loads through `LiveScene` +
  `AssetManager` (async, hot reload in Debug); existing fly camera. Existing highlands tests
  pass `--level highlands`.
- New ctests: `tools.game_assets_check` (strict), `tools.game_source_sizes`,
  `gpu.lastlight_bunker_smoke`.
- Acceptance: readable bunker PNG; editing a `.glb`, material or scene in Debug reloads live;
  highlands screenshot byte-identical; checks clean for both tool paths;
  `tools.game_source_sizes` passes.

### D3 — Input action maps (engine)
- Read: `engine/runtime/Runtime/Application.h`, `CommandLine.h`, game input code.
- `Zodar::Input`: button, 1D axis, 2D axis actions; key, mouse button, mouse delta, wheel,
  gamepad button/stick bindings; dead zone, scale, invert; contexts with priority; relative
  mouse mode.
- `input.json` (`zodar.input_map` v1): `DataError` with line:column, unknown fields rejected,
  deterministic serialize.
- Fixed-step sampling: button edges and accumulated mouse delta per fixed step.
- Integration: `ApplicationDesc::inputMap`; events reach input before `onEvent`; `inject()`
  for tests and headless runs.
- Game: fly camera, dig and place move to actions; old SDL code removed only after the
  screenshot tests pass.
- Tests: parse/errors, step edges, mouse delta, contexts, dead zones, injection; game smoke
  tests unchanged.
- Acceptance: no game input reads SDL directly; bindings come only from JSON.

### D4 — Record/replay + deterministic RNG (engine)
- Read: Input headers (D3), `engine/runtime/Runtime/RunReport.h`, `docs/guide/RUNNING.md`.
- `Core/Random`: named streams seeded from `--seed`, independent of call order between
  systems.
- `*.inputrec.jsonl` (`zodar.input_record` v1): header (app, input-map hash, seed, fixed-dt,
  level hash), then one line per fixed step with changes (delta-encoded); stores action
  states, not raw events.
- CLI: `--record-input FILE`; `--replay-input FILE` (implies `--deterministic`; hash mismatch
  is an error unless `--replay-lenient`); the run ends with the replay unless `--frames`.
  Run report gains an `input` section. `zodar.py replay FILE` (short summary).
- Tests: round-trip, delta encoding, hash mismatch, truncated file; GPU: record, replay twice,
  byte-identical screenshots.
- Acceptance: a hand-written recording replays headless with identical results on repeat runs.

### D5 — System scheduling, events, timers (engine)
- Read: `Application.h/.cpp`, `engine/world/World/World.h`, `FixedTimestep.h`.
- `SystemScheduler`: phases `Input`, `FixedSimulation`, `Animation` (empty until Phase E),
  `LateUpdate`, `RenderSync`; systems with name and optional `before`/`after`; deterministic
  topological order (ties: registration order); cycles and unknown names are errors; one CPU
  scope per system in the run report.
- `EventBus`: typed, double-buffered per fixed step (sent in step N, read in N+1).
- `Timers`: tick-based, one-shot/repeating, cancellable.
- Integration: per fixed step `Input` → systems → `onFixedUpdate` → event flip + timers; per
  frame `LateUpdate` → `onUpdate` → `RenderSync` → `onRender`. No systems = today's behaviour.
- Tests: ordering, cycles, event timing, timers, no-systems app; runtime smoke unchanged.
- Acceptance: the game camera and dig/place run as systems with identical screenshots.

### D6 — Game components in scene files + save/load (engine)
- Read: `SceneFile/SceneFile.h`, `World/Reflection.h`, `docs/guide/SCENE_FILES.md`,
  `AssetManager/LiveScene.h`.
- `registerSceneComponent<T>(name, fields)`: parse, validate, prefab overrides, serialize,
  capture; unregistered names stay errors; optional inclusion in `SCENE_COMPONENTS.md`.
- `SaveGame` (`zodar.save` v1): header (game id, version, scene path + hash, seed, tick, RNG
  state); `world` (entity snapshot via `captureScene` + registered components, models by
  registry id); `game` (JSON per key from save handlers). Atomic write (temp + rename);
  versioned load with clear errors; load into a new World, swapped in only on success.
- Rotating backups (GDD §11): before a save replaces a file, the previous file rotates to
  `<name>.bak1..N` (`SaveGameDesc::backups`, default 3); load can fall back to the newest
  valid backup when the main file is corrupt (reported, never silent).
- Autosave: configurable interval (`SaveGameDesc::autosaveInterval`, off by default; the game
  sets 5 minutes) on the fixed-step clock, plus save on normal exit when enabled. Generic: no
  player or game concepts in the engine API.
- CLI: `--load-save FILE`, `--save-at-exit FILE`.
- Tests: registered components (parse, override, capture round-trip, unknown field), save
  round-trip, version/corrupt/missing errors, atomic write, backup rotation and fallback,
  autosave interval (deterministic ticks) and save on exit; GPU determinism: replay to tick N,
  save, load, continue = uninterrupted screenshot.
- Acceptance: determinism case passes; a failed load leaves the running World unchanged.

### D7 — Playable bunker (game)
- Read: `docs/guide/INPUT.md` and `GAMEPLAY.md` (drafted in D3–D6), `game/src`,
  `game/assets/levels/bunker.scene.json`.
- Read also: GDD §4 (electricity), §10, §11.
- Components: `Collider` (static box), `Door`, `PowerSwitch`, `ElectricalPanel` (broken /
  repaired), `EmergencyLight`, `Interactable`, `PlayerSpawn`; colliders authored in prefabs (doorways walkable).
- `CollisionWorld` interface (decision 2) with the AABB implementation: axis-separated sweep,
  raycast, static boxes only.
- Systems: `PlayerController` (walk, sprint, crouch, look, gravity, steps/stairs) using
  `CollisionWorld`; `Interaction` (ray against `Interactable`); `Doors` (timer-driven
  sliding); `Power` (generator switch and repairable panels; main lights on only when the
  generator runs and the room's panel is repaired; emergency lights on while main power is off;
  light intensity + lamp emissive via `updateMaterial`). Player id and player-specific state
  are game-side (decision 3).
- `--free-cam` keeps the fly camera; F5/F9 quick save/load through `SaveGame` with handlers
  for player, doors, panels and power; game autosave every 5 minutes and on normal exit
  (D6 interval).
- Recording `game/tests/bunker_walk.inputrec.jsonl`: dormitory → repair panel → start
  generator → door → airlock.
- Tests: AABB sweep (walls, corners, steps, no tunnelling at max speed), door, panel and power
  state machines (emergency lights on without power, main lights only with generator + repaired
  panel); GPU `gpu.lastlight_bunker_walk` (ends in the airlock within tolerance, main lights on,
  emergency lights off,
  screenshot, 0 validation messages).
- Acceptance: a human or the replay can walk the bunker; all interaction runs through actions,
  systems and events; no system depends on the AABB implementation directly.

### D8 — Integration, audit, docs
- Read: `PROGRESS.md` handoff notes D1–D7, then the diff per review area.
- Save/load mid-replay in the bunker equals the uninterrupted run.
- Two read-only review subagents: (1) input/replay/scheduler determinism; (2) save, SceneFile
  extension, asset pipeline for both tools. Fix findings.
- Docs: `INPUT.md`, `GAMEPLAY.md`, `MODELING.md`; ARCHITECTURE, roadmap, ART_DIRECTION §4,
  PROGRESS; `examples/input_replay` as ctest.
- Bunker benchmark (lavapipe Release): draws, FPS, memory; recorded as baseline.
- Acceptance: Debug + Release 0 warnings, all ctest green, 0 validation messages; highlands
  screenshot byte-identical to pre-Phase D.

## Dependencies

```
D1 -> D2 ----------------------------> D7 -> D8
D3 -> D4 ----------------------------^
D3 -> D5 -> D6 ----------------------^
```

Executed in order D1..D8, one session each. No parallel agents edit code.

## Session workflow (token use and context)

- Caveman (installed): chat replies, status lines and summaries stay terse. Code, comments,
  commit messages, docs and test names are written in normal prose. Correctness, technical
  detail, test coverage and documentation quality are never reduced to save tokens.
- One session = one D-step, started fresh. Read only `CLAUDE.md`, `PROGRESS.md` and the
  session's read list; read large files in targeted ranges or via `grep`.
- No full-file dumps or full build logs; quote failing lines only. Build and test through
  `python3 tools/zodar.py` (summaries). Focused tests first (`-tc=...`), then full Debug ctest
  with GPU tests, then Release before pushing.
- End of every session: a handoff block in `PROGRESS.md` (done, tests + results, decisions,
  open debts, next session's read list and first step); new decisions in
  `docs/ARCHITECTURE.md`; schemas documented in the guides when introduced, so later sessions
  read the guide instead of the code; scope changes recorded in this file.
- Subagents only for independent read-only reviews (D8, or an audit a session needs), with a
  short evidence report.
- Commit and push only when builds, tests and validation pass; report anything not run.

## Out of scope and risks

- Out of scope: Jolt physics, audio, animation playback (Phase E); shadows (S5); streaming
  budgets; terrain textures re-authored to 32×32 (optional D2b).
- Fixtures only imitate exporters; the user's real exports confirm D1.
- Blender assets can drift from the voxel style (smooth normals, high polygon counts,
  off-palette textures): budgets, palette and texel-density checks catch this.
- Point lights bleed through walls without shadows: light range ≤ room size, thick walls.
- The AABB collision is temporary; it stays in `game/` behind `CollisionWorld`.
- Determinism breaks if a system reads frame time or SDL state directly; replay tests detect it.
- Repository growth: every `.blend` revision stays in Git history (no LFS). Kept small by
  compressed saves, unpacked textures, the 20 MB cap per `.blend` and ignored backups; Git LFS
  can be reconsidered after Phase D if sources grow.
