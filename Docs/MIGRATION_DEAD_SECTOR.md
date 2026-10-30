# DEAD SECTOR → RAVEN Engine migration map

## Source of truth

The HTML/Three.js game (`DeadSector.zip`, package `dead-sector` 0.1.0, three 0.180, ws 8.18)
defines gameplay behaviour. Its rules, weapon statistics, economy values and generation results
are preserved unless a change is explicitly approved. The original project is **never modified
or deleted**.

> The original is stored **unchanged** in [`Games/DeadSector/Reference/html/`](../Games/DeadSector/Reference/html/)
> (extracted byte-for-byte from `DeadSector.zip`). It is read-only reference material: ports
> and golden tests cite these files; fixes to the HTML game, if ever approved, are separate changes.

Facts checked while writing this map: ~60 ES modules (~273 KB), 47 Node unit tests + Playwright
browser tests, a Node `ws` room server (`server/index.js`) and a separate relay for an earlier
native client (`server/native.js`). The README mentions a Godot port in `godot/`, but that
directory is **not present** in the archive.

## Art direction

DEAD SECTOR keeps its PSX-inspired look, owned by the game (not the engine). The port
upgrades textures, models and lighting within that direction. The original fixed 540-line
pixelation is the baseline preset; exact parameters are tuned by visual comparison.

## Rules for the port

1. **Engine vs game.** Anything generic (rendering, physics, audio, navigation grid, net
   transport, UI primitives) goes to `Engine/`. Anything DEAD SECTOR specific (weapons table,
   enemy types, economy, missions, city families) goes to `Games/DeadSector/`.
2. **Data first.** Tables (`weapon-config.js`, `waves.js`, `economy.js`, `districts.js`,
   `asset-catalog.js`, `music-tracks.js`, `i18n.js`) become data files (JSON) loaded by the game,
   extracted by a script from the JS, not retyped by hand.
3. **Determinism.** Seeded generation (`town.js`, `interiors.js`, `mission-roll.js`, `weather.js`,
   `power-grid.js`) must produce the same layouts for the same seed. The JS PRNG/hash functions
   are ported bit-exactly (32-bit integer math, `Math.imul` semantics) and covered by golden tests
   generated from the JS version.
4. **Port tests with the code.** Each JS `tests/*.test.js` file gets a doctest equivalent.

## System map

| JS source | What it does | C++ destination | Engine/Game | Milestone |
| --- | --- | --- | --- | --- |
| `world-config.js` | Map size 250 m, edges, nav half-size | `Games/DeadSector/World/WorldConfig.h` | Game | M6 |
| `town.js`, `interiors.js`, `asset-catalog.js`, `districts.js`, `city-validation.js` | Seeded 100-building town, 28 families, interiors, placement + validation | `Games/DeadSector/World/TownGenerator`, data JSON | Game (uses Engine/Scene) | M6 |
| `city.js`, `skyline.js`, `surface-detail.js` | Instanced procedural geometry, backdrop belt, metre-scaled surface detail | `Games/DeadSector/World/CityBuilder` → Engine mesh/instancing; surface detail → Engine material feature | Both | M4 (subset), M6 |
| `navigation.js`, `enemy-motion.js` | 1 m nav grid, clearance per body size, broad-phase steering | `Engine/AI/NavGrid` (generic) + game agent config | Engine + Game | M6 |
| `enemies.js`, `waves.js`, `mutant-visuals.js`, `spore-cloud.js`, `poison.js`, `gore-particles.js` | Enemy types (HP/speed/damage/weights), behaviours, wave scheduling, effects | `Games/DeadSector/Enemies/*`; particles → Engine/Renderer particle system | Both | M6 |
| `player.js`, `look-input.js`, `first-person-body.js`, `body-visibility.js` | Fixed-step player physics (radius 0.32, height 1.8, eye 1.65), stamina, look input | Jolt `CharacterVirtual` in Engine/Physics; tuning in `Games/DeadSector/Player` | Both | M5 |
| `weapon-config.js`, `weapons.js`, `weapon-motion.js`, `weapon-icons.js`, `assets/models/weapons.json`, `assets/imported/Guns/*.fbx` | 10 weapons, magazines, damage, intervals, reload, wear, recoil motion, FBX models | Data JSON + `Games/DeadSector/Weapons/*`; FBX → glTF via `Tools/AssetCompiler` (ufbx) | Game + Tools | M4 (model), M5 |
| `economy.js`, `loot.js`, `shop-ui.js` | Prices, ammo 50/$50, 500 cap, medkits, repair, cash drops, SECTOR MART terminal | `Games/DeadSector/Economy/*`, UI on Engine/UI | Game | M6 |
| `operations.js`, `mission-roll.js`, `mission-models.js`, `encounter-events.js`, `power-grid.js` | Missions per wave, generators/growths, district blackouts | `Games/DeadSector/Missions/*` | Game | M6 |
| `weather.js`, `city-weather.js`, `atmosphere.js`, `atmosphere-particles.js`, `city-sky.js`, `environment-light.js` | Weather/fog rolls, wetness, day–night, sky, ash/sparks, environment lighting | Engine/Renderer (sky, fog, IBL, particles) + game weather rules | Both | M3, M6 |
| `flashlight.js` | Spot light, 120 s duration | Engine spot light + shadow; game battery rule | Both | M4 |
| `psx.js` | 540-line pixelated render target | `Games/DeadSector/Rendering/` PSX style preset (game shaders + settings) on engine internal-resolution/upscale and post-process capabilities | Game | M4 |
| `audio.js`, `music-tracks.js` | Synthesized SFX and 10 procedural scores (no audio files) | Engine/Audio (miniaudio) with a synth voice node; track data in game | Both | M5 |
| `game.js` | Main loop, state machine, glue (~40 KB) | `Games/DeadSector/DeadSectorGame` (split by system) | Game | M5–M6 |
| `compass.js`, `map.js`, `lobby-ui.js`, `shop-ui.js`, `metrics.js`, `settings.js`, `i18n.js`, `style.css`, `index.html`, `app.html` | HUD, map, lobby, settings, EN/RU strings, perf overlay | Engine/UI primitives + game screens; i18n → string tables | Both | M5–M7 |
| `network.js`, `connection-settings.js`, `peer-avatar.js`, `server/index.js`, `server/native.js` | Four-slot lobbies, 8-digit room codes, host-relayed sync over WebSocket, LAN/Render | Engine/Networking (transport, replication) + game protocol; C++ dedicated server replaces Node | Both | M7 |
| `scripts/import-weapons.js`, `scripts/weapon-materials.js` | FBX normalization, grip anchors, vertex colours | `Tools/AssetCompiler` | Tools | M4 |
| `scripts/export-godot*.mjs`, `scripts/test-godot-coop.py` | Exporters for the earlier Godot attempt | Reusable idea: export JS-generated layouts as JSON for golden tests | Tools | M6 |
| `scripts/economy-report.js`, `docs/ECONOMY_BALANCE.md`, `docs/GAME_DESIGN.md` | Balance reports, design reference | Kept as reference; report re-implemented as a test tool | Reference | M6 |

## Values that must be preserved exactly (non-exhaustive)

- Weapon table (magazine, damage, interval, reload, mode, price, unlock) — `weapon-config.js`.
- Enemy table (HP, speed, damage, spawn weight, radius, height, scale) — `waves.js`.
- Economy constants: `AMMO_LIMIT=500`, `AMMO_PACK=50`, `FLASHLIGHT_DURATION=120`, store position,
  reward clamp $15–100 from max HP — `economy.js`.
- Player body: radius 0.32 m, height 1.8 m, eye 1.65 m, 5 s sprint / 5 s recovery — `player.js`.
- Weapon wear: −5 condition per 100 shots fired; repair $500 for +50 — README/`economy.js`.

## Validation strategy

1. Extract tables from JS to JSON with a Node script; the C++ game loads the same JSON.
2. Generate golden files from JS (town layouts for N seeds, wave sequences, mission rolls,
   economy reports) and assert equality in C++ tests.
3. Behavioural parity for player/weapons via scripted inputs with fixed timesteps.
