# Fixed-step input replay

`ZodarInputReplay` is a CPU-only example using the public input, scheduler and named RNG APIs.
It needs no SDL window or Vulkan device. Build with `ZODAR_BUILD_SAMPLES=ON` (default):

```bash
python3 tools/zodar.py --build-dir /path/to/build build ZodarInputReplay
/path/to/build/bin/ZodarInputReplay /tmp/example.inputrec.jsonl
ctest --test-dir /path/to/build -R '^examples.input_replay$' --output-on-failure
```

The example loads `input.json`, feeds movement and two button taps, samples once per fixed
tick, and runs one scheduled simulation system. Each pulse advances the independent `pulse`
RNG stream. It writes a checked, delta-encoded recording, reloads it, verifies compatibility,
then injects every recorded action snapshot into a fresh simulation. All 60 post-tick hashes
and the endpoint must match, including named RNG state. Failure returns nonzero in Release
as well as Debug. Output goes to the supplied file, never the source tree.

This standalone loop demonstrates the APIs. An `Application` already owns input sampling,
scheduler phases, timers, events and recording fingerprints: do not sample or step those
again in game hooks. Set `ApplicationDesc::inputMap`, implement `inputLevelHash()` and
`inputState()`, then use `--record-input` / `--replay-input`. Runtime fingerprints also include
named RNG state automatically. See [INPUT](../../docs/guide/INPUT.md) and
[GAMEPLAY](../../docs/guide/GAMEPLAY.md).

For transactional runtime continuation, see [SAVES](../../docs/guide/SAVES.md).
`gpu.lastlight_bunker_walk` saves during door movement, resumes the same replay, and compares
the complete final checkpoint, fingerprint and screenshot against uninterrupted execution.
This example does not implement save handlers or promise cross-platform float determinism.
