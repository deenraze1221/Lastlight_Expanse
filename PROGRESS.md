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

## Engine audit: open items (from SECTOR-ENGINE docs)
- Renderer S4 awaiting RTX benchmarks; S5 shadows, S6 post (tonemap, TAA), S7 AO/GI, S8 volumetrics/fog, S9 HZB/meshlets planned.
- Asset streaming designed (docs/ASSET_STREAMING.md), not implemented. MSVC/Windows build unverified here.
- GeometryPool cannot free meshes; streamed terrain uses the CPU draw path.

## Current task
- AI-first engine: audit done, phased plan in docs/AI_FIRST_ROADMAP.md. Next: Phase A1
  (`zodar::Application` + headless mode, migrate the game).
- Visual style follow-ups (shadows, weather, performance) continue alongside.

## Next steps
1. S5 shadows: cascaded shadow maps for the sun (engine), sampled in pbr.frag and voxel.frag.
2. Weather: rain/snow particles + wet/snow-covered shading, day-night cycle driving sun/sky/fog.
3. Compact voxel vertex (8–16 B packed position/normal/AO/layer) — meshes are 64 B/vertex now
   (108 MB for 0.8 M triangles at radius 6).
4. GeometryPool free-list so terrain can use GPU-driven rendering (multi-draw indirect).
5. Texture streaming/compression: BC7 block textures via AssetPipeline (TextureArray already accepts BCn).
6. Benchmarks on a real GPU (RTX 4070 per roadmap).
