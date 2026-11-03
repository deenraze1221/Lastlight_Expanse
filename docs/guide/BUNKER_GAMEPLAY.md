# Bunker gameplay (D7)

The default bunker mode spawns a player in the dormitory. WASD walks, Shift sprints,
Space jumps on press, Ctrl crouches, mouse looks, E interacts, and Tab releases/captures
the mouse. Standing is blocked under a low ceiling. `--free-cam` retains the fly camera
(right mouse to look, Space/Ctrl vertical movement). `--explore` retains the old entrance
spawn for exploration regressions; `--bunker-walkthrough` retains the existing route.
Mode/spawn choice participates in recording compatibility.

Interactions choose the nearest authored interaction box within its range (default 2.5 m),
and reject targets behind voxel architecture, props or closed doors. E repairs panels,
toggles generators, or toggles sliding doors. Door leaves start parked open. Their travel
runs on the named `bunker.doors` fixed-tick timer. Each tick checks the entire swept volume
against the player; obstruction stops the leaf and requests reopening. Neither opening nor
closing can push the leaf through the player. Door collision follows the visible leaf.

Main lighting requires any running generator and a repaired distribution panel for that
room. Emergency lighting is the inverse of that room's main power. Unrepaired rooms stay
on emergency power. Point-light intensity is simulation state; lamp emissive overrides are
derived for rendering through `MaterialSystem::updateMaterial`, without mutating shared
model materials. There is no networking, construction, fuel economy, inventory, repair cost,
NPC behavior or D8 work in this step.

## Components and execution

All component types live in `game/src/Bunker/BunkerGameplay.*` and register through D6:

- `collider`: local `min`, `max`, `enabled`. Imported solids use generated collision prefabs;
  voxel architecture keeps the existing voxel query adapter. The old collider table/model
  bounds remain fallback paths for external models without authored boxes.
- `interactable`: local `min`, `max`, `range` (0.1–5 m).
- `door`: parent-local `closed`, `slide`, `fraction` (0–1), `seconds` (0.1–10), `open`,
  `obstructed`. Transform position must equal `closed + slide * fraction`.
- `powerSwitch`: generator `enabled` state.
- `electricalPanel`: `room` scene ID string and permanent `repaired` state.
- `emergencyLight`: `room`, lit `intensity`, and `emergency` (false for main lighting).
  Requires `pointLight`; a parent model receives the matching emissive state.
- `playerSpawn`: world-space `feet` and `yaw` in radians.

`Bunker.Controls` advances the player through `CollisionWorld`. `Bunker.Interaction` reads
the prior tick's typed `Used` events and emits new ones from action-driven raycasts. The
event codec persists stable scene IDs. The door timer runs after fixed systems and before
event publication. Static collider boxes are cached until scene replacement; moving boxes
are rebuilt from current transforms. Presentation does not advance gameplay.

The existing walker now implements the D7 controller, retaining its public name for callers.
It owns velocity, gravity, step/snap behavior, crouch state and camera step smoothing.
Game fingerprints sort stable scene IDs and include complete controller state, doors, panels
and generator state. Initial and reloaded scenes use D6 capture order, keeping light and draw
accumulation order identical after restoration. Asset compatibility hashes and derived lamp
materials refresh after scene replacement or a successful asset-only hot reload, rather than
rereading every asset at each checkpoint.
There are no raw persistent entity handles, mutable timer closures or global game state.

## Save slots

F5 saves and F9 loads `--save-at-exit FILE`, or `saves/bunker.save.json` relative to the
working directory. Interactive bunker sessions enable that default exit slot automatically.
Headless runs opt in with `--save-at-exit FILE` to avoid silently overwriting a user slot.
Periodic autosave uses the D6 fixed clock, every 300 simulated seconds by default;
`--autosave-interval` remains the explicit override. Normal successful exit saves the last
completed checkpoint. Atomic replacement, three rotating backups, recovery reporting and
transactional load are supplied by D6. Failed quick operations log an error and preserve play.

Registered ECS components persist doors, panels and power. The bunker handler persists
player/camera/controller state. Preparation instantiates and validates a replacement world,
builds collision and rejects player overlap before committing. Timers and pending interactions
use D6's runtime codecs. Quick-slot F5/F9 operations are ignored during recording/playback:
external slot history is not deterministic input. Use explicit `--load-save` to continue a
recording from its validated checkpoint; ordinary save/autosave/exit capture still work.

## Assets and checks

`LastlightAssetTool` registers game components and runs the same CLI implementation as
`ZodarAssetTool`; the generic tool remains independent of Lastlight. `tools/zodar.py` selects
the game frontend for paths inside `game/assets`. Regenerate and refresh registry metadata:

```sh
python3 game/tools/make_greybox_kit.py
python3 tools/zodar.py --build-dir /path/to/build import game/assets/prefabs game/assets/levels/bunker.scene.json
python3 tools/zodar.py --build-dir /path/to/build assets check --registry game/assets/assets.json --strict
```

`game/tests/bunker_walk.inputrec.jsonl` is a checked action recording: dormitory panel,
corridor panel, generator, close/reopen a door, then repair the airlock panel and return to
the airlock endpoint. The airlock panel sits clear of the west-wall corner so its interaction
surface is exposed to the room. Regenerate after intentional
content changes with `LastlightGameTests --write-bunker-route FILE`. The CPU authoring helper
uses the same movement, raycast, event delay and door logic; it does not teleport or bypass
occlusion. Header hashes bind the recording to the input map and current assets.

`gpu.lastlight_bunker_walk` replays that file, inspects saved player/panel/generator/lighting
state, including positive main-light intensities and zero emergency-light intensities in the
airlock, then compares a moving-door checkpoint continuation with uninterrupted pixels and
state. Unit tests cover interaction range/occlusion/nearest target, full-travel obstruction,
power combinations, enclosing interaction volumes, crouch clearance and the five-minute autosave boundary.
Debug `gpu.lastlight_bunker_asset_reload` changes a copied model without reloading the scene,
saves, and starts a new process to verify that the save remains compatible. Existing collision,
stairs, bunker walkthrough, replay/save and Highlands golden checks remain enabled.
