# Asset streaming — design (S3, not implemented)

Status: **design only**. S3 delivers the building blocks — the asynchronous `UploadQueue`
on the dedicated transfer queue, compiled KTX2 textures with baked mips stored smallest-first,
and a texture cache keyed by source content. This document fixes how a streamer will be built
on top of them, so later stages (M4 level loading, M6 open city) do not need to change those
building blocks. Nothing here is DEAD SECTOR-specific.

## Goals

1. Never stall a frame on I/O, decoding or uploads.
2. Bounded memory: textures and meshes stay within a device-local budget; quality degrades
   (lower mips) before anything fails.
3. Deterministic: streaming affects *when* data arrives, never *what* is rendered once it has
   arrived (the pixel-identity tests stay valid).
4. Every GPU feature it uses has a fallback (no sparse binding requirement).

## Non-goals (for the first implementation)

Virtual texturing / tiled resources, GPU-feedback-driven residency, mesh LOD streaming
(meshlets arrive in S9), network streaming, compression beyond BCn (Zstandard/GDeflate
supercompression can be added to the KTX2 path later; the reader rejects it today).

## Building blocks already in place (S3)

| Block | Where | Property the streamer relies on |
| --- | --- | --- |
| `UploadQueue` | `Renderer/Resources/UploadQueue.h` | Non-blocking `submit` → ticket; `acquireCompleted(frameCmd)` each frame; staging freed on completion; stats (bytes in flight) |
| Queue-family ownership | `makeOwnershipTransfer` | Images/buffers are released by the transfer family and acquired by graphics inside the frame command buffer; no stalls |
| KTX2 with baked mips | `Assets/Ktx2.h`, `ZodarAssetCompiler` | Levels are stored **smallest first**: the mip tail is the first bytes after the header, so a partial read gives a usable low-resolution texture |
| Compiled cache key | `Assets/TextureCache.h` | Content hash of the source → stale data is never used |
| Deferred destruction | `DeletionQueue` | Evicted resources are destroyed only after the GPU stops using them |

## Architecture

```
   game / scene  ──requests(asset, priority)──►  StreamingManager (main thread)
                                                   │  residency table, budgets, priorities
                     ┌─────────────────────────────┼───────────────────────────────┐
                     ▼                             ▼                               ▼
              I/O thread pool              decode/transcode jobs            UploadQueue (main thread)
          (file reads, async OS I/O)    (KTX2 parse, future: supercomp.)   submit → ticket → acquire
                     └──────── completed requests (lock-free queue) ───────────────┘
```

* **StreamingManager** (Renderer module, main thread). Owns the residency table and the
  budgets. Once per frame (before scene preparation): collects finished I/O, submits upload
  batches within the per-frame upload budget, calls `acquireCompleted` (already done by
  `Renderer::beginFrame`), publishes newly resident mips, and evicts over-budget resources.
* **I/O jobs** run on worker threads and touch no Vulkan objects. They fill *staging memory
  handed out by the manager* (persistently mapped ring, see below), so no copy is needed
  between the read and the GPU copy.
* **Uploads** stay on the main (submitting) thread because `UploadQueue` is not thread-safe
  and queue submission must be externally synchronised.

## Texture residency

Each streamable texture has:

* `residentMip` — the finest mip currently in the GPU image (levels `residentMip..N-1` are
  valid);
* `requestedMip` — from screen-space size: `log2(texelsPerPixel)` from the instance's
  projected bounds and the material's UV density (computed on the CPU from the frustum pass
  data; GPU feedback is a later option);
* a priority = f(distance, screen size, time since last visible, explicit game hints).

Rules:

1. **The mip tail is always resident.** On registration the manager reads the first block
   of the KTX2 file (all levels ≤ 128×128 — a few KiB, stored first) and uploads it, so every
   texture is immediately sampleable at low quality. Requests only add finer mips.
2. **Sampling clamp instead of reallocation where possible.** The image is created with the
   full mip count up front; non-resident fine levels are excluded with
   `VkImageViewCreateInfo::subresourceRange.baseMipLevel` (a new view per residency change;
   views are cheap and go through the bindless table). This avoids image reallocation and
   copies. Memory is still allocated for all levels in this mode.
3. **Memory-saving mode (budget pressure).** When the budget is exceeded, textures whose fine
   mips are evicted are *reallocated* with fewer levels (copy the resident tail GPU-to-GPU on
   the transfer queue, swap the bindless slot, retire the old image through the deletion
   queue). This is the fallback that works everywhere.
4. **Sparse residency (optional, later).** If `sparseResidencyImage2D` is available, fine
   mips can be bound/unbound without reallocation. It is an optimisation behind a capability
   flag, never a requirement (rule: every optional GPU feature has a fallback).
5. **Bindless slot swap is atomic per frame.** The descriptor for a texture is updated only
   between frames, after the upload's ticket is ready; materials keep the same slot.

## Budgets

* **Device-local budget** from `VK_EXT_memory_budget` (`queryMemoryStats`) minus a safety
  margin; the fallback is a fixed fraction of the heap size. Streaming textures and meshes
  share it with a configurable split.
* **Staging ring**: one persistently mapped `MemoryUsage::Upload` ring (e.g. 64–256 MiB)
  sub-allocated per request; a region returns to the ring when its ticket completes. This
  replaces today's per-batch staging buffers for streaming.
* **Per-frame upload budget** (bytes submitted per frame, e.g. 32 MiB) so the transfer queue
  never competes heavily with the frame's own copies; the measured `bytesUploaded`/ms from
  `UploadQueue::stats()` feeds an adaptive limit.
* **Eviction**: least-recently-requested first, finest mips first, never the mip tail and
  never anything referenced by the frame being recorded.

## Meshes

Meshes live in the `GeometryPool` (append-only today). Streaming meshes need a free list:
the pool becomes a sub-allocator (VMA virtual block over the big buffers); freed ranges are
recycled after the deletion queue has passed them. `GpuScene` instances whose mesh is not
resident are skipped (flag in the cull data), so the cull shader needs no change beyond one
flag test. Mesh LODs/meshlets (S9) plug into the same residency table.

## Ordering and correctness

* A resource is used by graphics only when its ticket `isReady` (acquired). The manager
  enforces this; the renderer never sees half-uploaded data.
* Determinism: given the same set of resident mips, rendering is identical. Tests can force
  "everything resident" (blocking mode) to keep pixel comparisons exact.
* Files are immutable while mapped/read; the compiled cache key prevents stale data.

## Instrumentation

ZodarBench and the overlay will report: resident vs requested bytes, upload MiB/s, requests
in flight, average latency request→ready (frames), evictions, and frames where the upload
budget was exhausted. These become the acceptance metrics of the implementation stage.

## Implementation plan (when scheduled)

1. Staging ring + I/O thread pool + StreamingManager skeleton with "always resident" mode.
2. Mip-tail-first texture loading with baseMipLevel views; screen-size requests.
3. Budgets and eviction with reallocation fallback.
4. Mesh sub-allocation in the geometry pool.
5. Optional sparse-residency backend behind a capability flag.
