# S4 Advanced Lighting — implementation report

Status: **implemented and verified on the cloud machine (llvmpipe software rasterizer).
No RTX 4070 measurements exist yet** — GPU timings below are not GPU performance; only
correctness, counts and CPU timings are meaningful from that environment.

## Scope

| Roadmap / request item | Implementation | State |
| --- | --- | --- |
| Metallic-roughness PBR, Cook-Torrance, Fresnel-Schlick | GGX D, height-correlated Smith visibility, Schlick F, Lambert diffuse (`zodar/lighting.glsl`) | Done (existed; restructured) |
| Energy-conserving specular, multi-scatter GGX | Kulla-Conty / Filament energy compensation `1 + f0 (1/dfg.y - 1)` from a baked DFG LUT, applied to direct and image-based specular; IBL diffuse scaled by `1 - E(f0)` | Done, white-furnace tested |
| Physically correct attenuation | Inverse square with Karis' smooth window to the light range; automatic ranges from an irradiance cutoff | Done |
| Spot lights | Inner/outer cone with Filament's angular falloff | Done |
| GPU-driven Clustered Forward+ | Exponential depth slices, compute cull (sphere/AABB, cone/sphere), fixed-capacity 16-bit per-cluster lists in ascending light order, device-local light buffer, GPU statistics | Done |
| IBL | HDR equirect (.hdr) or procedural sky → GPU bake: radiance cube + mips, cosine irradiance cube, GGX-prefiltered cube (filtered importance sampling), multi-scatter DFG LUT; skybox | Done |
| Depth prepass | Opaque/masked depth-only pass, invariant positions, shading pass keeps depth test GREATER_OR_EQUAL | Done (depth only) |
| Depth prepass outputs (normals/roughness/velocity) | — | **Not done** (no consumer before S6 TAA / S7 GTAO-SSR) |
| Emissive | glTF emissive + KHR_materials_emissive_strength (existing) | Done (existing) |
| Quality settings | `LightingSettings` (Low/Medium/High/Ultra cluster presets, prepass, multiscatter, cutoff, far), `iblSettings(quality)` | Done |
| Debug visualisation | Light count heat map (magenta = overflowing cluster), depth slices, normals, direct, indirect, specular IBL | Done |
| GPU profiling | Graph passes `Lighting.Upload`, `Lighting.ClusterCull`, `Lighting.Stats`, `Scene.DepthPrepass`, `Scene`; cluster counters read back without stalls; IBL bake time; overlay lines | Done |
| Benchmarks | ZodarBench `lights` scene, `--lighting-quality`, `--brute-force-lights`, `--no-prepass`, `--environment`, `--no-multiscatter`; JSON `lighting` section | Done (cloud only) |

## Architecture (for S5–S8 without a rewrite)

```
SceneRenderer::addToGraph
  GpuScene.Update / CullScan / CullBlocks / CullScatter   (S2)
  Lighting.Upload      lights -> device-local buffer, stats reset
  Lighting.ClusterCull compute: per-cluster light lists
  Lighting.Stats       counters -> per-slot readback
  Scene.DepthPrepass   opaque/masked depth only (optional)
  Scene                opaque -> sky -> blended; materials read frame data + light lists
  Resolve
```

* **Light data** are plain structs (`Lights.h`); `buildGpuLights` converts them (96-byte
  `GpuLight`, world data for shading + view data for culling). Per-light shadow indices,
  IES profiles or area-light parameters fit in the unused fields / a parallel buffer.
* **Shading** goes through `zodarShadePbrDetailed`, which separates direct, diffuse-indirect and
  specular-indirect terms: S5 multiplies the per-light term by a shadow factor; S7 replaces the
  indirect sources (probes, SSR) and multiplies by GTAO; S8 froxel volumetrics reuse the
  cluster grid (`ClusterMath`) and the light buffer.
* **Frame interface** (`zodar/scene.glsl`) exposes light/cluster addresses and IBL slots to
  every material shader, so custom/game shaders can use the same lights.
* **IBL** is an `Environment` object (bindless cube slots); several environments can coexist
  (local probes later), the DFG LUT is global.

## Trade-offs

* **Fixed-capacity cluster lists** (no atomics, deterministic order, byte-identical to brute
  force) cost memory: High = 30×17×32 clusters × 512 × 2 B ≈ 16 MiB at 1080p; tile size scales
  with resolution above 1080p so memory stays constant. Lists that overflow drop lights; the
  overflow is counted, shown in the overlay and the debug view.
* **One thread per cluster, all lights streamed through shared memory**: O(clusters × lights).
  Fine to tens of thousands of lights on desktop GPUs; a coarse per-tile pre-cull or a
  light BVH would reduce it further (not needed for current scenes).
* **Prepass keeps depth writes in the shading pass** (writes the same value): identical
  pipelines with and without prepass, no pipeline variants, GpuScene buckets unchanged. Masked
  materials still `discard` in the shading pass (late Z there).
* **IBL bake is blocking** (load time, graphics queue compute). Bake times on llvmpipe:
  0.7–1.1 s for High; expected tens of ms on an RTX 4070 (not measured).
* **No tone mapping yet** (S6): the resolve clamps; bright IBL/sky saturates to white.

## Evidence (cloud: llvmpipe, Release unless noted)

### Correctness tests (Debug, Release and Clang builds)
ZodarTests 56/56, ZodarGpuTests 44/44 with core + synchronization validation: 0 messages;
ctest 8/8. New S4 tests:

| Test | Result |
| --- | --- |
| Clustered vs brute force, 4 quality presets, reference and graph paths | Byte-identical HDR and output images (0 overflow asserted) |
| GPU cluster statistics, overflow reporting | 360 lights: 256 clusters, max 125/cluster, 0 overflow; 200 overlapping lights on Low: overflow reported |
| Spot cone, inverse-square falloff | Dark outside the cone; 1 m vs 2 m: ratio 4.0 ± 5 % |
| 5000 lights + all debug views | Render without errors |
| Depth prepass on/off | Byte-identical (reference and graph; masked + two-sided geometry) |
| DFG LUT vs CPU reference integration | Within 3 % at 20 texels; independent brute-force integration agrees (roughness 1: 0.308) |
| Uniform environment bake | Radiance, irradiance and every prefiltered mip = input radiance (0.2 %) |
| White furnace (metallic 0/1 × roughness 0.2/0.6/1) | 0.9995–1.001 with multi-scatter; single scattering 0.31 at roughness 1 (metal) |
| Procedural sky lighting/background, environment release | Pass |
| Cluster math, light conversion, HDR decode, procedural sky (unit) | Pass |

### Pixel identity and sync validation (ZodarBench, 640×360, 30 frames)
gpu / graph / reference paths: **identical** for grid, grid 16k, unique, city 32, city 64,
lights (2000 lights), lights + IBL, city 32 + IBL without prepass; 0 sync-validation errors in
all 24 runs. (This found and fixed a write-after-write hazard in the reference path's lighting
barriers that the fence-separated GPU tests could not see.)

### Counts and CPU (cloud CPU; counts are hardware-independent)

| Measurement | Value |
| --- | --- |
| Scene-pass fragment shader invocations, city 64, prepass off → on | 313 060 → 219 690 (−30 %) |
| Same, grid 4096 | 107 699 → 95 322 (−11 %) |
| CPU prepare, 4096 dynamic lights (lights scene) vs grid | 0.216 vs 0.055 ms (~40 ns per light) |
| CPU record, unique, prepass on / off | 0.054–0.059 / 0.049–0.051 ms (S2.1 cloud: 0.042–0.046) |
| Lights scene 1080p, 4096 lights, max lights per cluster (overflow) | Low 463 (78), Medium 339 (8), High 283 (0), Ultra 205 (0) |
| Cluster buffers at 1080p | Low 0.9, Medium 6.4, High 16.4, Ultra 32.5 MiB |
| IBL (High) | 20.0 MiB; bake 0.7–1.1 s on llvmpipe |

### RTX 4070
**Not measured.** The Windows package contains `run_bench.ps1` / `run_lighting.ps1` for:
lights scene per quality, clustered vs brute force, prepass on/off, IBL cost, and the S2.1
baselines (regression check). Expected risks to check there: the recording-time target for
`unique` (≤ 0.05 ms; cloud measurements scale to ≈ 0.035 ms using the S3 cloud/RTX ratio —
an estimate, not a measurement), and the GPU frame-time change of the S2 baselines (prepass adds
a pass; it removes fragment work in overdraw-heavy scenes).

## Known limitations / remaining S4 work

1. Thin G-buffer prepass outputs (normals/roughness/velocity) — deferred until a consumer
   exists (S6 TAA, S7 GTAO/SSR).
2. No RTX 4070 benchmarks yet.
3. Low/Medium presets overflow in extreme light densities (documented, visible in the debug view).
4. IBL bake is synchronous; no async-compute queue (S9).
5. Environment rotation, local reflection probes, SH irradiance: not implemented (S7 scope).
6. Spot lights keep their intensity inside the cone (no flux normalisation by cone angle);
   no photometric units / IES profiles.
7. Masked materials still use `discard` in the shading pass even with the prepass.
8. No tone mapping (S6).
