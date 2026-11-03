# Scene extensions and transactional saves (D6)

## Scene components

Register on the main thread, before parsing any scenes or prefabs:

```cpp
struct Switch { bool enabled = false; std::string target; };
static const zodar::FieldInfo fields[] = {
    {"enabled", zodar::FieldType::Bool, ZODAR_FIELD_ACCESS(Switch, enabled), "State"},
    {"target", zodar::FieldType::EntityReference, ZODAR_FIELD_ACCESS(Switch, target), "Scene ID"},
};
zodar::registerSceneComponent<Switch>("switch", fields);
```

Fields use the existing reflection types and range validation. Tables and their name and
field string views must have static lifetime. Types must be default constructible and
copyable. Names and C++ component types must be unique and cannot shadow built-ins. Type-erased
registrations must supply their C++ `typeid`; aliases for an already registered type are rejected. Unknown component names and
unknown fields remain errors. Missing fields use component defaults; prefab overrides patch
existing fields. Registered values participate in parsing, instantiation, serialization and
`captureScene`. Custom types are not added to the generated built-in component reference.

`FieldType::EntityReference` stores a **string**, never an `Entity` handle. Empty means no
reference. Nonempty references must name an entity in the expanded document. References in
prefabs and overrides are relative to that prefab and receive the instance prefix, including
nested instances. `SceneId` preserves authored IDs through capture and restoration; generated
hierarchy IDs avoid collisions with authored IDs. Resolve strings through `SceneInstance::byId`
after restoration. Raw entity handles and cached component pointers must be reacquired.

## Save data and staging

`SceneFile/SaveGame.h` exposes `SaveGameDesc`, `SaveGameData`, `StagedSaveGame`,
`serializeSaveGame`, `parseSaveGame`, `writeSaveGame`, `loadSaveGame`, and `autosaveDue`.
The `zodar.save` version 1 envelope contains game ID/version, scene path/content hash, seed,
completed tick, named RNG streams, a flattened scene snapshot, game JSON values per key, and
runtime state. Model references retain registry IDs. Asset bindings must outlive the loaded
world. Material definitions are captured inline, including the saved values rather than rereading their
source files. Relative texture paths are rebased to their inline material directory. Relative
model paths resolve against `SaveGameDesc::sceneDirectory` (default: the parent of `scenePath`),
never the save slot directory. `sceneDirectory` is runtime configuration, not compatibility
identity; registry model IDs remain unchanged. Required model/texture assets still need to be
available; save files do not embed those assets.

`loadSaveGame` parses and instantiates into a **new World**. It returns that world and its
new `SceneInstance`; it never changes a caller's live world. Validate all application handlers
before moving the staged world into use. Errors include the candidate path and cause.

Application integration uses three protected hooks:

- `saveGameDesc()` supplies compatibility identity and backup/autosave settings.
- `captureGameSave()` captures the application world and JSON game values.
- `prepareGameSave(data)` validates every game key and builds replacement state. It returns
  a `SaveCommit`, normally via `makeSaveCommit(noexcept_lambda, stagedInputState)`, where
  `stagedInputState` is the candidate's value for the application's `inputState()` hook
  (the default is empty for applications that do not override that hook). The runtime combines
  it with staged named RNG streams and checks the saved/replay fingerprint before commit.
  Preparation must not mutate live application state. The commit must perform only
  nonthrowing swaps/assignments; allocate,
  validate, resolve assets, rebuild geometry and bind stable references during preparation.

`Application::saveCheckpoint(path)` is legal only inside `onCheckpoint`, after a successful
fixed tick (systems, application hook, timers and event publication). Calls during a fixed
system/hook or rendering are rejected. `loadCheckpoint(path)` is legal during startup or
`onCheckpoint`; replacing an active input recording is rejected. All input, RNG, gameplay
clock/queues, replay cursor and application state are staged before commit. A failure leaves
live state unchanged. GPU work is drained before replacing resources.

A checkpoint discards the wall-clock accumulator and interpolation remainder on restoration.
The simulation resumes at the next tick; wall-clock statistics are not persistent game state.
The runtime restores deterministic visual time from the completed tick. Replay compatibility
also checks the complete recording's canonical serialization hash, fixed dt, seed and input-map hash. A resumed
replay's `--frames` limit is its absolute ending tick, not a number of additional ticks.

## Events and timers

Both currently readable and pending event batches are saved, preserving insertion order
within each type. Cross-type delivery order remains undefined, as in normal EventBus use.
Register each persistent type with `events().registerType<T>(name, encode, decode)`, where
encode returns a string and decode validates a string into a value. A nonempty channel without
a codec fails capture. Decoding happens entirely in staging; unknown or duplicate channels fail.

Use `timers().registerCallback(name, callback)` and
`timers().afterNamed(delayTicks, name, payload, repeatTicks)` for persistent timers. Payloads
are strings whose semantics belong to that registered callback. IDs, next ID, absolute due
ticks and repeat intervals are preserved. Equal deadlines retain ascending ID order. Overdue,
duplicate or unknown timers fail staging. Pending anonymous closures fail capture rather than
being dropped or replayed early. Callback registrations and system definitions are code, not
save data, and must be installed before loading. Callbacks must resolve references from the
new application state rather than retaining pointers into the old world. Named callbacks must
not keep mutable closure state: persistent state belongs in the payload or game handlers.
Event spans and component pointers must be reacquired after a commit.

## Files, recovery and autosave

Writes reuse `Core/AtomicFile`: exclusive sibling temporary files, checked write/close, atomic
replacement on POSIX and Windows. Before publication, the writer prepares all replacements and
original recovery copies for the primary and `.bak1` to `.bakN` (default 3, configurable 0–100).
The primary is published last. An ordinary preparation/publication failure restores prior
contents and removes newly created destinations. If rollback itself fails, original copies are
retained and their paths are reported for manual recovery. Destinations must be regular files
or absent; symlink/special-file destinations are rejected without replacing them.

This provides rollback for runtime failures, not crash-durable multi-file atomicity. It does
not promise fsync/power-loss durability, metadata preservation, or concurrent mutation/writers
to one save chain. Readers always see individual complete files.

Corrupt/incompatible primaries do not rotate over valid backups. After application fallback
recovery, the first write to the recovered slot also skips its rejected primary, including a
primary that passed envelope parsing but failed game/runtime validation. Standalone API callers
can use `writeSaveGame(..., false)` when `StagedSaveGame::recoveredFrom` is nonempty. Successful
replacement then restores normal rotation for subsequent application writes.

Loads try the main file and then backups in increasing numeric order. Application loads also
validate runtime and game handlers before selecting a candidate. Recovery is explicitly logged
and `StagedSaveGame::recoveredFrom` identifies standalone API recovery. No candidate is silently
accepted after a compatibility or validation failure.

`SaveGameDesc::autosaveInterval` is in fixed-clock seconds, zero disables it. `autosaveDue`
uses integer tick boundaries after converting the interval with `ceil(interval / fixedDt)`.
Application integration enables writes when `--save-at-exit FILE` selects a slot. Lastlight's
interval defaults to 300 seconds; `--autosave-interval 0` disables periodic writes. The generic
Application default is off. There are no new player, door, power or D7 gameplay systems.

```sh
LastlightExpanse --load-save slot.json --save-at-exit slot.json
LastlightExpanse --save-at-exit slot.json --autosave-interval 60
```

Normal exit writes a snapshot cached at the last completed tick, after successful shutdown,
capture and report validation. Simulation, shutdown or output failure does not replace that
exit save. Periodic saves that already succeeded remain valid. Zero completed ticks cannot
produce an exit save; a loaded completed checkpoint qualifies even if no new tick runs. A replay
checkpoint is checked for divergence before publication. Exit-save failures update the run
report exit code as well as the process result. Caching a full snapshot each tick when a save slot is enabled trades
CPU/memory for an exact exit checkpoint; large mutable worlds should measure this cost before
adding incremental capture. Files, render resources, async job queues, profiler history and
external service state are not serialized.
