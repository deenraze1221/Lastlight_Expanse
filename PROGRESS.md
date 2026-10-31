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

## Engine audit: open items (from SECTOR-ENGINE docs)
- Renderer S4 awaiting RTX benchmarks; S5 shadows, S6 post (tonemap, TAA), S7 AO/GI, S8 volumetrics/fog, S9 HZB/meshlets planned.
- Asset streaming designed (docs/ASSET_STREAMING.md), not implemented. MSVC/Windows build unverified here.
- GeometryPool cannot free meshes; streamed terrain uses the CPU draw path.

## Current task
- Visual style: Minecraft-inspired voxel look with pixel-art texture arrays (256² standard, 512² hero),
  nearest sampling, dynamic lighting, shadows, fog, weather.

## Next steps
1. Texture-array support in renderer (2D array images + bindless array slot) and a voxel material shader
   (per-face layer index, nearest + mips, crisp texels).
2. Voxel vertex data: texture layer per face; world-space UVs already in place.
3. Fog (height + distance) in voxel shader; S5 shadows (CSM) for the sun.
4. Weather (rain/snow particles), day-night cycle.
5. GeometryPool free-list so terrain can use GPU-driven rendering.
