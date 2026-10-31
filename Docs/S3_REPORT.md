# S3 Assets — completion report

Status: **implemented; finalized on the evidence below**. Hardware: NVIDIA GeForce RTX 4070,
driver 617.42, Windows (results supplied by the project owner). Cloud runs: llvmpipe
(software rasterizer — CPU timings and correctness only, never GPU performance).

## Scope delivered

| Item | Implementation |
| --- | --- |
| Asynchronous transfer queue | `UploadQueue` (dedicated transfer family, graphics-queue fallback), tickets on a timeline semaphore + fences, queue-family release/acquire, per-frame `acquireCompleted`, blocking `UploadBatch::submit` on top; `--no-transfer-queue` / `ContextDesc::dedicatedTransferQueue` for A/B |
| Offline KTX2 + BCn compiler | `ZodarAssetCompiler` (`model`, `texture`, `info`), `Engine/AssetPipeline` (bc7enc BC7, rgbcx BC1/3/4/5), KTX 2.0 reader/writer in `Assets/Ktx2` |
| Baked mips | Mips generated offline with the engine's filters (sRGB-correct box, normal-vector) and stored in KTX2; `loadGltf` uses compiled textures when the source hash matches; runtime mip generation remains only as the logged fallback for uncompiled models |
| Streaming | Design only: `Docs/ASSET_STREAMING.md` |

## Evidence

### Compiler and BC7 (RTX machine, `asset_compile.txt`; identical numbers in the cloud)

DamagedHelmet (5 × 2048² textures):

| Texture | Usage | Format | Levels | Size | PSNR (level 0 vs source) |
| --- | --- | --- | --- | --- | --- |
| image0 | colour | BC7 sRGB | 12 | 5.33 MiB | 47.66 dB |
| image1 | data | BC7 | 12 | 5.33 MiB | 43.61 dB |
| image2 | colour | BC7 sRGB | 12 | 5.33 MiB | 51.97 dB |
| image3 | data | BC7 | 12 | 5.33 MiB | 55.28 dB |
| image4 | normal | BC7 | 12 | 5.33 MiB | 49.29 dB |

Total 26.67 MiB vs 106.67 MiB RGBA8 + mips (4.0× smaller). Compile 1.59 s; second run
"5 up to date" in 0.14 s. KTX2 output is byte-identical between the Linux and Windows
builds (cloud check) and independent of the thread count (unit test).

### Loading and uploading (RTX, `assets_*.json`, 3 runs each, 1920×1080)

| Textures | Upload queue | Load ms | Stage ms | Transfer ms | Staged MiB | Device-local MiB | GPU frame ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| compiled BC7 | transfer | 46.7–47.9 | 2.0 | 1.6–1.8 | 27.7 | 190.9 | 0.42 |
| compiled BC7 | graphics | 46.7–47.1 | 2.0–2.2 | 1.6–1.9 | 27.7 | 190.8 | 0.42 |
| source PNG/JPEG | transfer | 882.8–886.5 | 14.3–15.2 | 20.6–23.5 | 107.7 | 318.9 | 0.42 |
| source PNG/JPEG | graphics | 883.9–888.7 | 11.0–12.1 | 21.2–23.3 | 107.7 | 318.8 | 0.42 |

Compiled assets: ~19× faster load, ~13× faster transfer, 128 MiB less device-local memory,
no GPU frame-time cost. Transfer vs graphics queue: no measurable difference for this blocking,
load-time upload (expected; the benefit is non-blocking uploads during frames, which this
benchmark does not exercise). The compiled-vs-source screenshots differ slightly (lossy BC7;
cloud measurement of the same comparison: 45 dB PSNR, 20 of 57 600 pixels > 32/255 on specular
edges).

### Synchronization validation (RTX, `assets_validation.txt`)

| Run | Upload queue | Errors | Warnings |
| --- | --- | --- | --- |
| model, compiled textures | transfer | 0 | 1 |
| model, source textures | transfer | 0 | 1 |
| city 64 | transfer | 0 | 1 |

The warning is the Vulkan loader's duplicate `VK_LAYER_OBS_HOOK` message (OBS Studio), not
ZODAR. These runs exercise the dedicated transfer queue with queue-family ownership transfer
through the blocking path (`UploadQueue::wait`).

### Pixel identity (RTX, `compare_results.txt`)

15 / 15 comparisons identical: grid 4096, grid 16k, unique 4000, city 32, city 64 at frames
50/300/550, gpu vs graph and gpu vs reference.

### Performance regression vs S2.1 (RTX, `bench_*.json`, same scenes and settings)

GPU frame time, average ms:

| Scene | gpu S2.1 → S3 | graph S2.1 → S3 | gpu cap 120 S2.1 → S3 | graph cap 120 S2.1 → S3 |
| --- | --- | --- | --- | --- |
| grid | 1.231 → 1.235 (+0.3%) | 1.210 → 1.212 (+0.2%) | 1.085 → 1.083 (−0.1%) | 1.062 → 1.066 (+0.4%) |
| grid 16k | 4.242 → 4.247 (+0.1%) | 4.243 → 4.235 (−0.2%) | 3.739 → 3.749 (+0.3%) | 3.729 → 3.734 (+0.1%) |
| unique | 1.071 → 1.073 (+0.1%) | 1.060 → 1.056 (−0.3%) | 0.931 → 0.931 (+0.1%) | 0.914 → 0.914 (−0.1%) |
| city 32 | 1.188 → 1.200 (+0.9%) | 1.163 → 1.191 (+2.4%) | 1.068 → 1.076 (+0.7%) | 1.042 → 1.060 (+1.7%) |
| city 64 | 1.539 → 1.601 (+4.1%) | 1.262 → 1.323 (+4.9%) | 1.394 → 1.463 (+4.9%) | 1.365 → 1.429 (+4.7%) |

All scenes are within the ≤ 5 % limit. City 64 is close to it (+4.1 % to +4.9 %). S3 adds no
per-frame GPU work to these scenes (uploads happen at load; the frame only gains an already-
signalled semaphore wait), and the untouched CPU-driven graph path moved by the same amount,
so the shift is attributed to run-to-run variance (clocks/thermal state) — **not confirmed**:
the locked-clock run that would separate the two was not performed.

CPU (gpu path): prepare p50 0.006 ms, recording p50 0.026–0.029 ms — S2 targets still met.
City 64 uncapped CPU-frame p99 2.458 ms (S2.1: 2.399 ms), dominated by waiting on a saturated
GPU; capped p99 0.290 ms. The p99 ≤ 2.0 ms target remains open exactly as in S2.1 (the owner's
decision on the uncapped measurement method is pending).

### Cloud verification (llvmpipe, this environment)

Debug, Release and Clang builds without warnings; ZodarTests 50/50; ZodarGpuTests 35/35 with
core + synchronization validation and 0 messages; ctest 8/8; Windows ZodarTests 50/50 under
Wine. llvmpipe has one queue family, so the cloud only exercises the graphics-queue fallback.

## Not independently verified

* **RTX unit and GPU test logs** (`test_unit.txt`, `test_gpu.txt`) were not supplied. The GPU
  tests that run only on a device with a dedicated transfer family — the per-frame
  `acquireCompleted` path with real queue-family ownership transfer, the graphics-queue
  fallback device created next to it, and BCn sampling on NVIDIA — have **no hardware evidence**.
  The blocking cross-family path *is* covered by the RTX sync-validation runs above.
* **KTX2 spec conformance** was checked only by the engine's own reader and tests (Khronos
  `ktx validate` was not run anywhere).
* The attribution of the city 64 shift to variance (no locked-clock run).

## Remaining limitations

* `UploadQueue` is single-threaded; no streaming (design only); staging buffers are allocated
  per batch.
* KTX2: 2D only, no supercompression; normal maps use BC7 (BC5 needs shader z reconstruction).
* The compiler's up-to-date check decodes the source images (≈ 3 s for the helmet on the cloud
  CPU, 0.14 s on the RTX machine).
* No ZodarBench scenario measures non-blocking uploads during gameplay.
