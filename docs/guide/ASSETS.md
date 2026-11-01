# Assets: textures, glTF models and materials (Phase C1, async C2, registry C3, scene models C4, hot reload C5/C6, palettes C7, tool exports D1)

Module `Zodar::AssetManager` (`engine/rendering/AssetManager/AssetManager/AssetManager.h`).
Working example: `examples/asset_loading` rendered by the sample `ZodarAssetLoading`
(ctests `gpu.asset_loading_example` deterministic, `gpu.asset_loading_async` frame-loop path).

## Loading

```cpp
#include "AssetManager/AssetManager.h"

zodar::AssetManagerDesc desc;
desc.root = "assets";                                  // relative paths resolve here
desc.defaultSampler = zodar::gfx::SamplerDesc::pixelArt(); // pixel-art game; engine default: linear
zodar::AssetManager assets(renderer().context(), upload(), scene(), desc);

zodar::ModelHandle crate = assets.loadModel("crate.gltf");          // .gltf / .glb
zodar::TextureHandle rune = assets.loadTexture("textures/rune.png"); // PNG/JPEG/TGA/BMP
zodar::MaterialAssetHandle m = assets.loadMaterial("materials/rune.material.json");

if (crate.failed()) ZODAR_LOG_ERROR("{}", crate.error()); // loads never throw
crate->model.appendDraws(draws, transform);            // CPU draw path (SceneView::draws)
view.draws = draws;
```

- Same resolved path + same settings returns the same asset (`stats().hits`).
  Textures are keyed by usage too (`Color` sRGB, `Data`/`Normal` linear) and mip setting.
- `ModelLoadSettings::samplerOverride` forces one sampler for all model textures (separate
  cache entry). Without it the glTF samplers apply; `defaultSampler` fills files that specify
  no filter.
- Scene files: `assets.createSceneMaterials(doc, scenePath)` creates every scene material with
  its textures; `.handles()` fills `SceneBindings::materials`. Meshes are still bound by name.

## Async loading (Phase C2)

```cpp
desc.jobs = &jobs();                                    // Application's JobSystem
zodar::ModelHandle crate = assets.loadModelAsync("crate.gltf");     // returns at once, loading()
zodar::SceneMaterialAssets mats = assets.createSceneMaterialsAsync(doc, scenePath);

void onUpdate(const zodar::FrameTime&) override {
    assets.update();                                    // once per frame, before recording
    if (crate.ready()) crate->model.appendDraws(draws);  // draw only ready assets
    if (!mats.loading() && mats.errors().empty()) { /* instantiate the scene */ }
}
```

- Pipeline: decode (PNG/glTF parse, mips) on the `JobSystem` -> `update()` uploads with
  `UploadBatch::submitAsync` (at most `uploadBudgetBytes` staged per update, at least one
  asset) -> ready once the frame loop acquired the upload (`UploadQueue::isReady`). Async
  materials become ready when all their textures are; a failed texture fails the material.
- `waitAll()` blocks until nothing is pending (loading screens, `--deterministic` runs, tests).
  A blocking `load*()` of an asset that is loading asynchronously also waits.
- `handle.loading()`; `get()` / `->` throw while loading. `stats().pending` / `uploading`.
- Cancel: drop every handle. Decoding results are discarded; a finished upload is retired.
- Without `desc.jobs` (or with 0 workers) decoding runs inline in the load call; uploads stay
  asynchronous. When an asset becomes ready depends on worker and transfer timing: wait
  (`waitAll`) where frames must be reproducible.
- Worker jobs touch only their inputs and a mutex-protected result queue; caches and handles
  stay on the render thread.

## Asset registry and CLI (Phase C3)

`assets.json` next to the assets lists every asset by id (schema `zodar.asset_registry` v1):

```json
{ "schema": "zodar.asset_registry", "version": 1, "assets": [
  { "id": "crate", "type": "model", "source": "crate.gltf", "sampling": "file", "compile": false,
    "hash": "fnv1a64:da26810eac1f9b88", "dependencies": ["textures/crate.png"] } ] }
```

Types: `texture` (`usage` color|data|normal, `mips`), `model` (`sampling`
file|pixelArt|nearest|linear, `compile`, `outputs`), `material`, `scene`, `prefab`. Paths are
relative to the registry directory. `hash` covers the source and every dependency.

```bash
python3 tools/zodar.py import assets/crate.gltf                      # add/update (nearest assets.json, else ./assets.json)
python3 tools/zodar.py import assets/textures/crate.png --id crate_texture
python3 tools/zodar.py import assets/crate.gltf --compile --sampling pixelArt   # + KTX2 cache
python3 tools/zodar.py assets list -- --registry assets/assets.json
python3 tools/zodar.py assets check -- --registry assets/assets.json --report report.json --strict
```

(`ZodarAssetTool import|list|check` directly takes the same options.) Default ids are file
names without asset extensions; a clash must be resolved with `--id`. Re-importing keeps the
id and settings. Import fails if a dependency is missing. A file is imported under one id
only (`--id` with another id fails); `--id` naming an entry of a different file or type
starts from default settings. Sources are relative paths inside the registry directory
(absolute or `..` sources are rejected when parsing). `assets.json` is written atomically
(temporary file + rename). Malformed numeric options (`--density-target abc`) and an empty
`--palette` are usage errors. A corrupt compiled KTX2 cache falls back to the source image
with a warning.

`check` exit code: 1 if any asset has errors (`--strict`: warnings too). Errors: missing
source or dependency, unreadable/invalid file, textures of 256 or more texels on either side
(prohibited, Phase V2). Warnings: stale (source/dependency changed since import: re-import),
texture size violations (outside the entry's texture class; engine default: above 64×64,
non-standard), texel density outside target/2..target×2 (registry `textureStandards`, engine
default 32; `--density-target N` overrides), glTF importer warnings, missing
compiled outputs, triangle budget exceeded and model bounds outside 1/16..64 m (Phase D1).
glTF importer *notes* (ignored data such as animations, extra UV sets or unsupported
material extensions) are reported but are not warnings, so `--strict` passes with them. The
report (`zodar.asset_report` v1) has per asset: status, errors, warnings, textures (size +
class), meshes, vertices, triangles, materials, texelDensity; models also have `sourceTool`
(`blockbench|blender|other`, from glTF `asset.generator`), `notes`, `size` [x, y, z] in metres
and, with a class, `class` and `triangleBudget`; entries with a texture class also have
`textureClass`. Registries without `textureStandards` produce byte-identical reports to
before V2 (unless a texture is ≥ 256: now an error).

### Defaults, budgets and batch import (Phase D1)

Optional top-level fields (schema stays v1; registries without them load and save unchanged):

```json
"defaults": { "model": { "sampling": "pixelArt", "compile": true, "class": "prop" },
              "texture": { "usage": "color", "mips": true } },
"budgets": { "prop": 2000, "character": 6000, "architecture": 20000 }
```

- `defaults` per type (`texture`: usage, mips; `model`: sampling, compile, class) apply when a
  file is imported for the first time and the option is not given. Entries always store
  their settings, so changing defaults never changes how existing entries load.
- `budgets`: asset class -> maximum triangles (positive). Model entries may have `"class"`
  (`--class NAME` on import); it must name a budget (parse/import error otherwise). Over
  budget: warning `triangles: N triangles exceed the 'prop' budget of M`.
- `zodar.py import DIR` imports every asset file below `DIR` in sorted path order, skipping
  hidden entries, `*.zodar` caches, the registry's palette swatches and files another found
  asset depends on (e.g. a `.gltf`'s external textures, covered by its hash). Without
  `--registry` the nearest `assets.json` at or above `DIR` is used.
- Export settings per tool and the modeling checklist: `docs/guide/MODELING.md`.

### Palette validation (Phase C7)

Optional `"palette"` at the top of `assets.json` (no palette: checks and reports are unchanged):

```json
"palette": "palette.png"                                   // PNG swatch, or
"palette": ["#3a2618", "#966234"]                          // hex list, or
"palette": { "colors": "palette.png", "subPalettes": { "glow": ["#5adcff"], "ash": "palettes/ash.png" } }
```

- Swatch: every RGB colour of its non-transparent texels (any layout/size). Hex: `#rrggbb`.
- Sub-palettes extend the master palette: an entry with `"palette": "glow"` may use master +
  glow colours (`zodar.py import FILE --sub-palette glow` sets it; unknown names are errors).
- `check` counts the level-0 texels of colour textures (texture entries with usage `color`,
  material base colour + emissive slots, model images used as base colour/emissive) that are
  not in the palette. Fully transparent texels are ignored; normal/data maps are not checked.
- Any off-palette texel is a warning: `palette: 4 of 60 texels (6.67%) not in palette 'master'
  (e.g. #ff00ff)`; `--strict` turns it into exit code 1. The tool prints a palette summary line.
- Report: `"paletteDeviation": {palette, texels, offPalette, percent, examples}` per asset and
  `paletteTexels` / `offPaletteTexels` per texture (only when a palette applies).
- `--palette swatch.png` or `--palette "#rrggbb,#rrggbb"` replaces the master palette for one
  run (sub-palettes still apply). A missing swatch or malformed colour is an asset error.

Loading by id:

```cpp
const zodar::AssetRegistry registry = zodar::AssetRegistry::load("assets/assets.json");
const zodar::AssetEntry& crate = registry.at("crate");
zodar::ModelHandle h = assets.loadModelAsync(registry.sourcePath(crate), zodar::modelLoadSettings(crate));
```

## Scene model references (Phase C4)

```json
{ "name": "Crate", "transform": { "position": [0, 0.5, 0] }, "modelRenderer": { "model": "crate" } },
{ "name": "Small crate", "modelRenderer": { "model": "props/crate.gltf", "visible": true } }
```

- A reference ending in `.gltf` / `.glb` (any case) is a file path relative to the scene file
  (prefab paths are rebased like material files); anything else is an `assets.json` id (must be
  a model entry; its import settings apply). Parsing checks that path references exist; an
  entity has a meshRenderer or a modelRenderer, not both. Old scenes are unchanged.
- `assets.loadSceneModels(doc, scenePath, &registry)` (throws DataError listing unresolved and
  failed references) or `loadSceneModelsAsync` (check `loading()` / `errors()`); then
  `bindings.models = models.models();` and `instantiateScene` adds `ModelRenderer` components.
  `collectDraws` draws them (CPU path; `WorldRenderSync`/GpuScene skip them).
- Registry: scenes hash path-referenced models (+ their images); `assets check` reports
  unknown or non-model ids. `captureScene` writes modelRenderer back (`SceneCaptureNames::models`).

## Material texture slots

`*.material.json` (and inline scene materials) accept `baseColorTexture`,
`metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`, `emissiveTexture`:

```json
{ "name": "rune", "baseColorTexture": { "path": "../textures/rune.png", "filter": "pixelArt" } }
```

`filter`: `default` (desc.defaultSampler) | `pixelArt` | `nearest` | `linear`; `wrap`:
`repeat` | `clamp` | `mirror`. A path string alone means default filter + repeat. Paths are
relative to the file holding the material. Schema: `docs/guide/SCENE_FILES.md`.

## Pixel-art filtering

`gfx::SamplerDesc::pixelArt()`: nearest magnification and minification (crisp art pixels),
linear blending between mips (no distant shimmer), anisotropy off. glTF exports from
Blockbench carry `NEAREST` / `NEAREST_MIPMAP_LINEAR` samplers, which map to the same state.
Use `linear` for smooth textures (sky, UI gradients, effects).

## Texture size standards

**Engine default** (`TextureStandard::engineDefault()`, every registry without
`textureStandards`): 32×32 is the default for every asset; 64×64 is the absolute maximum;
8×8 and 16×16 are allowed for tiny details; density target 32 texels/m.

**Configurable per registry (Phase V2):**

```json
"textureStandards": {
  "classes": { "small": [32], "environment": [64], "character": [128] },
  "defaultClass": "environment",
  "prohibitedSize": 256,
  "densityTarget": 64,
  "densityTolerance": 2
},
"defaults": { "texture": { "textureClass": "environment" }, "model": { "textureClass": "environment" } }
```

- `classes`: texture class -> allowed square sizes (powers of two, ascending, below
  `prohibitedSize`); kept sorted by name. `defaultClass` applies to entries without a class.
  `prohibitedSize` (default and maximum 256), `densityTarget` (default 32) and
  `densityTolerance` (default 2) are optional. Problems are reported with their JSON path
  (`/textureStandards/classes/env/1: size 48 is not a power of two`).
- Entries (`texture`, `model`, `material`) may set `"textureClass"` (`--texture-class NAME` on
  import, `""` clears; `defaults` for texture and model). It must name a declared class (parse
  and import error otherwise). **`textureClass` is independent of the model `class`** (triangle
  budgets): one selects texture sizes, the other a triangle budget.
- Severity: a size not allowed by the entry's class (or non-square / not a power of two) is a
  **warning** (fails `--strict`); **256 and above on either side is an error** under every
  standard, the engine default included. Nothing is resized.
- Runtime (`AssetManagerDesc::textureStandard`, opt-in): loads by path know no class, so a size
  allowed by any class passes; prohibited sizes are logged as errors but still load. Without
  it the engine default warnings stay unchanged.

**Lastlight's final standard** (GDD §3 Amendment 1, ART_DIRECTION §2): 64×64 standard for
environment, blocks, furniture, props and equipment; 32×32 small details; 128×128 only
characters and weapons; ≥ 256×256 prohibited without an approved exception; 8×8 and 16×16 not
approved. Lastlight's `assets.json` declares it in **V5** (approved V2 decision), when the D2
greybox textures are re-made at 64×64; until then the game registry uses the engine default
so `tools.game_assets_check --strict` stays green.

Violations load unchanged but add a warning to `TextureAsset::warnings` /
`ModelAsset::warnings`, the log and `zodar.py assets check` (`Assets/TextureStandards.h`,
`checkTextureSizes`). Textures are never resized automatically.

## Pixel-art compression

Pixel-art textures are never block-compressed (BC7/BCn are lossy). `compileModelTextures` /
`ZodarAssetCompiler model` / `zodar.py import --compile` write uncompressed RGBA8 KTX2 for pixel
art (level 0 byte-identical to the PNG; mips are box-filtered) and BC7 only for other textures.
Pixel art = a glTF sampler with NEAREST magnification (Blockbench exports), registry
`sampling` pixelArt/nearest, a nearest `ModelLoadSettings::samplerOverride`, or `--pixel-art`.
`loadGltf` ignores a block-compressed cache for pixel-art textures (`textureStats.lossyRejected`,
warning) and decodes the source instead; the compiler replaces such caches. Compiled caches from
compiler version 1 are ignored (key version 2). Tests: `pixel art:*` (unit) and
`asset manager: pixel-art textures reach the GPU byte-exact` (GPU readback).

## Lifetime rules

- Handles share ownership. The last handle dropped removes the cache entry and retires the GPU
  objects on the device deletion queue (destroyed after frames in flight complete). Materials
  return their `MaterialSystem` slot (`releaseMaterial`, reused by later materials); textures
  their bindless slot. A material asset keeps its textures alive.
- Failed loads: `handle.failed()`, `handle.error()` (file name + reason, e.g. missing texture,
  unknown shader, invalid glTF). `handle.get()` / `->` throw `zodar::Error` for failed handles.
  The failure is cached while handles exist; drop them to retry.
- Single-threaded (render thread). Release every handle before the AssetManager is destroyed;
  declare it before the members holding handles. Its destructor waits for the device.
- Pending async loads at destruction are dropped after a device wait. Streaming budgets are a
  later phase (`docs/AI_FIRST_ROADMAP.md`).

## Hot reload (Phase C5)

```cpp
AssetReloadResult r = assets.reloadFiles({"textures/crate.png"}); // every asset using the file
assets.reload(textureHandle);          // or one asset (texture, model, material file)
if (!r.ok()) for (auto& f : r.failed) log(f); // "<key>: <error>"; previous version kept
```

- The new GPU objects are swapped into the existing cache record: handles, `&handle->texture`,
  `&handle->model` and material handles (`MaterialAsset::handle`) stay valid; only the
  content changes. `handle.generation()` counts successful reloads.
- The previous GPU objects go to the device deletion queue (frames in flight finish first).
- Materials using a reloaded texture are refreshed (`MaterialSystem::updateMaterial`, GPU
  entry rewritten by the next recorded frame; `AssetReloadResult::materialsRefreshed`).
- Material files are re-parsed; their textures load through the cache. Changing `shader`,
  `alphaMode` or `doubleSided` is rejected (pipeline state; restart instead).
- Models reload when the `.gltf`/`.glb` or one of its `.bin`/image files changes. A reloaded
  model has new gfx material handles: read them from the model (e.g. `appendDraws`) every
  frame instead of caching them.
- Failure (missing/broken file, unknown shader, missing texture): nothing changes; the error
  is returned and logged. A *failed* asset whose file is fixed becomes ready (failed file
  materials are retried on every `reloadFiles`).
- Materials created from data (`createMaterial`, inline scene materials) change through
  `updateMaterialData({{handle, data, baseDirectory}, ...})` (all or nothing).
- Reloads are blocking (decode + upload on the calling thread). An asset still loading
  asynchronously is skipped and its changed file retried by `update()` once it finished.
- Watching baselines a file when it is read (load or reload), so a save during a load is
  still reported; explicit reloads consume earlier changes (`FileWatcher::refresh`).

### File watching (opt-in, Debug builds)

```cpp
AssetManagerDesc desc;
desc.watchFiles = true;                       // off by default
desc.watchInterval = std::chrono::milliseconds(250);
// assets.update() every frame now polls the files of all live assets and reloads changes
```

- Compiled in only for Debug builds with the CMake option `ZODAR_HOT_RELOAD` (default ON);
  `zodar::kHotReloadAvailable` tells. Release builds ignore `watchFiles`; explicit `reload*`
  calls work in every build.
- `Core/FileWatcher` polls size + modification time (no OS notification API, no threads).
  `assets.watchedFiles()` lists what is watched; `assets.pollFileChanges()` checks now.
- A half-written file fails to reload and is picked up again by the next change.

### Shader hot reload (Phase C6)

```cpp
assets.reloadShader("zodar/pbr");          // or reloadFiles({".../pbr.frag.spv"}), or watching
materials.reloadShader(handle, context.deletionQueue()); // MaterialSystem level
```

- Material shaders (`MaterialSystem::registerShader`, engine `zodar/pbr`, `zodar/unlit` and
  game shaders) reload from their SPIR-V files; rebuild them with the normal build
  (`cmake --build`, GLSL -> `bin/shaders/*.spv`) while the app runs with `watchFiles`.
- Every cached colour and depth pipeline of the shader is rebuilt and swapped into the existing
  `GraphicsPipeline` object (references stay valid); old pipelines/modules are retired.
- Rejected without changes: missing file, not SPIR-V, truncated, no entry point of the right
  stage, pipeline creation error. The shader must keep the engine interface (vertex inputs,
  shared layout); mismatches are reported by validation. Compute/post shaders do not reload.

### Scene hot reload (Phase C6)

```cpp
#include "AssetManager/LiveScene.h"
LiveSceneDesc desc;
desc.meshes["cube"] = &cubeMesh; // meshRenderer meshes from code
desc.registry = &registry;       // optional: modelRenderer ids
desc.watch = true;               // Debug builds: update() reloads after a file change
LiveScene level(assets, world, "levels/a.scene.json", desc);
if (!level.load()) log(level.error());
level.update();                  // every frame
Entity door = level.instance().byId.at("door"); // re-read after a reload (generation())
```

- Watches the scene file and every prefab it uses (`files()`); `reload()` works in every build.
- Success: new entities are created, then the old ones (with their subtrees) destroyed; changed
  inline materials keep their gfx handle (`updateMaterialData`). Material files, textures and
  models are reloaded by the AssetManager (`watchFiles`).
- Failure (syntax/schema, missing prefab/material/texture/model/mesh binding, inline-material
  pipeline-state change): the previous entities, bindings and assets stay; `error()` says why.
- Components or children that code added to scene entities are lost on reload; the
  destructor destroys the scene's entities.
