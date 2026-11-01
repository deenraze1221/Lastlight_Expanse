# Art Direction — Lastlight Expanse and ZODAR games

**Status:** official direction, adopted 2026-10-31. Documentation only. No Blockbench integration
or renderer change has been made for it yet. The "Future technical requirements" section lists
that work. Schedule it through `PROGRESS.md` / `docs/AI_FIRST_ROADMAP.md`, not ad hoc.

This direction applies to Lastlight Expanse. It is the default for future ZODAR games unless a game
documents its own direction. Engine code stays style-neutral: it provides capabilities (sampling
modes, mip control, lighting, shadows, fog, weather). Games choose the look (see
`docs/ARCHITECTURE.md`, "Rendering: capabilities, not styles").

## 1. Visual style

- Minecraft-inspired voxel / pixel-art aesthetic.
- Crisp, readable pixel textures. No unwanted blur: no bilinear magnification on pixel art and no
  texture compression that smears art pixels.
- Modern lighting on top of the retro surfaces: dynamic shadows, ambient occlusion, fog and weather.
- Low-poly and voxel-like geometry with detailed, readable silhouettes. Detail comes from shape and
  texture, not from dense meshes.
- **Hybrid geometry (GDD §3 Amendment 1, 2026-11-01):** the environment (walls, floors, ceilings,
  buildings, architecture, roads, terrain, rocks, bunkers, underground facilities) is built from
  voxel blocks on a 0.5 m base grid (Full Block 1 m, Half Block / Slab, Vertical Half Block,
  Sub-block 0.5 m, plus shapes: stairs, slopes, corners, arches, thin panels). Props and detailed
  assets are imported models (§3). Both share materials, lighting and rendering. Specification:
  `docs/guide/VOXELS.md`.

## 2. Texture standards

**Final standard (GDD §3 Amendment 1, 2026-11-01; replaces the 2026-10-31 "32 default / 64
maximum" table and every earlier proposal).** Enforcement status: the engine checks
(`Assets/TextureStandards.h`, AssetManager, `zodar.py assets check`) still implement the old
32/64 rule until session V2 makes the standard configurable per game and Lastlight's
`assets.json` declares this table; the D2 greybox textures (32×32) are replaced by 64×64 ones
in V5. Textures are never resized automatically; violations are reported.

| Size | Use |
| --- | --- |
| 32×32 | Small or simple details and assets |
| 64×64 | **Standard** for ALL environment textures: voxel blocks, architecture, furniture, props, equipment |
| 128×128 | **Only** characters and weapons |
| 256×256 and above | **Prohibited** unless the project owner explicitly approves an exception |

- Sizes are powers of two and square. Above the size allowed for the asset's class, non-square
  or not a power of two is a violation (warning: re-author the texture; nothing is resized).
- Sizes below 32×32 (8×8, 16×16) are not part of the final table; whether tiny details may use
  them is an open question for the project owner (until decided: treat as violations in new art).
- **Consistent palette.** Each game keeps one master palette. All textures are quantised to it, or
  to documented sub-palettes per biome or faction. Declare them in the game's `assets.json`
  (`"palette"`, `"subPalettes"`); `zodar.py assets check` reports off-palette texels.
- **Consistent texel density.** One art pixel should cover about the same world size on every
  environment asset. Reference: **64 texels per metre**: one 64×64 tile per 1 m block face, so
  one art pixel is 1/64 m (4 texels per Blockbench unit at 16 units/m); a Sub-block face shows a
  32×32 part of the tile (world-space UVs). Environment models stay within 2× (32..128
  texels/m). Characters and weapons (128×128) are exempt when their UV layout needs a different
  density. (The check's current default target is 32; V2 sets Lastlight's target to 64 and
  per-class exemptions.)
- **Consistent pixel-art appearance.** Hard edges, deliberate dithering, no photo sources, no
  upscaled low-res art without cleanup.
- **Sampling.** Use nearest-neighbour magnification and minification on pixel art
  (`gfx::SamplerDesc::pixelArt()`; material files: `"filter": "pixelArt"`).
- **Compression.** Pixel-art textures are never block-compressed: BC7 (and every BCn format) is
  lossy and changes art colours. Compiled pixel-art textures are uncompressed RGBA8 KTX2 (level 0
  byte-identical to the source); the loader rejects a BCn cache for pixel-art textures and
  decodes the PNG instead. Pixel art = glTF sampler with NEAREST magnification (Blockbench), a
  pixelArt/nearest model sampling setting, or `--pixel-art`. BC7 stays available for smooth,
  non-pixel-art textures.
- **Mipmaps.** Configure them carefully: blend between mip levels (linear mip filter) to stop
  distant shimmer, build mips with a filter that keeps the palette readable, and set the anisotropy
  per material. Smooth or non-pixel-art textures (UI gradients, sky, effects) may use linear
  filtering.
- Emissive pixels and transparency follow the conventions of the material that uses them. The
  current terrain convention: alpha < 1 marks emissive pixels (`game/shaders/voxel.frag`).

**Known violations (reported, not resized):** the procedural terrain textures of the Highlands
regression scene (`game/src/Rendering/BlockTextures.h`: 10 layers at 256×256 and 1 hero layer at
512×512) predate this standard. They are generated in code, not loaded through the asset
pipeline, and stay unchanged so the Highlands screenshot regression stays byte-identical
(`docs/guide/VOXELS.md` §12). The D2 bunker greybox textures are 32×32 and are re-made at
64×64 in V5.

## 3. 3D asset workflow

- **Environment geometry is voxels, not models** (GDD §3 A1): walls, floors, ceilings, stairs,
  pillars, beams, openings and terrain are block construction (`docs/guide/VOXELS.md`). Models
  are for props and detailed assets.
- **Blockbench** is the primary tool for voxel/low-poly models (weapons, characters, mutants,
  furniture, equipment, props).
- **Blender** is the secondary tool, for complex models (machinery, vehicles, blast-door frames)
  and organic shapes that are impractical in Blockbench. Blender assets must not drift from the
  voxel style: same texture sizes, palette, nearest sampling and texel density; triangle
  budgets per asset class keep polygon counts in check.
- **One standard for both tools and for voxel blocks** (§2): 64×64 standard (32×32 small
  details, 128×128 characters and weapons only); master palette plus documented sub-palettes;
  64 texels/m for environment assets; nearest sampling with linear mips; no BCn on pixel art;
  triangle budget per asset class. Units: Blockbench 16 units = 1 m, Blender 1 unit = 1 m with
  transforms applied; pivots and footprints snap to the 0.5 m grid where they meet voxel
  geometry.
- **Exchange format: glTF 2.0** (`.glb` preferred, `.gltf` + files accepted), exported from
  either tool. The asset carries meshes, materials, UVs and textures. Later it can also carry
  skins and animations (Phase E). Export settings per tool: `docs/guide/MODELING.md`.
- **Engine/game split:**
  - ZODAR Engine keeps the asset pipeline generic: glTF import, texture compilation, sampler and
    mip rules, validation and reports. Nothing in `engine/` knows about Blockbench or Blender
    project files or Lastlight content; glTF `asset.generator` is read only to report the
    source tool.
  - Lastlight Expanse keeps its game-specific assets, palettes, texel-density targets and content
    rules in `game/` (for example a future `game/assets/`).
- **Pipeline (both tools):** export `.glb` to the game's model folder → `zodar.py import`
  (registry defaults: pixel-art sampling, lossless compile) → `zodar.py assets check --strict`
  (size, palette, texel density, triangle budget, bounds, importer warnings) → prefabs/scenes
  reference the registry id → hot reload in the game. Sources are versioned next to the
  exports so assets can be re-exported: `.bbmodel` (plain JSON) and `.blend` (compressed, ≤ 20 MB) both in regular Git.

## 4. Technical requirements

What exists today (Phase C1 added the asset manager, see `docs/guide/ASSETS.md`):
- `AssetManager` loads PNG textures, glTF models and material files with texture slots;
  `pixelArt` filter preset; texture size warnings (32 default, 64 maximum); lossless pixel-art
  compilation (RGBA8 KTX2, BCn rejected).
- glTF sampler filters are honoured end to end (verified by GPU test on the material sampler).
- `GltfLoader` imports meshes, materials, UVs, textures and glTF samplers. Nearest
  mag/min/mip filters and wrap modes are read into `ModelSampler`. `KHR_texture_transform` is
  accepted.
- `TextureArray` and `zodarSampleArray` exist, with point-filtered sampling and linear mips used
  by the terrain.
- Fog (`SceneLighting::fog`) exists.

Required work, to be scheduled:

1. **glTF import for Blockbench and Blender exports** (engine, generic). *Done in Phase D1*:
   sampler filters end to end, per-face UVs, several textures per model, hierarchy and pivots,
   clashing/space-containing names, Blender PBR materials, vertex colours, tangents and extra
   UV sets; one importer note per ignored feature, actionable errors for Draco, WebP and mesh
   quantization; source tool, triangle budgets and bounds in the asset report. Verified with
   generated fixtures (`engine/tests/data/gltf_tools`); real exports from both tools are the
   open follow-up.
2. **Pixel-art texture rules in the asset pipeline** (engine, generic, configured per game).
   Each texture gets a "pixel art" usage flag that means: no BCn compression (or an
   art-preserving mode, decided by A/B tests), mip generation that keeps the palette readable,
   nearest sampling, and a max-anisotropy setting. Size validation (§2: 8/16/32/64) gives a
   warning, not an error (done at load time in Phase C1; the offline compiler check remains).
3. **Asset validation report** (engine tool). Texture size class, texel density per mesh
   (texels per metre, from UV area against world area), palette deviation against a
   game-supplied palette, and triangle counts. Output is JSON, so agents can check assets
   without looking at them. *Done in Phases C3 and C7* (`zodar.py assets check --report`: size
   class, texel density per model, triangle counts, palette deviation against the master palette
   and sub-palettes in `assets.json`).
4. **Lighting features** (rendering roadmap): S5 dynamic shadows (cascaded sun shadows in
   `pbr.frag` and `voxel.frag`), ambient occlusion beyond the mesher's voxel AO (S7), and
   weather (rain/snow) plus the day–night cycle (backlog).
5. **Game content layout** (Lastlight): `game/assets/` holds `.bbmodel` sources, `.glb`
   exports, the master palette and an asset manifest (Phase C asset registry).
6. **Animation** (Phase E): Blockbench bone animations exported as glTF skins/clips, played by
   the `Animator` component.
7. **Voxel construction** (Phase V, V1–V6, `docs/guide/VOXELS.md`): block registry and shape
   library on a configurable grid (0.5 m for Lastlight), shared voxel material shader, voxel
   collision, structure templates, region storage; configurable texture standards per game
   (V2) to enforce §2.

## 5. Review checklist for new art

- [ ] Size: 64×64 standard; 32×32 for small details; 128×128 only characters/weapons; nothing
      ≥ 256×256 without an approved exception.
- [ ] Compiled losslessly (no BC7 on pixel art).
- [ ] Palette matches the game's master palette.
- [ ] Texel density within the documented range (64 texels/m for environment assets).
- [ ] Footprint and pivot aligned to the 0.5 m grid where the asset meets voxel geometry.
- [ ] Reads crisply up close (nearest sampling) and does not shimmer at distance (mips).
- [ ] Silhouette readable at gameplay distance.
- [ ] Triangle count within the asset class budget; bounds plausible (1/16..64 m, correct
      export scale).
- [ ] Exported with the settings in `docs/guide/MODELING.md` (Blender: no Draco, no WebP,
      transforms applied, "Closest" interpolation).
- [ ] Source (`.bbmodel`, or a compressed `.blend` ≤ 20 MB in regular Git) committed next to
      the `.glb` export.
