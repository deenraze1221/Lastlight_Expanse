# D8 bunker Release baseline

D8 branch `feat/d8-integration-audit`, based on main `8c53cc3`. This records a baseline,
not an optimization comparison. Raw reports and `summary.json` accompany this document.

## Workload and reproduction

Build Release with warnings as errors. Run three serial launches after builds and tests finish,
using Mesa llvmpipe, the offscreen driver and two llvmpipe workers:

```sh
export SDL_VIDEO_DRIVER=offscreen LP_NUM_THREADS=2
for run in 1 2 3; do
  /path/to/release/bin/LastlightExpanse --level bunker --headless --deterministic \
    --frames 360 --width 640 --height 360 --threads 2 --no-validation \
    --autosave-interval 0 --save-at-exit /tmp/bunker-${run}.save.json \
    --report /tmp/bunker-${run}.json
done
```

The default dormitory camera stays stationary. Each launch renders 360 frames and executes
360 fixed ticks (6 simulated seconds). This includes the D7 controller, interaction, door and
power systems, voxel/model rendering and full per-tick exit-checkpoint capture. Autosave writes
are disabled; normal exit publishes the final checkpoint. Startup asset loading and the final
exit write are outside steady-frame timings. There is no warm-up exclusion; the report's
frame-time distribution includes startup frames. No interactive input or traversal is measured.

Validation is disabled only for timing. The full Debug/Release gate separately exercises
Vulkan validation, replay movement, save/load and hot reload.

## Recorded baseline (2026-11-04)

Ubuntu 24.04 container, GCC 13.3 Release, AMD EPYC 9V74 host CPU; Mesa llvmpipe 25.2.8
(LLVM 20.1.2), Vulkan 1.4 for timing. Validated gate runs use Vulkan 1.3 with the distro layer.
The container exposes no CPU quota in `cpu.max`; shared-host scheduling can affect timings.

| Run | Mean FPS | CPU mean (ms) | GPU mean (ms) | Wall p95 (ms) |
| --- | ---: | ---: | ---: | ---: |
| 1 | 30.8927 | 32.3408 | 28.8362 | 37.1199 |
| 2 | 29.9469 | 33.3762 | 29.7564 | 39.1512 |
| 3 | 29.3485 | 34.0462 | 30.4935 | 39.4801 |
| Median | 29.9469 | 33.3762 | 29.7564 | 39.1512 |

All three endpoints match: 34 draw calls/commands, 65 visible instances, 7,066 triangles,
52 lights (379 submitted, 314 culled). Engine allocator bytes: **34,795,568** (33.18 MiB)
in each launch. Highest reported device-local heap usage: **993,796,096** bytes (947.76 MiB).
Maximum child process RSS across the launches: **171,532 KiB** (167.51 MiB). These counters
measure different populations and must not be added together. All final input fingerprints match.

The complete warnings-as-errors gate passed Debug 45/45 (404.22 s), Release 44/44 (90.86 s).
All 27 Debug and 25 Release generated application reports have zero Vulkan warnings/errors.
Both Highlands PNGs are byte-identical to pre-Phase-D `268a2bf` built on the same driver:
`61ab33fa0fc3305645a12a7dcd222ba2a9d54a136616b27bdb5031a1f3b9753d`.
Both moving-door continuations also match complete checkpoint JSON and final PNG bytes.

## Metrics and accounting

FPS is the report's reciprocal mean wall frame interval; CPU/GPU milliseconds come from their
own profiler series. Summary values are medians of the three run means, not pooled samples.
Draw counts and triangles are last-frame counters. `allocationBytes` counts engine allocator
allocations; `deviceLocalUsage`/`peakDeviceLocalUsage` are Vulkan heap-budget observations,
which on software Vulkan are not dedicated VRAM or total process memory. Process RSS is the
maximum child `ru_maxrss` across the three Linux launches, in KiB. It includes driver memory;
there is no claim of a complete game CPU-allocation inventory. Treat ±5% timing changes as noise.

Hardware GPUs, Windows/MSVC, physical input devices and real Blockbench/Blender exports remain
unvalidated. This stationary 640×360 software-rendered baseline does not predict hardware or
world-traversal performance.
