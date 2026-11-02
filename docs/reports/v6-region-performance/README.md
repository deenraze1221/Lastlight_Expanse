# V6: region-scale performance

Baseline: main `ee3f952`, with the region scenario and timing instrumentation added, before
changing `ChunkMesher.cpp`. Candidate: this branch's exact partial-face culling. All six
raw ZodarBench reports are retained here; summary values are medians of three runs.

## Workload and reproduction

Build Release with `ZODAR_GPU_TESTS=ON`. Run baseline then candidate three times, interleaved,
with no builds or other benchmarks running. Use a distinct output name for every run:

```sh
ZodarBench --scene region --headless --no-validation --warmup 0 --frames 544 \
  --width 320 --height 180 --output candidate-1.json --report candidate-1-run.json \
  --screenshot candidate-1.png
```

The benchmark refuses an existing nonempty `<output>.regions` directory rather than reusing
stale edits. Reports and region files remain available for inspection. The baseline binary
uses the same scenario and telemetry but the base revision's mesher. All other sources,
compiler settings, driver, resolution and command-line options are identical.

The deterministic route visits a 4 x 4 lattice spanning 4000 x 4000 meters, then returns to
the first stop. Each stop lasts 32 updates; 544 measured frames plus two statistics-drain
frames cover the route. This is a region traversal stress test with discontinuous camera
movement, not a continuous walk. Terrain is generated on demand, never allocated for the
whole envelope. Configuration: 1-meter cells, one chunk Y layer, radius two chunks,
16 jobs/update, 256 MiB world CPU budget, 256 MiB region budget, 128 MiB GPU/staging budget.
Walls and slabs carry the structural flag and retain LOD 0 nearby. Sixty-four moving-window
props share a mesh/material. Seventeen edit attempts produce sixteen saved edits. Return
visits verify explicit air survived unloading and reload. Jobs run inline for repeatability;
this does not measure asynchronous worker throughput. Graph and reference paths work;
`--path gpu` is rejected for this scenario because streamed buffers are not GeometryPool
allocations. Other scenarios retain all three paths.

Frame distributions, draws, visible objects, culling and GPU passes use ZodarBench's existing
measured-frame accounting. Region distributions and totals include every update, including
warmup and drain frames: generation and meshing completed-job milliseconds, update time
(including region I/O), upload submission time, uploaded payload bytes, peak CPU reservations,
GPU/live/deferred/staging bytes, residency, unsettled update count and final in-flight backlog.
Unsettled updates measure how often streaming has outstanding demand, not the number of
unscheduled chunks. Memory figures are engine accounting, not process RSS or VRAM telemetry.

## Evidence and chosen optimization

Debian 13, GCC 14.2, CMake 3.31.6, Release, Mesa 25.0.7 llvmpipe (LLVM 19.1.7), Vulkan 1.4.
No hardware GPU was available. Software timing changes around five percent are noise;
payload and geometry reductions are deterministic.

| Metric | Baseline | Candidate |
| --- | ---: | ---: |
| CPU frame mean, ms | 19.278 | 18.164 |
| CPU frame p95, ms | 40.820 | 41.589 |
| GPU frame mean, ms | 16.847 | 15.912 |
| GPU frame p95, ms | 37.561 | 35.916 |
| Scene pass mean, ms | 9.215 | 8.627 |
| Depth prepass mean, ms | 3.997 | 2.943 |
| Total meshing, ms | 300.540 | 256.479 |
| Upload submit mean, ms | 0.080 | 0.078 |
| Uploaded bytes | 98,062,720 | 43,756,160 |
| Peak GPU + staging bytes | 11,835,040 | 5,280,800 |
| Historical post-update CPU sample, bytes | 134,227,647 | 134,227,647 |
| Typical visible triangles (p50) | 27,834 | 12,786 |
| Typical draw calls (p50) | 10 | 10 |
| Typical visible / culled objects (p50) | 65 / 10 | 65 / 10 |
| Peak chunks / regions | 39 / 25 | 39 / 25 |
| Unsettled updates / final backlog | 278 / 0 | 278 / 0 |

Rendering dominated frame time; the Scene and depth passes were the largest GPU costs.
Repeated slabs emitted invisible internal partial faces. The candidate removes a boundary
polygon only when an identical adjacent block has an exactly matching opposite polygon
(reverse cyclic vertices, exact coordinates). It caches matches per polygon per mesh.
Unequal shapes, different block states, non-boundary faces, sealing and cube meshing retain
their existing paths. No vertex-format, renderer, Hi-Z, binary greedy or mesh-shader change.

Timing gains are inconclusive. Ratios of separate medians suggest 5.8% CPU and 5.5% GPU
reductions, but two of three paired runs regress: CPU changes are -48.6%, +16.6%, +18.0%;
GPU changes are -50.1%, +20.6%, +16.3%. This variation prevents a reliable timing speedup
claim. CPU p95 also rises 1.9%. These software timings do not establish hardware performance. Uploaded payload
and peak GPU/staging accounting fell 55.4%; typical triangles fell 54.1%.

All six region captures were byte-identical:
`8b29431f66482a317643ef7cc19485fc5feebfa1d8c058c457407e49c9d36522`.
Base Highlands golden for this driver:
`5ddd23b4cf86dfd551dfad44e349fcefb2594f31a2c37b72f6d9c00f68045776`.

Remaining limits: no hardware GPU, Windows/MSVC, 0.5-meter stress run, continuous camera
path, asynchronous throughput measurement, or full-region populated residency claim.
D7/V7 and unrelated rendering features are outside this change.

## Validation

Final `tools/gate.sh`: Debug 39/39 and Release 39/39 CTest entries passed, warnings as errors,
Vulkan GPU tests enabled. Both 90-frame Highlands runs matched the base hash above with
zero validation errors and warnings. The unit runner retains its two existing skipped cases.
The new region regression runs the full 544-frame route with validation enabled and checks
persisted edits, reloads, residency bounds, nonzero telemetry and settled final backlog.

The first full gate found the expected shape-topology hash changes: streamed LOD meshes
`0x1823C9036434EA3A` became `0x3D6335241A09076E`; catalogue meshes
`0x2444B9C0D80D2BD6` became `0xEC2FF97EB5257032`. These hashes include removed internal
polygons, so their expectations were explicitly updated with the previous values retained
in comments. New face-count, volume, unequal-slab and sealed cross-chunk tests explain the
change. Existing LOD selection, seam rays (zero misses), threaded determinism, cube/query
regressions and pixel goldens remain checked. Seventeen focused topology cases (13,700
assertions) passed before rerunning the full gate. The initial gate failure history was
retained at `/workspace/v6-gate/gate-failures.log`; no unchanged failing run was retried.

The full region route also passed on `--path reference` with Vulkan validation enabled
(zero errors/warnings); its capture matched the graph baseline/candidate hash above.


## Audit corrections

The six original reports above are retained unchanged as historical evidence. Their
`peakCpuBytes` sampled post-update residency and missed temporary inline reservations;
`sceneDetails.uniqueMeshes` incorrectly counted only the shared prop mesh.
New reports use the world's lifetime accounted-memory high-water for `peakCpuBytes`,
including generation/meshing reservations sampled before execution and data/events before
consumption. `peakRegionCpuBytes` separately includes region request scratch reservations.
These component peaks can occur at different times and must not be added as a simultaneous
peak. Accounting is conservative engine bookkeeping, not measured RSS. Unique mesh counts
now include drawable streamed meshes plus the shared prop mesh.
Regression coverage includes inline/threaded high-water retention, report mesh counts,
and adjacent built-in shapes at all four rotations across all six chunk boundary directions,
with sealed-boundary and volume checks. Rendering, collision and existing goldens are unchanged.

`audit-fix-validation.json` records the corrected counters: world peak 231,741,248 bytes,
region peak 134,225,883 bytes, and 13 unique drawable meshes. Uploaded bytes remain
43,756,160 and the region screenshot retains the original pixel hash. Validation reported
zero errors/warnings. This run overlapped compilation and enabled validation; its timings
must not be used for performance comparisons.

Audit-fix verification: focused 13 cases / 14,082 assertions and full-route report regression
passed. Full `tools/gate.sh` passed Debug 39/39 and Release 39/39 with validation and warnings
as errors. Highlands and region pixel hashes, topology goldens and collision checks remain
unchanged; no validation errors/warnings. Existing skipped unit cases remain skipped.
