# Voxels: hybrid voxel + model world (architecture, Phase V)

Status: **approved architecture (V0, 2026-11-01), not implemented yet.** This guide is the
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

- **Base cell:** configurable edge length (`VoxelGridSettings::cellSize`). Lastlight Expanse:
  **0.5 m**. The engine has no built-in unit; Highlands keeps its 1 m cells (§12).
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
  geometry (stair risers of 0.25 m, wedges, curves, thin panels).

## 3. Block registry and shape library (engine)

Extends the existing `VoxelPalette` *(exists: id → colour, optional texture layers)* into a
**block registry**; no second id system.

- `BlockState` (per id): `material` (index into the block material table), `shape` (index into
  the shape library), `rotation` (4 yaw steps; plus flip for top/bottom variants), flags
  (e.g. `noCollision`, `transparent` reserved).
- `BlockMaterial`: texture layers per face group (top, side, bottom), PBR factors
  (roughness, metallic, emissive), palette-checked source textures.
- `ShapeTemplate` (one per shape; extensible by registration, no core changes):
  - mesh geometry in cell-local space (triangles, normals, UVs derived from world position);
  - **face-occlusion info**: for each of the 6 faces, whether the shape covers it fully
    (`full`), partly (`partial` + 2D coverage mask for later use) or not at all;
  - **collision definition**: a list of cell-local boxes (exact for boxy shapes, a stepped
    approximation for curves and slopes, documented per shape);
  - fill fraction (for LOD) and AO participation.
- Built-in engine shapes (generic, game-neutral): `cube` (id 0, the greedy path), `slab_low`,
  `slab_high`, `stair` (two 0.25-cell risers), `wedge` (slope), `corner_inner`,
  `corner_outer`, `quarter_round` (convex arch piece), `quarter_round_inner` (concave),
  `panel_thin` (1/8-cell plate, 4 orientations). Games register more.
- Data format: `*.blocks.json` (game content: materials and block states by name), loaded by
  the engine, registered in `assets.json` (new asset type, dependencies = textures) and
  validated by the asset checks (sizes, palette, density). Block names map to stable ids
  saved with regions (§7) so ids can be re-assigned without corrupting saves.

## 4. Meshing *(extends ChunkMesher)*

- Meshing chunk: **32³ cells** *(exists: `kChunkSize` = 32)* = 16 × 16 × 16 m at 0.5 m.
- Cube cells use the existing greedy mesher with per-vertex AO *(exists)*. For palettes that
  only contain cube shapes the output must stay **byte-identical**; a regression test hashes
  the mesher output for fixed inputs (Highlands and test chunks).
- Shape cells emit their template (rotated), are never merged greedily, and get AO from
  their neighbourhood (approximation: corner samples of the cell).
- Face culling: a face is skipped when the neighbour's touching face is `full`. Faces next to
  `partial` faces are emitted (correct before clever).
- UVs: world-space, **1 texture tile per metre** (`uvPerMetre`, new mesh option). At 64×64
  textures that is 64 texels/m; a Sub-block face shows a 32×32 part of the tile. Today the
  mesher writes UVs in voxel units *(exists)*; the option defaults to that behaviour.
- Output stays `MeshData` until V6 introduces a packed voxel vertex (§9).

## 5. LOD and cracks *(extends existing LOD + sealing)*

- Existing: cells of 2^lod voxels, majority rule, boundary sealing between LODs *(exists)*.
- Shapes count by fill fraction in the majority rule.
- **Conservative LOD:** chunks that contain authored structures (or any cell flagged
  `structural`) never drop below LOD 0 inside the interior range, so thin walls (0.5 m) never
  disappear or open up. Terrain-only chunks keep distance LOD.
- No visible cracks: existing sealing between LODs, plus chunk-boundary tests with shapes.

## 6. Collision and raycasts *(extends VoxelWorld::raycast)*

- Collision is queried from the grid, not from generated meshes:
  `VoxelWorld::collectSolidBoxes(aabb, out)` returns world boxes for cube cells (merged along
  rows) and the template boxes of shape cells.
- `raycast` becomes shape-aware (cube cells as today; shape cells test their boxes).
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
- Required renderer track (`docs/AI_FIRST_ROADMAP.md`): S5 shadows, S7 AO beyond voxel AO,
  S8 volumetric fog, weather; needed for the target look before D7 where possible.
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
| Configurable grid (`cellSize`), block registry, shape library, `*.blocks.json` loader | Block catalogue (`game/assets/blocks`), palette |
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
