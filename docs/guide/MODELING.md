# Modeling: Blockbench and Blender to glTF (Phase D1)

One pipeline, two tools. Both export `.glb` into the same registry, check and hot-reload path
and follow one art standard (`docs/ART_DIRECTION.md`). Registry, check and loading details:
`docs/guide/ASSETS.md`. The engine reads glTF only; it never parses `.bbmodel` or `.blend`
files and treats every export the same way. `asset.generator` is read only to report the
source tool (`blockbench`, `blender` or `other`).

**Verification status (Phase D1):** the pipeline is verified only against *synthetic*
fixtures written to resemble each exporter's output (`engine/tests/data/gltf_tools/valid/`).
Compatibility with real Blockbench and Blender exports is **not yet verified**: it needs one
real export from each tool in `engine/tests/data/gltf_tools/real/`. The export settings and
menu names below come from the tools' documented behaviour and have not been checked against
a real export in this repository.

| | Blockbench (primary) | Blender (secondary) |
| --- | --- | --- |
| Use | Props, items, weapons, characters, creatures, modular kit pieces | Large or curved architecture, organic shapes |
| Units | 16 Blockbench units = 1 block = 1 m | 1 unit = 1 m |
| Source files | `.bbmodel` (plain JSON, normal Git) | `.blend` (regular Git: compressed, ≤ 20 MB, textures not packed) |

## Folders (recommended game layout)

```
game/assets/
  assets.json                    registry: palette, defaults, budgets, entries
  palette.png                    master palette swatch (or a hex list in assets.json)
  sources/blockbench/*.bbmodel   sources, versioned next to the exports
  sources/blender/*.blend
  models/*.glb                   exports (the registry entries point here)
```

Mark the asset folder `-text` in `.gitattributes`: registry hashes cover exact file bytes.
All sources are regular Git files (no Git LFS). For `.blend` files: mark them `binary`, save
with *File > Save > Compress*, keep textures as separate PNGs in `sources/blender/textures/`
(not packed), stay at or below 20 MB per file and never commit Blender backups (`*.blend1`,
`*.blend2`; ignore them in `.gitignore`). A game can enforce this with a test (Lastlight:
`tools.game_source_sizes`); other assets have no file-size cap, their limits are the asset
checks (texture sizes, triangle budgets, bounds).

## Export settings

### Blockbench

- *File > Export > Export glTF Model*, binary `.glb` (a `.gltf` with embedded textures works
  too and imports identically).
- Keep the default scale (16 units = 1 m). The bounds check warns below 1/16 m or above
  64 m, which catches a model exported at the wrong scale.
- Textures: PNG, 32×32 by default, 64×64 maximum (8×8 and 16×16 for tiny details). Several
  textures per model are fine: the exporter writes one primitive per texture.
- Blockbench writes NEAREST samplers, so its textures are pixel art automatically (lossless
  compile, nearest sampling).
- Groups become nodes that carry the pivot; cubes are their children. The importer keeps
  the hierarchy (`ModelNode::path`, e.g. `crate/lid group/cube`) and the pivots (world
  matrices). Duplicate names and names with spaces are allowed.
- Animations may be exported; until animation playback exists they are ignored with a note.

### Blender

- Scene: *Unit System* Metric, *Unit Scale* 1.0. Model at 1 unit = 1 m, +Z up in Blender
  (the exporter converts to glTF +Y up).
- Apply transforms before exporting (*Object > Apply > All Transforms*) unless a node scale
  is intended; the bounds check catches doubled or missing scale.
- Materials: Principled BSDF only (base colour, metallic, roughness, normal, emission). Image
  Texture nodes: *Interpolation* **Closest** (exports NEAREST samplers = pixel art), PNG
  images, 32×32 default, 64×64 maximum, quantised to the game palette.
- *File > Export > glTF 2.0*:
  - *Format*: glTF Binary (`.glb`).
  - *Include*: selected objects or the visible collection; no cameras or lights (ignored,
    reported as notes).
  - *Transform*: **+Y Up** on.
  - *Data > Mesh*: Apply Modifiers on; UVs and Normals on; Tangents optional; vertex colours
    only if the material uses them.
  - *Data > Compression*: **off**. Draco (`KHR_draco_mesh_compression`) is rejected.
  - *Material > Images*: **Automatic** or **PNG**, never WebP. `EXT_texture_webp` is rejected.
  - *Animation*: off until animation playback exists (otherwise ignored with a note).
- Optimisers: do not quantise vertex attributes (`KHR_mesh_quantization` is rejected; e.g.
  `gltfpack -noq`), no meshopt compression.

## What the importer does with each feature

| Feature | Result |
| --- | --- |
| Static triangle meshes, metallic-roughness materials, `KHR_materials_unlit`, `KHR_materials_emissive_strength` | Imported |
| Images in the binary buffer, `data:` URIs or external files | Imported |
| Node hierarchy, pivots, non-uniform scale | World matrices per mesh node; `ModelNode::path` |
| Vertex colours (`COLOR_0`), tangents, smooth normals | Imported (normals/tangents generated when missing) |
| Animations, skins (bind pose), morph targets (base shape), cameras, lights, `TEXCOORD_1+`, `COLOR_1+`, other `KHR_materials_*` (specular, ior, transmission, ...) | Ignored, one **note** per feature naming the users |
| `KHR_texture_transform`, a texture using `TEXCOORD_1+`, specular-glossiness, non-triangle primitives | Ignored with a **warning** (the model looks different) |
| Required `KHR_draco_mesh_compression`, `EXT_texture_webp`, `KHR_mesh_quantization`, `EXT_meshopt_compression`, `KHR_texture_basisu`, any other unknown required extension | **Error** naming the extension and the export setting that fixes it |

Notes are informational: `assets check --strict` passes with notes and fails on warnings.

## Import and check

```bash
python3 tools/zodar.py import game/assets/models            # batch: every asset file, sorted
python3 tools/zodar.py import game/assets/models/wall.glb --class architecture
python3 tools/zodar.py assets check -- --registry game/assets/assets.json --strict --report report.json
```

- New entries take the registry `"defaults"` (e.g. `{"model": {"sampling": "pixelArt",
  "compile": true, "class": "prop"}}`); options given on the command line win; re-importing
  keeps an entry's settings.
- `"budgets"` maps asset classes to triangle budgets; an entry's `class` selects one. Over
  budget is a warning.
- The check also warns about texture sizes, off-palette texels, texel density outside
  16..64 texels/m (target 32) and bounds outside 1/16..64 m. The report lists `sourceTool`,
  `notes`, `size`, `class` and `triangleBudget` per model.

## Hot reload

Re-export over the same file name. Games that enable file watching (Debug builds,
`AssetManagerDesc::watchFiles`) reload the model and its textures in place; `LiveScene`
reloads scenes and prefabs that reference it. Re-run `zodar.py import` afterwards so the
registry hash is current (`check` reports stale entries).

## Checklist for a new model

- [ ] Correct units (Blockbench 16 units = 1 m, Blender 1 unit = 1 m, transforms applied).
- [ ] `.glb`, no Draco, no WebP, no quantisation.
- [ ] Textures PNG, 32×32 (64×64 maximum), palette colours only, nearest ("Closest") sampling.
- [ ] Texel density about 32 texels/m (16..64).
- [ ] Triangle count within the class budget; `class` set at import.
- [ ] `zodar.py import` done; `zodar.py assets check --strict` passes (notes are fine).
- [ ] Source file (`.bbmodel` / `.blend`) committed next to the export under the same base
      name; `.blend` saved compressed, ≤ 20 MB, no packed textures, no `.blend1` backups.

## Test fixtures

`engine/tests/data/gltf_tools/make_fixtures.py` (stdlib Python, deterministic) generates
fixtures that imitate both exporters (`valid/`) and the three rejected cases (`rejected/`);
`assets.json` there is the strict-checked registry (ctest `tools.asset_check_gltf_tools`,
`tools.gltf_fixtures_up_to_date`). Real exports go to `engine/tests/data/gltf_tools/real/`
as `blockbench_*.glb` / `blender_*.glb`; the unit case "real exports load" checks every file
found there.
