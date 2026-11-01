# Voxels: hybrid voxel + model world (architecture, Phase V)

Status: **approved architecture (V0, 2026-11-01); V1 implemented (block registry, shape
library, mesher shapes; §2–5 marked *(exists)*); V2 implemented (`*.blocks.json`, block
catalogue, texture standards, `zodar/voxel`; §3/§9); V3–V6 not started.** This guide is the
binding specification for Phase V (V1–V6, `docs/PHASE_D_PLAN.md`). Sections marked
*(exists)* describe current code; everything else is the target. Update this file when a
V-session implements or changes a part, so later sessions read the guide instead of the code.

Authoritative game rules: GDD §2 and §3 (Amendment 1). Art rules: `docs/ART_DIRECTION.md`.
Imported-model pipeline: `docs/guide/MODELING.md`, `docs/guide/ASSETS.md`.

## 1. Hybrid model

Voxel blocks: architecture and structural environment. Imported 3D models: weapons,
characters, mutants, furniture, generators, machinery, computers, vehicles, equipment and
decorative props.

| Voxel environment (blocks) | Imported models (glTF, D1 pipeline) |
| --- | --- |
| Walls, floors, ceilings, buildings, structural architecture | Weapons, characters, mutants |
| Roads, terrain, rocks, landscape | Furniture, generators, machinery |
| Bunkers, underground facilities, large structures | Computers, terminals, vehicles |
| Stairs, slopes, corners, arches, thin panels as block shapes | Doors, blast-door frames, lights, signs, vents, cable trays, equipment, decoration |

Both use the same materials standard (palette, texel density, PBR values), the same lighting
(shadows, AO, fog, volumetrics, weather) and the same renderer. Block construction is
Minecraft-like; the look is not: modern atmospheric lighting on top of crisp pixel-art
surfaces.

## 2. Grid, cells and blocks

- **Base cell:** configurable edge length in world units *(exists: `StreamingSettings::voxelSize`,
  copied into `MeshOptions::voxelSize`; the name stays `voxelSize` in code, V1 decision)*.
  Lastlight Expanse: **0.5 m**. The engine has no built-in unit; Highlands keeps its 1 m
  cells (§12).
- A cell stores one 16-bit **block-state id** (`zodar::voxel::Voxel`, 0 = empty) *(exists:
  16-bit ids)*.
- Named block sizes (Lastlight, 0.5 m cells):

| Name | Size | Cells |
| --- | --- | --- |
| Full Block | 1 × 1 × 1 m | 2 × 2 × 2 |
| Half Block / Slab | 1 × 1 × 0.5 m (half height) | 2 × 2 × 1 |
| Vertical Half Block | 0.5 × 1 × 1 m | 1 × 2 × 2 |
| Sub-block | 0.5 × 0.5 × 0.5 m (1/8 of a Full Block) | 1 |

  The 0.5 m cube is a **Sub-block**, never a "quarter block". These names are authoring
  terms: the engine stores cells; a Full Block is eight cube cells of the same block state.
- The base cell is the resolution, not a limit on form: **shapes** (§3) give a cell finer
  geometry (stair risers of 0.25 m = half a cell, wedges, curves, thin panels).

## 3. Block registry and shape library (engine)

Extends the existing `VoxelPalette` into a **block registry**; no second id system.
*(exists, V1: `engine/scene/Scene/Voxel/BlockRegistry.h`, built-ins in `Shapes.cpp`)*

- `VoxelPalette` *(exists)*: `colors` and `faceLayers` are the material table (indexed by
  material); `blocks` (per id) and `shapes` (shared `ShapeLibrary`). **Legacy mode:** empty
  `blocks` = every non-empty id is a cube with material = id (Highlands, unchanged). Registry
  mode: ids beyond `blocks` (unknown blocks) are cubes with the explicit fallback material
  `VoxelPalette::kFallbackMaterial` (magenta, texture layer 0), never a real material (V2).
- `BlockState` (per id) *(exists)*: `material`, `shape` (index into the shape library),
  `rotation` (**4 yaw steps only**, 90° right-handed about +Y: one step turns +X into −Z;
  flip reserved, V1 decision), flags (`kNoCollision`; `kTransparent` reserved, not meshed).
- `BlockTable` *(exists)*: immutable per-id lookup built once from a palette (validates
  material/shape/rotation, resolves rotated geometry, full-face bits, fill, AO occluder);
  `VoxelWorld` builds one and shares it with mesh jobs.
- `BlockMaterial`: texture layers per face group (top, side, bottom), PBR factors
  (roughness, metallic, emissive), palette-checked source textures.
- `ShapeTemplate` (one per shape; extensible by `ShapeLibrary::add`, no core changes)
  *(exists)*:
  - geometry: planar polygons in cell-local space [0, 1]³, counter-clockwise seen from
    outside, star-shaped from the first corner (fan triangulation); normals are computed;
    UVs are derived from position by the mesher;
  - **face-occlusion info** (derived): per face `Full`, `Partial` (+ 4 × 4 coverage mask for
    later use) or `None`, from the area of the polygons lying on that face;
  - **collision definition**: a list of cell-local boxes (exact for boxy shapes, a stepped
    approximation for curves and slopes; used from V3);
  - fill fraction (derived from the enclosed volume, in eighths, for LOD) and an explicit
    `aoOccluder` flag (V1 decision).
  - Validation (`zodar::Error`): corners inside the cell, planar non-degenerate polygons,
    star-shaped from the first corner (no reversed fan triangle (relative test on the fan
    angle's sine), the fan sweeps less than a full turn; V2), boundary polygons facing outward,
    no overlap on a face (area sum ≤ 1 and no 32 × 32 sample point strictly inside two
    polygons, so overlaps balanced by gaps fail; V2; known limit: an overlapping sliver
    thinner than about 1/32 cell between sample points is not detected), closed and outward surface
    (volume independent of the reference point), valid boxes, unique name.
- Built-in engine shapes (generic, game-neutral) *(exists)*. Rotation 0: slopes and steps rise
  toward +Z, curved pieces bend about the X axis.

| Shape | Full faces (rotation 0) | Fill | `aoOccluder` |
| --- | --- | --- | --- |
| `cube` (index 0, the greedy path) | all | 8/8 | yes |
| `slab_low` / `slab_high` | −Y / +Y | 4/8 | no |
| `stair` (two risers of half a cell = 0.25 m at 0.5 m cells) | −Y, +Z | 6/8 | yes |
| `wedge` (slope) | −Y, +Z | 4/8 | no |
| `corner_inner` (concave slope corner) | −Y, +X, +Z | 5/8 | no |
| `corner_outer` (convex slope corner) | −Y | 3/8 | no |
| `quarter_round` (convex arch piece, 4 segments) | −Y, −Z | 6/8 | yes |
| `quarter_round_inner` (concave arch piece) | +Y, +Z | 2/8 | no |
| `panel_thin` (1/8-cell plate on −Z; 4 orientations by rotation) | −Z | 1/8 | no |

  Games register more.
- Data format: `*.blocks.json` (game content: materials and block states by name), loaded by
  the engine, registered in `assets.json` (new asset type, dependencies = textures) and
  validated by the asset checks (sizes, palette, density). Block names map to stable ids
  saved with regions (§7) so ids can be re-assigned without corrupting saves.
  *(exists, V2: `engine/rendering/SceneFile/SceneFile/BlockFile.h`)*

```json
{ "schema": "zodar.blocks", "version": 1, "uvPerMetre": 1,
  "materials": [
    { "name": "concrete", "textures": "textures/concrete.png", "roughness": 0.9 },
    { "name": "panel", "textures": { "top": "textures/panel_top.png", "all": "textures/panel.png" },
      "tint": [1, 1, 1], "metallic": 0.6, "roughness": 0.4, "emissive": 2 } ],
  "blocks": [
    { "name": "concrete", "material": "concrete" },
    { "name": "stair", "material": "concrete", "shape": "stair", "rotations": 4 },
    { "name": "grate", "material": "panel", "shape": "slab_low", "collision": false } ] }
```

  - Materials: `textures` = one path for every face, or `top`/`side`/`bottom` (`all` fills the
    missing ones); paths relative to the file. `tint` (RGB, 0..1, default white), `roughness`
    (0..1, default 0.8), `metallic` (0..1, default 0), `emissive` (≥ 0, default 0: strength of
    texels with alpha < 1). Material index = file order (`VoxelPalette::colors`/`faceLayers`).
  - Blocks: `material`, `shape` (shape-library name, default `cube`; custom shapes are
    registered in code with `ShapeLibrary::add`, not in JSON (V2 decision)), `rotations` 1 or
    4, `collision` (default true; false = `kNoCollision`).
  - `BlockCatalogue` (`buildBlockCatalogue` / `loadBlockCatalogue`): registry palette, names,
    texture-array layers (first-use order, deduplicated by texture + PBR parameters, so a
    texture reused with other parameters gets its own layer), per-layer parameters and the
    JSON path of each layer's first use. All errors are `DataError`s with JSON paths.
  - **Texture-array dimensions:** all layers of one catalogue are square and the same size;
    `checkBlockLayerSizes` names each offending layer, e.g. `/materials/1/textures/side: 32x32
    (rust.png) differs from the texture array size 64x64 (first layer:
    /materials/0/textures/top)`.
- **Block id stability (V2 rules):**
  - At most **65534 ids** (incl. rotation variants): id 0xFFFF always stays outside the
    registry, so `BlockTable` keeps an out-of-range fallback cube (e.g. for bedrock when no
    block is a cube); `BlockTable` rejects larger registry palettes.
  - `buildBlockCatalogue` validates data built in code as well (names, rotations 1/4, id
    limit, parameter ranges incl. NaN, empty textures) with JSON paths.
  - Ids follow the file order: id 0 is empty, the first definition is id 1. `"rotations": 4`
    expands into four consecutive ids named `name`, `name@1`, `name@2`, `name@3` (yaw steps
    0..3); `@` is reserved in names. Loading the same file always gives the same ids.
  - Reordering, inserting or removing a definition changes every later id. Anything that
    stores raw ids (chunk data, saves, test fixtures) is only valid for an unchanged file, so
    **block files are append-only** until regions save names.
  - Ids beyond the registry load as the fallback material (magenta), never as another block.
  - V4 (`*.zregion`, §7) saves the block **names** (palette) with each region and maps them to
    the current ids on load; the `name@N` names are part of that contract. V2 provides no save
    migration and no saved data uses block ids yet.

## 4. Meshing *(extends ChunkMesher)*

- Meshing chunk: **32³ cells** *(exists: `kChunkSize` = 32)* = 16 × 16 × 16 m at 0.5 m.
- Cube cells use the existing greedy mesher with per-vertex AO *(exists)*; the merge key is
  the material (rotated cube states merge). For palettes that only contain cube shapes the
  output stays **byte-identical** *(exists: golden hashes in
  `engine/tests/Scene/VoxelRegressionTests.cpp` and `game/tests/HighlandsMeshTests.cpp`)*.
- Shape cells (LOD 0) emit their rotated template, are never merged greedily *(exists)*.
  AO approximation: each polygon is treated like a cube face along its dominant normal axis;
  each corner samples the three neighbours of the cell in front of it toward the nearest
  cell corner. Only blocks with `aoOccluder` darken neighbours.
- Face culling *(exists)*: a face or boundary polygon is skipped when the neighbour's
  touching face is `Full`. Faces next to `Partial` faces are emitted (correct before clever).
- UVs: world-space **tiles per metre** (`MeshOptions::uvPerMetre`) *(exists)*; Lastlight sets 1
  (V2/V5). At 64×64 textures that is 64 texels/m; a Sub-block face shows a 32×32 part of the
  tile. Default 0 = legacy voxel units.
- Sloped polygons *(exists)*: UVs are a **planar projection** along the dominant axis of the
  polygon normal, and that axis also picks the face layer (Y = top/bottom, X/Z = side).
  **Tie rule:** the largest absolute normal component wins; ties prefer Y, then X (a 45° wedge
  slope is a top face). Consequence: along a 45° slope the texture is stretched by √2 (the
  projected tile covers √2 m of surface per metre). Accepted for V1/V2; revisit with a
  slope-aware projection only if art needs it (tested in `VoxelShapeMeshTests.cpp`).
- Output stays `MeshData` until V6 introduces a packed voxel vertex (§9).

## 5. LOD and cracks *(extends existing LOD + sealing)*

- Existing: cells of 2^lod voxels, majority rule, boundary sealing between LODs *(exists)*.
- Shapes count by fill fraction in the majority rule *(exists)*. At LOD > 0 every solid cell
  is meshed as a cube of its material (temporary, V1 decision; conservative LOD in V3).
- **Conservative LOD:** chunks that contain authored structures (or any cell flagged
  `structural`) never drop below LOD 0 inside the interior range, so thin walls (0.5 m) never
  disappear or open up. Terrain-only chunks keep distance LOD.
- No visible cracks: existing sealing between LODs, plus chunk-boundary tests with shapes.

## 6. Collision and raycasts *(extends VoxelWorld::raycast)*

- Collision is queried from the grid, not from generated meshes:
  `VoxelWorld::collectSolidBoxes(aabb, out)` returns world boxes for cube cells (merged along
  rows) and the template boxes of shape cells.
- `raycast` becomes shape-aware (cube cells as today; shape cells test their boxes). Until V3
  it treats every non-empty cell as a full cell *(exists)*.
- Lastlight composes this with imported-model colliders behind its `CollisionWorld` interface
  (`game/src/Physics`); architecture never needs hand-made collider boxes.
- Phase E (Jolt): per-chunk static compound of merged boxes, rebuilt on edit.

## 7. Chunks, regions and world data

- **Meshing chunk:** 32³ cells (16 m at 0.5 m): edits, meshing, culling.
- **World region:** **64 × 64 m** horizontal (4 × 4 meshing columns at 0.5 m; full world
  height): the unit of streaming, storage, authoring and future multiplayer synchronisation.
  The GDD's 64 × 64 m "chunk" is the region.
- World content is layered (engine composes, game supplies the layers):
  1. base generator (`IVoxelGenerator` *(exists)*): deterministic terrain, roads, rivers;
  2. stamped **structure templates** (§8);
  3. authored edits (hand-made changes, stored per region);
  4. runtime / player edits (persistent world changes; *(exists in memory: `m_edits`)*).
- **Region storage:** one file per region (`*.zregion`): versioned header, block-name table,
  per-chunk palette + RLE (or uniform marker), only chunks that differ from layers 1–2.
  Compressed, deterministic bytes, loaded asynchronously.

## 8. Structure templates

- A structure is a box of cells (palettized, RLE) with an origin, named anchors and optional
  model placements (scene entities referencing registry ids), stored as `*.zstruct`.
- Stamped into generators or regions at grid-aligned positions with 4 yaw rotations.
- Captured from a world box (`captureStructure`) and written by scripts (the bunker in V5).
- Imported models stay scene entities, placed relative to the structure origin.

## 9. Rendering and performance

- Shared voxel material shader in the engine (`zodar/voxel`: texture-array sampling, PBR,
  fog, the same lighting as `zodar/pbr`). Highlands keeps its game shader (§12).
  *(exists, V2: `engine/rendering/Shaders/Engine/voxel_pbr.frag`, registered by
  `MaterialSystem` as `kVoxelShader`; `AssetManager/BlockMaterials`)*
  - Inputs: mesher UVs (`vUv0`, `MeshOptions::uvPerMetre` = the catalogue's), vertex colour
    rgb = tint × AO, alpha = texture-array layer. Material `custom0` = texture array, sampler,
    layer table, table size; `custom1.x` = a nearest sampler used only for the table reads
    (never a filtering sampler on RGBA32F) (indices/sizes as uint bits); `baseColorFactor`
    multiplies the albedo.
  - **Bounds:** the shader checks the layer against the table size before the table read
    (in float, so NaN/negative/too large fail) and uses fallback parameters (roughness 1,
    metallic 0, emissive 0) outside it; it does not rely on `robustImageAccess`. The texture
    sample clamps its layer as Vulkan defines for array layers. Tested with a mesh whose
    layers lie outside the table.
  - Per-layer parameters (roughness, metallic, emissive) live in `gfx::Vec4Table`: an N × 1
    `R32G32B32A32_SFLOAT` image read with `texelFetch` (`zodarFetch`), so values are exact
    float32. **Precision decision (tested):** RGBA8 would store roughness 0.3 as 0.302 and
    cannot hold emissive > 1; RGBA16F stores 0.3 as 0.2998. The format's sampled-image support
    is checked at creation (mandatory in Vulkan).
  - Emissive: texels with alpha < 1 emit `texel.rgb × (1 − alpha) × emissive` (untinted; the
    Lastlight terrain convention).
  - `BlockMaterials(context, upload, scene, catalogue)` checks the layer count against the
    device's `maxImageArrayLayers` (DataError at `/materials`, before any texture is loaded),
    loads the layer textures (sRGB, mips, pixel-art sampler), rejects mismatched layer sizes
    with the layer's JSON path, uploads the texture array and the table and creates the
    material; destruction retires them through the deletion queue. It must be destroyed before
    its `SceneRenderer` (same contract as `AssetManager`).
  - GPU resource order: `Texture`, `TextureArray` and `Vec4Table` register their bindless slot
    before recording uploads, so a full bindless table throws before any copy into the image
    is pending (tested). Partial uploads: `TextureArray` queues one upload per layer (copy +
    layout transition referencing the `VkImage`, replayed at submit). It now validates every
    layer's byte sizes before registering or queuing anything, and if a later layer still fails
    (e.g. staging allocation), it releases the slot and hands the image to the batch
    (`UploadBatch::keepAlive(GpuImage&&)`), which destroys it only after the batch's work has
    finished. Regression test with injected staging failure after the first layer
    (`UploadBatch::failStagingAfterForTesting`, `TextureTests.cpp`); the old code produced
    `VUID-VkImageMemoryBarrier2-image-parameter` and a crash at submit.
  - Tests (`engine/tests/Gpu/VoxelMaterialTests.cpp`, lavapipe): table read back bit-exact;
    emissive blocks byte-identical to `zodar/unlit`; lit and fogged blocks identical to
    `zodar/pbr` on the same surface (tolerance 1 for other drivers); out-of-range layers use
    the fallback parameters; a full bindless table and catalogues above `maxImageArrayLayers`
    fail cleanly.
- Required renderer track (`docs/AI_FIRST_ROADMAP.md`): S5 shadows, S7 AO beyond voxel AO,
  S8 volumetric fog, weather; scheduled after D8, before the bunker's final presentation and
  sign-off (approved placement, `docs/WORLD_PRODUCTION_ROADMAP.md` §2).
- Planned optimisations, **only after benchmarks show the need** (V6):

| Bottleneck | Optimisation |
| --- | --- |
| 64-byte `Vertex` for voxel meshes | Packed voxel vertex (8–12 bytes) |
| CPU draw path (`StreamedMeshSet`, `ModelRenderer`) | GPU-driven chunk rendering (`GeometryPool` with free/reuse), model/prop instancing |
| Memory at 0.5 m (8× the cells of 1 m) | Uniform chunks *(exists)*, per-chunk palettes (4/8-bit indices), streaming radius, diff-only storage |
| Region I/O | Compressed region files, async loads |
| Shapes break greedy merges | Triangle budget per chunk, shape budget per structure |
| Generation / meshing latency | Async jobs *(exists)*, nearest-first scheduling *(exists)* |

- Benchmark: ZodarBench scenario at 4 × 4 km region scale (V6), lavapipe interleaved runs
  for regressions plus real-GPU numbers.

## 10. Engine / game split

| ZODAR Engine (generic) | Lastlight Expanse |
| --- | --- |
| Configurable grid (`voxelSize`), block registry, shape library, `*.blocks.json` loader | Block catalogue (`game/assets/blocks`), palette |
| Meshing, AO, conservative LOD, sealing | Which chunks are structural (via flags/structures) |
| Collision queries, shape-aware raycasts | `CollisionWorld` composition, player controller (D7) |
| Structure templates, region storage, layered generators | Terrain/road/river generators, bunker and building layouts |
| Voxel shader, texture arrays, material infrastructure | Look parameters (fog, light colours) |
| Asset import + validation (configurable texture standards per game) | Its texture standard and budgets (`assets.json`) |

No Lastlight names or rules in `engine/` (CLAUDE.md).

## 11. World generation (Lastlight)

The world is the handcrafted 4 × 4 km map of the GDD. Procedural generation is an
**authoring tool**: deterministic terrain, roads, rivers and stamped structures produce a
base that designers edit by hand; the result is saved as region data and shipped. It is not
an endless or randomly generated runtime world.

## 12. Highlands regression

Highlands stays the 1 m-cell regression scene with its own generator, 256² legacy textures
and `lastlight/voxel` shader. It is not migrated. Every V-session keeps its screenshot
byte-identical in Debug and Release (sha256 `1e6b461a…` at 640×360, 90 frames, lavapipe) and
the cube-only mesher hash test green.

## 13. Bunker on voxels (V5)

- Walls, floors, ceilings, pillars, beams, stairs and openings become voxel geometry
  (0.5 m walls, 4.0 m ceiling, 4 m corridor, existing room layout and areas).
- Doors, blast-door frames, lights, signs, vents, cable trays, furniture, generators and
  terminals stay glTF models.
- Architecture collision comes from voxel queries; `colliders.json` keeps imported-model boxes only
  until `Collider` components exist (D6/D7).
- The exploration mode and the walkthrough test run on the voxel bunker.
