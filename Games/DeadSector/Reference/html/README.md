## Two versions: HTML and Godot

The existing HTML game remains in the repository root. The separate **Godot 4.6 Standard** native version is in [godot/](godot/README.md): import [godot/project.godot](godot/project.godot) and press F5. It includes native architecture selection/fresh interiors, weather/day-night, soldier body, original audio/music, weapons, six enemy types, waves, shop/economy, missions and airdrops. Native four-player co-op now supports direct LAN ENet and WebSocket rooms on the same Node.js deployment as HTML. See [connection instructions](godot/NETWORK_RU.md). HTML and Godot use separate rooms and cannot share a match.

# DEAD SECTOR

Browser wave-survival FPS with seeded 250 × 250 m towns, English/Russian UI and private four-player lobbies. All environment models are original procedural geometry. Weapon models come from the user's updated `gunsv2.fbx`, with the original pistol retained.

## Play

Download ZIP, extract it and open `index.html`. Play first opens a four-slot lobby (one occupied, three empty). The lobby Play button starts a five-second countdown. Offline solo play uses the same lobby and does not need a server. Single player needs no installation, server or Internet connection. A new run generates a town and starts beside SECTOR MART with 100 HP, $0, a pistol, 8 loaded rounds and 84 spare rounds. Starter ammunition makes the first cash earnings possible; there are no free ammunition or medkit boxes in the world.

WASD move; mouse look; Shift sprint; Space jump; LMB fire; R reload; E open the terminal or revive a nearby partner. **1 selects the primary weapon; 2 selects the pistol.** There are only these two weapon slots. Buying a different primary replaces the previous primary, returns its remaining magazine rounds to the universal reserve and equips the new empty gun. The pistol, its magazine and its condition remain. Reload the new weapon with R. H uses a small medkit; J uses a large medkit. Esc pauses single player. Resuming preserves the run; Restart starts over.

Sprinting lasts five seconds from full stamina. Stamina recovers in five seconds at walking speed or while stationary; exhaustion requires full recovery before sprinting resumes. The HUD shows health, stamina, cash, the two slots, weapon condition and ammunition. Static control instructions on the right have been removed. The terminal opens a scrollable shop with owned slot buttons, prices and purchase feedback. Close it using its button or Esc.

## Money, shop and condition

A dead enemy leaves a cash bundle on the ground. Walking within 0.85 m collects it automatically with a cash sound. Rewards derive from maximum HP, clamped to $15–100: runner 125 HP → $15; zombie 250 HP → $30; eye 200 HP → $20; brute 800 HP → $100. Cash remains until collected, and a bundle can be collected once. In co-op, one collection removes the bundle for everyone and credits its full value to every room account, including reconnecting players.

SECTOR MART is a unique grocery building in the central quarter. Its large terminal opens within 2.1 m. The shop contains ten prop families: stocked shelves, produce stand, refrigerated case, freezer, checkout with cash register, shopping cart, basket stand, display rack, stock boxes and the terminal. Entrances and the route to the terminal have physical player-clearance checks.

| Weapon | Magazine | Damage | Fire interval | Reload | Mode | Price |
| --- | --- | --- | --- | --- | --- | --- |
| Pistol | 8 | 45 | 0.28 s | 1.15 s | single press | starter |
| Helsing | 60 | 35 | 0.075 s | 4 s | automatic | $6,500 |
| Tikhar | 1 | 130 | 0.45 s | 3 s | single press | $4,000 |
| Shambler | 5 | 65 | 0.50 s | 3 s | single press | $3,500 |
| Shotgun | 2 | 90 | 0.60 s | 2 s | single press | $2,000 |
| Kalash | 30 | 50 | 0.14 s | 3 s | automatic | $4,500 |
| VSV | 12 | 80 | 0.24 s | 6 s | automatic | $6,000 |
| Scorpion | 35 | 45 | 0.10 s | 4 s | automatic | $5,000 |
| Vector | 20 | 40 | 0.09 s | 3 s | automatic | $3,000 |
| G36 | 35 | 40 | 0.14 s | 4 s | automatic | $5,500 |

The table reflects the latest balance; pistol remains45 damage. There is no aiming mode. Each weapon has a distinct synthesized report.

- Universal ammunition: **50 rounds for $50**. The **500-round limit includes reserve and both loaded magazines**. A full pack is refused if it would exceed the limit; no money is deducted.
- Small medkit: **$100**, immediately restores **25 HP** when purchased while injured; stored at full health for H.
- Large medkit: **$250**, immediately restores **75 HP** when purchased while injured; stored at full health for J. Health caps at 100; using a kit at full health consumes nothing.
- Every weapon has **100 condition**, losing **5 condition after each 100 shots actually fired**. Empty clicks do not cause wear. At zero condition it cannot shoot.
- Repair: **$500 for +50 condition** on the equipped weapon, capped at 100. Repairing an already intact weapon is refused without charging.

The updated FBX uses painted metal, brown stock/grip and blue pneumatic accents on appropriate parts, stored as vertex colors rather than stretched image textures. Authored normals are retained, and each model has individual trigger/support grip anchors and forearm endpoints. New guns are single-piece meshes; reload moves the gun and hand without detaching an invented magazine. Pistol retains its separate original magazine. `node scripts/import-weapons.js` rebuilds normalized geometry, colors and grip metadata.

## City generation

The town has 100 buildings and 28 repeatable architecture families, including the new cinema, fire station, motel and a tower with a 10 m structural height, plus the unique grocery. Ground floors use three floor plans: central spine, front lounge with rear rooms, and open floor with rear service band. Furniture, wall orientation and room use vary deterministically. Wide entrances have clear vestibules. Upper stories are closed architectural volumes; stairs and upper-floor access are not implemented.

Street population is increased by 15% after the baseline placement pass. Added families are traffic cones, street clocks, newspaper stands, public telephones and bus route boards. Reservations reject collisions with buildings, cars and existing props. Benches face pedestrian areas, bins pair with seating/shelters, lamps follow uniform pavement lines, cars fit lanes or marked bays, and fences form connected boundary runs of at least 14.6 m. The five extra prop families and all repeatable architectural families appear in each tested town.

`town.js`/`interiors.js` generate placement and colliders. `city.js` uses instanced primitive construction. Terrain is tiled once; room finishes replace yard patches. Markings/labels have depth bias, wall joints meet at boundaries, and shadow bias and ambient fill improve interior lighting. Solid instanced facades do not receive their own shadow map, eliminating the striped self-shadow acne; cast shadows remain on the ground. Furniture uses compound colliders at actual seat/table heights. Navigation accounts for the height/width of normal, flying and giant enemies. Tests validate overlap/support rules on 24 seeds and physically walk all room routes and the terminal approach on three cities; these checks do not guarantee every possible visual defect on every device.

World pixel rendering remains mandatory at 540 pixels vertically. There is no vertex snapping. Flat colors and original low-poly environment models are retained; no uploaded environment FBX/texture is used.

## Enemies and waves

| Enemy | HP | Speed | Damage | Chance |
| --- | --- | --- | --- | --- |
| Zombie | 250 | 2.42 m/s | 25 | 55% |
| Runner | 125 | 3.78 m/s | 15 | 20% |
| Flying flesh eye | 200 | 2.3 m/s | projectile 30 | 20% |
| Brute | 800 | 1.1 m/s | 40 | 5% |

Ordinary zombies have nine procedural outfits. Jointed shoulders/elbows/hips/knees/ankles drive opposite strides, planted stance feet and attack motions. Brute arms stay outside the belly. Death immediately hides the model and emits red particles from one bounded pool; Vomiters emit harmless green spores instead. There are no falling corpses. Death/outfit/animation state replicates without replaying particles on duplicate packets.

The first wave spawns 15 enemies gradually over 120 seconds from four corner hives. Each later wave adds five. After spawning finishes, **every enemy must die before the 30-second break starts**. The HUD shows CLEAR REMAINING / ДОБЕЙТЕ ОСТАВШИХСЯ while waiting. Projectiles are blocked by cover and damage only on body contact. The perimeter is a concrete wall 5.8 m high.

## Network play

Node.js >=22.12 is needed to host rooms:

```sh
npm ci
npm run build
npm run server
```

The main menu has Play, Join by code and Settings. Play opens your local four-slot lobby. Open to network inside that lobby creates a real server room and displays an eight-digit numeric invitation code with a copy button. Friends use Join by code in their main menu; they must connect to the same server. The host presses Play to start a synchronized five-second countdown. Guests can press Ready in the lobby to prepare mouse capture; a browser that has released capture may require Resume when the match begins. Disconnecting during the countdown cancels it and returns the remaining players to the lobby.

The default endpoint is the current site's WSS/WS service (the address is blank for an offline file). A different shared endpoint can be set in Settings → Server. Reconnect is shown only when a previous room is saved. Codes identify lobbies on that server; they do not publish a machine to the Internet. No public server is automatically deployed. The existing 60-second in-world preparation starts after the five-second lobby countdown.

The host simulates enemies, shooting and waves for up to four people. Each client sees up to three peers, enemies target any living player, damage is addressed to its recipient, and E revives a nearby downed teammate. The room server tracks cash availability, individual wallets, owned slots, medkits and market transactions. It checks pickup/terminal range and rejects duplicate cash collection or invalid purchases. Transaction revisions prevent delayed combat snapshots from undoing purchases/repairs. As in the existing private-room design, clients are trusted for combat movement/ammunition; this is not an anti-cheat service.

At zero HP, a partner within 2.5 m presses E to restore 50 HP. The run ends when every connected player is downed. Menu/shop use does not stop an active network world. Unexpected disconnect reserves the seat, account and world snapshot for 120 seconds; Reconnect can restore the same room after reload. Explicit host Disconnect closes it. Rooms are held in memory and disappear on server restart. Public Internet gameplay requires separate deployment; local four-client and real-socket checks cover the provided implementation.

For Docker, first run `npm ci && npm run build`, then `docker build -t dead-sector .` and `docker run --rm -p 3000:3000 dead-sector`. The image runs as an unprivileged user; a public HTTPS site needs a TLS proxy with WebSocket upgrades.

## Development and checks

```sh
npm ci
npm run dev -- --port 5173
# Open /app.html for source development; root index.html is the generated standalone build.
npm test
npm run build
CHROMIUM_PATH=/usr/bin/chromium npm run test:geometry:browser
CHROMIUM_PATH=/usr/bin/chromium npm run test:mutants:browser
CHROMIUM_PATH=/usr/bin/chromium npm run test:economy:browser
CHROMIUM_PATH=/usr/bin/chromium npm run test:browser
CHROMIUM_PATH=/usr/bin/chromium npm run test:combat:browser
CHROMIUM_PATH=/usr/bin/chromium npm run test:city:browser
```

Browser checks need a running dev server. Override BASE_URL, CHROMIUM_PATH, ARTIFACT_DIR (screenshots), and WS_URL (external server for network/revive scenarios) as needed. `test:economy:browser` starts an isolated local room server and funds its test account server-side to verify purchases; the playable client has no money-grant debug API. `test:weapons:browser` aliases that market/equipment scenario. `test:network:browser` creates its own isolated server and verifies four browser clients; `test:revive:browser` uses a running room server. Tests cover exact stats, slots, wear/breakage, cash, caps, medkits, repairs, stamina, transactions/reconnect, wave completion, collision routes and model bounds. Software headless rendering is not a measurement of desktop GPU FPS.

`npm run build` writes self-contained `index.html` and `dist/index.html`, including models and styles. Source edits require rebuilding. The committed root file supports double-click offline play.

## Atmosphere and encounter update

The town now uses a cool overcast evening palette and lighter-distance fog, warm grocery lighting, cold street lights and an occasional flickering circuit. Activating the generator stabilizes and strengthens the street circuit for the rest of that run. Instanced boarded windows, facade cracks, litter beside dumpsters and weathered vehicle panels mark the abandoned environment without obstructing tested entrances. This is stylized procedural scenery, not a destructible-building simulation.

Each gun has its own recoil parameters and reload motion profile, with animated action/bolt, spent cases and support-hand operation. The original FBX guns remain single meshes: these are procedural first-person mechanisms rather than imported skeletal animations. Hits emit small red particles and cause a body recoil. Wounded enemies limp and slow to 80% speed below 35% health. Normal/runner melee has a short preparation; brutes wind up for 0.6 s and can miss if the player moves away or takes cover. Flying eyes swell before firing. Brute steps have a separate low-frequency sound.

Wave 1 remains standard. Waves congruent to 2 modulo 3 concentrate on one hive; waves congruent to 0 modulo 3 favor runners (at least70%, depending on unlocked types); every fifth wave overrides these modes and finishes with one mini-boss. It has 1,800 HP, 55 melee damage, speed 1.3 m/s and a separate armored Warden body, 6.6 m height and 2.1 m navigation radius, plus visible red chest marks and a HUD health counter. It must die before the normal clear-and-break transition.

Missions use a single seeded roll after20 seconds of assault. Waves1–2 never produce missions; wave3 onward has a15% chance. The successful roll selects one package, terminal-zone, generator or growth objective, paying300–600 dollars to each player. All four mission types share one lifecycle and persist until completed. A carried active mission prevents a second mission from replacing it. Pickup is E, terminal radius is6m with30 seconds of continuous occupation, and beeps repeat every5 seconds. The server awards each shared completion once. Generator interaction stabilizes street lights; the growth must be shot with cover respected.

Wave-clear bonuses remain separate: $75 + $25 × wave number, or $500 every fifth wave.

See [the reproducible resource model](docs/ECONOMY_BALANCE.md), generated by `node scripts/economy-report.js`. It includes ammunition and amortized repair costs at 65% accuracy, but excludes travel and damage taken. Prices and requested weapon damage are unchanged; the report is not a substitute for playtesting.


Shop update: two tabs separate weapons from gear (ammo, medkits and repair). Weapons are ordered by price. Each card shows a side silhouette projected from its actual model; hovering or keyboard focus reveals damage, magazine capacity, cyclic rounds/minute and reload time. Touch screens show those details directly. The starter reserve is now84, for92 total rounds. Cash auto-picks up underfoot with a separate sound. Flying-eye projectiles now travel at20 m/s, twice their previous speed; enemy types have separate growl/snarl/chirp sounds.


A fresh match has a60-second preparation countdown with no enemy spawns or events. The first assault begins after that countdown; enemies then emerge gradually on its normal120-second schedule. Subsequent intermissions last30 seconds after every enemy is dead. Each assault starts with a five-second on-screen notification and an original synthesized horn. Original procedural ambient music has calm and tense stems: preparation/intermission uses calm chords, active assault/clearing uses the tense stem. Changes crossfade over2.5 seconds; paused single player fades music out. Browser audio starts after the player's Play gesture. No external songs or audio downloads are used.


Enemy progression: waves1–5 contain only zombies/runners (weights55:20 normalized over available types). Waves6–20 add eyes (55:20:20 normalized) and allow Vomiters under the per-wave15% cap. Wave21 onward enables regular brutes as well, with base weights55/20/20/5. The separate Warden mini-boss appears every fifth wave starting at wave5; it is not a brute and does not unlock the regular brute early. Fifth-wave completion rewards remain $500 even before bosses unlock. Purchased medkits immediately heal an injured player; full-health purchases go into reserve for H/J.

The fifth enemy family is the armored Warden: distinct mask, shoulder armor and horns, slimmer body and separate navigation clearance. Regular brute unlock is wave21.


Latest visual/control changes: the first assault contains15 enemies, then20,25 and +5 thereafter. A10m lattice landmark rises above the grocery roof with four SECTOR MART signs and a beacon. The terminal has a proper display, keypad, payment slot, trim and stereo beeps attenuated to silence at8m. The soundtrack now has five original calm and five original tense procedural compositions with different tempo, harmony, melody and timbre, rotating by wave while preserving the2.5s mood crossfade. Pointer-lock re-entry resets buffered input; non-finite/warp deltas are discarded and bounded direct input prevents sudden180° spikes. A top compass shows heading, azimuth ticks, localized N/E/S/W, and distance markers for the shop and up to three teammates. Targets behind the field of view are pinned to an edge.


Per-wave weather uses independent seeded rolls: overcast55%, rain20%, storm15%, clear10%. Fog is rolled separately: light60%, medium30%, heavy10%, and can overlay any weather. Rain is masked under building roofs; storms add brief lightning and delayed thunder. The four-wave day/night cycle is day for waves1–2 and night for3–4, repeating; each wave advances six in-game hours. Clock starts at06:00, freezes during preparation/clearance waits/intermissions and does not rewind during breaks. Weather, fog and day phase derive from the shared seed/wave and match across a lobby.


Settings now use Audio/Controls/Graphics tabs. Audio sliders persist master, music and weather volume independently, including mute at0. Weather has looping rain/wind beds, delayed storm thunder and fades between conditions; weapon/terminal sounds remain under master. Controls contains sensitivity and the key map. Low/Medium/High graphics adjust shadows, lamp lighting and rain density; required540p pixel rendering is unchanged. Graphics settings are reapplied when a new city or network room rebuilds the scene.

Menu settings open in a separate right-hand drawer with a close button. The menu plays the calm soundtrack after the first user gesture; buttons have hover/click tones routed through master volume. Gear cards include ammunition, medkit and repair silhouettes. Mouse input is consumed once per frame without a smoothing tail. All ten guns now use individually reviewed grip anchors aligned to the pistol grip and fore-end. A dedicated 45-degree weapon camera makes guns approximately54% larger without changing world FOV. HUD elements are scaled to125%; the compass has a transparent background.

Soldiers replace the old network capsules: helmet, goggles, face covering, plate carrier, pouches, backpack, shoulder protection, articulated legs and boots. Peers display the actual equipped model using the same grip anchors; shoulder/elbow segments connect to glove wrists. Aim pitch is replicated, poses interpolate, and hips/knees/feet animate each frame between pose packets. Looking down in first person reveals the local torso and legs without rendering a duplicate head or weapon. These are original procedural stylized models and animations.

Darker atmosphere: reduced skylight, cold overcast/storm palettes, stronger distance haze and subdued exposure, while warm shop lights remain. A fixed-buffer ash field drifts around the player, excludes covered interiors, and scales with quality. Broken street lamps emit short amber spark bursts; repairing the generator stops them. Hit bursts are denser, and red fragments retain a faint emissive contribution in the dark.

Survival equipment: flashlight $300 (includes120s charge), spare battery $150, bear trap $200. F toggles the directional beam; only powered-on time drains charge. B consumes a spare to refill120s. The flashlight is a one-time purchase. Key3 selects a handheld trap, LMB places one at the player's feet;1/2 returns to guns. Traps ignore players and flyers, deal300 damage once to the first ground enemy, disappear, and produce a15m snap. Owned equipment and placement consumption remain server-authoritative in private rooms.
Waves1–2 have no missions. Starting at wave3, a shared, seeded roll occurs once per assault with15% probability; it selects one mission from package, terminal-zone, generator or growth. New mission types: collect a package inside a randomly selected building using E, or hold a6m terminal zone continuously for30s. The terminal beeps every5s. Each mission pays a seeded300–600 dollars to every room account, once. Generator/growth use the same300–600 reward range and the same single15% roll; there is no separate old-event reward path. At480s intervals of active match simulation (solo pause freezes the clock), a transport appears with engine audio, flies for6s and then drops a parachute supply crate onto an unobstructed road. After8s descent, hold E within2m for10 uninterrupted seconds. Releasing E, leaving range or dying resets progress. The opener receives100–500 dollars, once; this reward is individual.
Enemies now remember gunshots and trap noises, investigate last-known positions and use alternating flank routes when the player is visible. Flying eyes seek a clear ranged position and keep distance. The Warden has three HP phases with increased pursuit speed; phases2/3 add a visibly telegraphed area slam every third melee attack. The ground ring warns of3m/4.5m range for1.2s; walls and moving out of range prevent damage.
Rain progressively darkens the physical asphalt material, reduces roughness and adds a clearcoat highlight; wetness lingers and dries gradually afterwards. This is material-based wet shading, not screen-space reflections. Soft low fog drifts around the four hives; damaged plaster and small rubble details decorate facades. Ash and rain are now world anchored and only recycle beyond the nearby simulation volume, rather than following player motion. The local body appears only below a roughly55-degree downward look and is offset behind the camera. Movement has a short acceleration/braking ramp; weapon bob uses actual speed, so Shift alone does not animate it. Main-menu UI is20% larger, the independent settings drawer40% larger, and Join uses the game style. Wave completion now has its own notification and sound. English/Russian translations include the new equipment, missions and network status messages.

Sector HUD spacing has been expanded: sector title50% larger, combat/weather labels25% larger with separate rows. Master audio output is boosted30% while retaining independent volume sliders.

Combat HUD panel opacity is reduced20% relative to previous values and uses neutral charcoal. Weapon slots sit above ammunition on the right; equipment stays above vitals on the left. Context prompts appear only near usable objects or nearby enemies.

A successfully rolled mission announces itself to every player with a sound and popup, and a gold MISSION marker on the compass. Any living teammate can complete it; the server credits each account once. Completion has a shared popup and tone, and removes the marker. Supply crates have a separate blue compass marker.

`npm run test:survival:browser` verifies mission announcement/audio/compass, wave-end notification, sector/weather spacing and English HUD. Its browser-only fixture forces a qualifying wave via the Waves prototype; it does not add a debug interface to the shipped game. Other checks cover equipment purchases, real flashlight drain/battery replacement and selecting/placing a trap without firing the gun.

Generator and growth are now full shared missions in Operations, alongside package and terminal. The duplicate old crossroads task is removed. The generator is an original diesel model with wheeled chassis, fuel tank, engine vents, exhaust, control panel, gauge and cable. The growth has fleshy lobes, pulsing emissive core, veins/roots and bone spines. Its health is500 +500×floor(wave/10):500 on waves1–9,1000 on10–19,1500 on20–29. When an active growth crosses a tenth-wave boundary, its maximum and current HP rise by500, preserving damage already dealt. Health replicates to all clients.

Enemy voices now use distinct layered procedural living/death sounds for zombies, runners, brutes/bosses and eyes, distance attenuated within25m/30m. Hosts and guests play living calls locally; replicated deaths sound once per enemy, without replay on repeated snapshots.

Primary weapons unlock after completing waves: Shotgun2, Vector5, Shambler8, Tikhar10, Kalash10, Scorpion15, G3618, VSV20, Helsing25. Locked cards show the requirement; the server validates completed-wave progress for all players. The first Warden on wave5 has1800HP; each subsequent five-wave encounter adds600HP (wave10:2400, wave15:3000). Its actual maximum HP replicates to guests.

Locked weapon cards are darkened with a lock over the silhouette; the price remains visible and a footer states the required completed wave.

Warden size is tripled (scale3.3, radius2.1m, height6.6m) with matching navigation and melee reach. First-person view magnification: Shambler×1.5, Kalash×1.4, Scorpion×1.3, G36×1.25, Vector×1.2; Helsing combines×1.1 proximity with×1.4 size (×1.54). World camera and third-person gun sizes are unchanged. Weapon/utility switching takes1.5s:0.75s lowering,0.75s raising, with firing/reloading blocked until completion.

City expansion adds residential highrise24m, corporate tower32m, hospital tower21m and communications tower28m plus rooftop aerial. Ten street props: ATM, vending machine, bicycle rack, extinguisher, sandbags, survey tripod, construction light, notice board, water tank and concrete pipe. The five calm/five tense scores now include Cold Dawn and Blackout.

## Movement, crowds and presentation

Acceleration and braking take15% longer; jump apex is50% lower (launch speed divided by sqrt2). Ground enemies default to the player spawn beside the market as their base destination. Players within180m trigger pursuit even behind walls; a short sight memory and the existing gunshot investigation remain. Smoothed grid routes, distributed flanking positions and three-pass local crowd separation prevent shared-cell queues. Terrain collisions constrain separation. Ground zombies use ballistic jumps and floor/ceiling checks to reach players on vehicles up to2.8m above them. Authoritative enemy positions, including height, replicate to guests.

A seeded decorative belt extends100m behind each perimeter wall, including corner districts, with varied rooflines, facade windows and roof equipment. It has no gameplay colliders or interiors. A moving cloud sky, night moon/halo and world-space surface weathering preserve the dark pixel aesthetic without vertex distortion.

Graphics settings include an optional metrics overlay: basic FPS; extended FPS, frame time and measured WebSocket round-trip latency; advanced adds available JS heap readings and approximate device RAM capacity. Browser security does not expose GPU temperature or whole-system used RAM, so unavailable readings are explicitly marked. Metrics default off and persist locally.

Flashlight charge is shown as a120s battery meter and percentage; purchases/refills use the same120s capacity. Nearby live enemies within25m appear as red squares on the compass, replacing the nearest-enemy interaction text.

The local body fades in over the look-down angle and roughly0.25s of motion using cloned, dithered materials, without changing remote soldiers. Slot3 requires an owned trap; spending the last trap returns to the firearm through the existing smooth switch. The original supply transport has a tapered fuselage, cockpit, four animated propellers, wings, tail and navigation lights.

## Surface and lighting upgrade

Materials use metre-scaled world coordinates: staggered brick courses, pavement joints, damp concrete bases and wood grain retain their scale on differently sized meshes. Road patches become reflective puddles as the existing rain wetness accumulates, then dry gradually. A cached, original sky environment supplies soft reflections to metal, glass and wet asphalt, with reduced intensity at night. The shadow frustum follows the player over a140m area for better local detail. Street-light pools and halos are instanced/world-anchored and disabled on low quality; night skies gain sparse stars behind cloud cover. Geometry and gameplay colliders are unchanged.

## 250m city expansion

The playable square is250×250m (62,500m²), with100 city blocks,298 furnished ground-floor rooms and106 evenly spaced street lamps. New architecture: courthouse,38m Art Deco tower, freight depot and observatory with a rooftop dome. New props: recycling station, parcel locker, bus sign, road block, cable reel, utility cabinet, flower box, street kiosk, delivery trolley and solar bollard. All are original procedural models and participate in placement bounds/collisions. Select facade windows emit a restrained warm glow at night without adding point lights.

World dimensions are shared by generator, server pose bounds, navigation, flying-enemy positioning, mission placement and the100m background belt. Navigation rasterizes collider footprints directly to accommodate the larger city without a collider scan for every grid cell.

## Districts and creature presentation

Four districts now constrain building selection and vary facade materials, details, trees and service props: Ironworks, Old Residences, Civic Quarter and Market Row. The original five zombie outfits gain firefighter, police, mechanic and exposed-infection costumes (nine total). The Warden retains its original outfit and gains plague, iron, charred and reclaimer variants (five total), selected per boss encounter and replicated by skin index. Face details, clothing equipment, idle breathing and continuous anticipation/strike/recovery improve the rigs while retaining planted feet and brute arm clearance.

Every fourth assault cuts power to one rotating district; boss assaults take priority. Lamps, facade windows, signs and light halos/pools go dark until intermission. Store terminal and its emergency light remain usable. The event is derived from wave state on every client.

Enemy awareness is180m, independent of line of sight; melee and projectile damage still require unobstructed paths. Added ticket machine, drinking fountain, vent stack, fire-hose cabinet and folding barricade. Tripods now connect three grounded feet to a raised hub instead of an inverted point. City construction removes exact duplicate primitives and reports same-transform overlaps between different non-decal materials for review.

Enemy traversal now validates smoothed segments against actual expanded terrain colliders, picks the nearest free navigation cell by distance, replans after blocked movement or a3s no-progress window, and attempts headroom-checked hops over low prop obstacles. Missing grid paths fall back to collision-constrained pursuit so recovery still runs. Every surviving enemy is marked on the compass during clearing. Regression scenarios cover cars, connected fences, narrow gaps and all hive routes on two furnished seeds.

Mouse look now requests unadjusted relative input where supported, falls back to standard pointer lock, rejects stale queued events and isolated cursor warps, and removes the old16-degree-per-frame truncation that made fast movements depend on FPS. Ordinary deltas apply once with no smoothing tail. Five additional street models and grounded tripod feet are included in the rendered-bounds audit.

First-person body update: the local view keeps a narrowed leg rig (72% width/depth) with torso, vest, backpack and upper gear hidden. Walk/run steps advance from actual physics travel, with separate stride lengths, planted-foot IK, smooth stop/start and a jump tuck. The local view no longer estimates speed from network-packet timing. Remote soldier proportions and rig remain unchanged.

Local body now includes a compact pelvis, belt and lower abdomen joined to straight-legged, hip-driven steps; the upper vest stays hidden to preserve downward visibility. Refrigerated display back panels are inset within their frame and box tape uses separate raised strips, removing the reported coplanar surfaces. A linear HDR target now combines world and viewmodel before one post pass (gentle bloom, restrained sharpening/color grading, vignette and grain), retaining built-in pixelization. Surface normals gain subtle micro relief. Heavy fog density is doubled to0.058; other fog tiers are unchanged.

## Vomiter enemy

Eye health is200HP; brutes have800HP and unlock on wave21. Vomiters unlock on wave6 with300HP,1.25× standard scale and2.057m/s speed (15% below the standard2.42). They prepare a visible0.25s vomit attack only within8m and with a clear firing line, then launch a green liquid projectile at5m/s with a maximum1.6s flight. Reload is1–2s after release. Walls stop the projectile. Impact deals10HP and starts five1s poison ticks of5HP each; repeat hits refresh rather than stack poison. Typed hits/projectiles and enemy preparation replicate to guests.

A seeded15% replacement roll is capped at floor(wave total×0.15) Vomiters per wave; zero are possible. The per-wave count survives snapshots and resets between waves. Their death emits harmless green spores for2.5s and a wet sound instead of red gore. Health below25HP displays a persistent red edge vignette during play; poison has a separate countdown.

Player look-down is capped at60degrees below the horizon; the upward limit remains unchanged. The limit applies to accumulated mouse input at every sensitivity.

UI refresh: neutral field-terminal panels, consistent button/focus states, compact store cards and revised menu/settings/lobby/HUD styling. The store header and tabs stay fixed while cards scroll without a visible scrollbar; wheel, touch and keyboard scrolling remain available. The world camera FOV is73degrees (was78); weapon viewmodel zoom is unchanged.
