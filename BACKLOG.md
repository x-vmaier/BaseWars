# Backlog: specs of tasks that are not current yet

Open this file only when a task becomes the current one. Then **move its spec into `TODO.md`** ("Current task specs") and **delete it here** (part of the close-out: this file must only hold tasks that are not done).
Roadmap order, status and the task rules live in `TODO.md`. Task ids, "Done when" criteria and code pointers below are unchanged from the original plan.

## Naval (T14)

### T14b Wind and ship combat rules (size S)

* Wind direction (server-authoritative singleton in `registry.ctx()`, slowly changing, replicated, persisted) + wind-rose speed modifier per heading; compass in the HUD.
* Ship combat only with archers aboard or a crossbow upgrade.
* Done when: selftest `wind` (speed differs by heading as configured; ships without archers cannot attack; save/reload keeps wind); screenshot of the compass.

### T14c Harbour window and multi-modal routing (size M)

* Grid-based Harbour window in the Archery (`TrainingPanel`) style (frame `assets/ui/training_panel.tmj` from `tools/gen_ui_maps.py`): build ships / fleet / harbour upgrade. Replaces the context-panel path.
* Multi-modal route planning (land -> boat -> land) for pawns and clan defense-quota troops whenever water is faster (generalise `FerryPlanner`; `DefenseSystem::planDefenseRoute`, pawn haul).
* Done when: selftest `ferry-route` (a destination across water is reached via ship faster than the land detour; falls back to land when no ship; no orphan jobs after ship destroyed mid-route); screenshot of the window at scale 1 and 2.

## Unit behaviour and logistics (T15)

### T15a Auto-defense vs manual orders (size S)

* Manual orders and target priority are never overridden by auto-acquire; auto-defense only for idle units. Done when: selftest `orders` (a unit with a manual attack / move order ignores a closer enemy; an idle unit defends).
* Code: `CombatSystem::fight`, `UnitSystem`, `DefenseSystem`.

### T15b Reliable caravans and relocation / donation / respawn cleanup (size M)

* Reliable automated supply caravans (including across water and to clan mates). Clean town relocation / donations / respawn: jobs, ships and caravans referencing a moved or destroyed Town Hall are re-targeted or cancelled via `on_destroy` hooks.
* Done when: selftest `logistics` (caravan completes, survives save/reload, cancels cleanly when the target Town Hall is destroyed or moved; invariants hold throughout).
* Code: `ConvoyHaulSystem` (read the reasoning comment block on the earlier loop bug), `SpawnSystem::spawnBase`, `NetSession`, `World::removeOwner`.

### T15c Clear Zone (size S)

* Drag-select area tool next to single-obstacle right-click clear: designates every tree / rock / gold vein in a rectangle; the server expands the rect with a cap `clear_zone_max_tiles`; reuse `C_Clear` / `ClearingSystem` jobs (append a message if needed). Persist clear jobs (closes a known gap).
* Done when: selftest `clearzone` (cap respected, jobs created once per tile, jobs survive save/reload); screenshot of the selection overlay.

### T15d Pawn roles and job priorities (size M)

* Roles Builder/Repair, Obstacle Clearer, Logistics Hauler as a persisted per-pawn component, set per pawn (context panel) and as a Town Hall allocation. `ConstructionSystem` / `ClearingSystem` free-pawn choice and `ConvoyHaulSystem` honour priorities so
  critical repairs and construction are never starved by clearing (closes the known gap). Defaults in `server.cfg`.
* Done when: selftest `pawnroles` (with many clear jobs and one upgrade, the builder-role pawns take the upgrade; role persists across save/reload); screenshot of the role UI.

## Combat and protection (T20)

### T20a Combat triangle: roles, armor classes, damage matrix (size M)

* Replace `damageVs` / `armorOf` in `Shared.hpp` by role / armor-class fields in `KindDef` plus a damage matrix whose factors are `server.cfg` values. Melee infantry screens the front (high arrow resistance), ranged / fire units hit unarmored units and structures hard,
  siege / raider units do bonus damage to walls and buildings and are weak to infantry. No new sprites: realise siege with existing faction raiders (log the choice).
* Done when: selftest `damage-matrix` (every role pair deals the configured factor; changing `server.cfg` changes outcomes; a mixed army beats a mono army in a scripted battle).

### T20b Garrisons, walls and watchtowers (size M)

* `Garrison` component (capacity, occupants, hidden like `Aboard`), a `KT_*` tag for garrisonable buildings, appended commands to garrison / leave. Garrisoned ranged units get more range, damage and protection. Watchtowers reuse `Tower`; walls: decide how they look (no wall sprite) and log it.
  Replicate occupants, save garrisons, `on_destroy` ejects occupants.
* Done when: selftest `garrison` (capacity, bonuses, eject on destroy, save/reload, invariants); screenshot of a garrisoned tower.

### T20c Offline protection and peace shield (size M)

* When the player and all online clanmates are offline: reduced incoming structural damage, NPC sieges against the base paused (`PatrolSystem` / `FactionSystem` targeting, Pacing paused), and a temporary peace shield after heavy destruction (threshold and duration in `server.cfg`,
  real-time, persisted, shown in the HUD, ended by the owner attacking someone). Never block the owner's own play.
* Done when: selftest `protection` (offline reduction applies, shield triggers at threshold, expires by wall-clock after restart, ends on owner attack).

### T20d Scorched blueprint ruins (size S)

* A destroyed player building leaves a `Ruin` (T18b) on its tile, rendered as a dark scorched ghost of the sprite, rebuildable at `ruin_rebuild_cost_pct` / `ruin_rebuild_time_pct`; hook `World::destroyNow`, footprint handling, placement checks; persisted with expiry.
* Done when: selftest `ruins` (destroy -> ruin, cheaper rebuild, expiry, save/reload, no footprint overlap); screenshot of a ruin.

### T25b Pacing Director v2 (size S)

* Raid composition built from roles (T20a): the director picks a mix that teaches counters (e.g. raiders plus escorts), announces it in the Warning phase (icons of incoming roles), and the Aftermath reward scales with how well the player defended. Uses garrisons if present.
* Done when: selftest `pacing-v2` (composition follows the role table, strength cap holds, announcement lists the composition).

## Replayability (T27)

### T27a Chronicle and legacy perks (size M)

* Persisted per SteamID `Chronicle`: playtime, towns founded, raids survived, buildings lost, achievements (data table). Visible in a Chronicle window (paper theme).
* Small, capped **legacy perks** unlocked by achievements and chosen when resettling / starting a new world (e.g. +5% start wood, faster first pawn, one extra hint of the map). Never enough to trivialise early game; caps in `server.cfg`.
* Done when: selftest `chronicle` (achievements trigger once, perks apply and respect caps, persistence across save/reload and across defeat / resettle); screenshot of the window.

### T27b Faction personalities and optional decrees (size M)

* Each NPC faction gets a personality from the world seed (aggressive raider, trader, isolationist, opportunist) that changes its patrol, siege, trade and demand behaviour (data table, `server.cfg` weights) so two seeds play differently.
* Optional **decrees**: modifier sets (e.g. "Long Winter": scarcer food, richer rewards) chosen at world start, listed in `server.cfg`, shown in the HUD, reflected in Chronicle.
* Done when: selftest `personalities` (same seed -> same personalities, different seeds differ, behaviour parameters differ measurably, decree modifiers apply and persist).

## World map (T21)

Code: `src/ui/BigMap.*` (opened by clicking the minimap or `N`), `src/rendering/Minimap.*`, `src/rendering/MapOverlay.*` (icons shared by both), `rendering/Shaders.*`, `tools/gen_ui_maps.py`, `client/Settings.hpp`.

### T21a Parchment art generator and style tokens (size M)

* `tools/gen_map_art.py` generates under `assets/ui/map/`: parchment paper (grain, vignette, tileable), ink motif atlases for forests, mountains, hills / plains, deserts, wetlands (only if the `Biome` enum has them, else closest biome), coast, wave strips, and the icon set (town, outpost, harbour, ship, troop group, skull, swords, resource, anchor, cluster).
  Warm brown paper, near-black ink, light shading. Style tokens (colors, line widths, dash styles) in one header.
* Done when: assets are generated deterministically by one script run and loaded by a small client test hook that draws the atlas (screenshot).

### T21b Parchment terrain and animated ocean (size M)

* Big map terrain as ink motifs placed deterministically with `hash2(x, y, seed)`; static layers cached per chunk in render textures (rebuilt only when explored tiles change); ocean animated by a phase offset (pre-baked wave strips or a small shader loaded like `Shaders.cpp`). No flat biome fills, no single-pixel dots.
* Done when: screenshots of the big map at three zoom levels; a frame-time log (`BW_STUTTER=1`) shows no spikes beyond a recorded threshold when panning.

### T21c Icons, borders and routes (size M)

* Ink icons for towns, outposts, harbours, ships and troop groups; pawns omitted or clustered with a count. Claimed player / clan / hostile territory as dashed ink borders with distinct dash styles. Active convoys, caravans and support patrols as dashed travel lines with arrowheads (data already replicated: convoy / patrol paths, `S_ShipInfo`; check `MapOverlay`).
* Done when: screenshot with a town, a ship route, a convoy and three territory types.

### T21d Markers, minimap redesign and overlap fix (size M)

* Place, label and remove hand-drawn markers (skull, swords, resource, anchor pin) by right-click or compact ink toolbar; personal markers persist locally (client file next to `settings.txt`); tactical markers shared with clan members via appended protocol messages (server relays to the clan, length-capped labels, rate-limited, validated).
* Minimap: miniature parchment with ink border, same icon set, no HUD overlap (fixes the known gap at UI scale 2 in small windows), smooth interaction.
* Done when: selftest `markers` (rate limit, length cap, relay only to clan); screenshots of the big map and of the minimap with the build menu and chat open at scale 1 and 2.

## Polish (T16)

### T16a Rain and ocean shader (size S)

* Dynamic rain with a shader across biomes (lower in deserts), weather derived client-side from a replicated seed or server-driven (decide, log it); refine `assets/shaders/water.fs`. Code: `WaterRenderer`, `PostFx`, new `rendering/WeatherRenderer`.
* Done when: screenshots in a forest and a desert biome; frame time unchanged within a recorded margin.

### T16b Rebindable keys (size M)

* Pause-menu keybind screen: customizable, saved bindings (hard-coded `IsKey*` in `src/client/Input.cpp` move into an action table persisted in `settings.txt`; conflicts detected; reset to defaults).
* Done when: scripted run rebinds a key and the new key triggers the action (log assertion); screenshot of the screen at scale 1 and 2. (The central hint system moved to T26.)

## Mid-game activities (T22, Calm-phase content)

### T22a NPC contracts and speed-up tokens (size M)

* Server system + persisted `Contract` state: escort merchant carts, clear nearby bandit camps, deliver time-sensitive supply orders. Rewards: speed-up tokens (per-profile counter consumed by a context-panel action that shortens the running `BJob`), gold or rare resources (prefer counters over a new `Res` enumerator, which sizes many arrays).
  UI: contract list in the NPC town context panel + small HUD tracker. Reuse existing units / sprites (carts = pawns / convoy, bandits = black faction). Contracts appear only in Calm / Aftermath (asks the Pacing Director).
* Done when: selftest `contracts` (accept, complete, fail by timeout without punishment, reward granted once, persistence, tokens shorten a job).

### T22b World discoveries (size S)

* Hidden treasure caches, ancient ruins and rare wildlife herds (tinted sheep) rewarding scouting and hunting; spawn density in `server.cfg`; persisted, shown on the map once found.
* Done when: selftest `discoveries` (density from config, found once, reward, persistence).

### T22c Sea events (size M, needs T14 / T15 / T19)

* Salvageable shipwrecks and flotsam, wandering pirate patrol ships that drop loot, fishing shoals for food (a ship works the shoal), small remote islands (`genBiome` already makes rare islands) with rich gold / rare ore veins (new feature type, existing gold sprites) rewarding distant outposts and automated ferry routes.
  All spawns / timers / rewards server-side, configurable, persisted, on the world map; pirate ships spawn only through the Pacing Director.
* Done when: selftest `sea-events` (spawn, salvage, shoal depletion and respawn, pirate loot, persistence).

## Tactical command and enemy ecology (T23)

**Asset gate.** The design is limited to Pixel Frog's Tiny Swords (64x64 grid, 10 fps base animations, 5 faction colors, elevation, stairs, water, bushes / rocks, VFX, UI). `assets/` has only the Free Pack (units and buildings in 5 colors, terrain, particles: Dust x2, Fire x3, Explosion x2,
Water Splash, UI: banners, bars, buttons, cursors, icons, ribbons, swords, wood table). **The Enemy Pack (Stone Turtle, Boar, Minotaur, Boompuff, Torch Goblin, Slingshot Gnome, Hex Goblin, Giant Bat, Bumblebee, Imp ...) is not in the repo.** T23c is blocked until the owner adds it; never invent sprites.

### T23a Command Focus (size M)

* Slow time to ~15% while clicking / dragging units or holding Space, with command lines, range rings and target previews. Decision made: client-side presentation slow-motion; orders are previewed and buffered, the server tick never changes (see Decisions in `TODO.md`).
* Done when: screenshot with rings and previews; selftest `focus-buffer` (buffered orders are sent in order with correct sequence numbers).

### T23b Anti-punishment flow (size M)

* No permanent wipe of hours of progress from one mistake: defeated squads retreat wounded to camp and are healed by Monks / Meat; partial clears grant permanent meta-resources, blueprint unlocks or tactical perks (persisted per profile, linked to Chronicle).
* Done when: selftest `retreat` (a lost fight leaves wounded survivors at camp, healing works, partial-clear reward granted once).

### T23d Stealth and ambush crits (size M)

* The 4 animated Bushes and elevation cliffs give stealth: units inside bushes or behind cliffs are semi-transparent with a UI indicator (server-side `Stealth` visibility rules in `Replication`, not only a client fade); sneaking Archers onto high ground or flanking behind patrol cones (FOV cones for `PatrolSystem`) trigger **Ambush Crits** (bonus damage, dust burst, hitstop).
  Offensive raids on faction camps (pick off lookouts, burn supply huts, rescue captured Pawns, extract with loot) and base defense with traps built only from existing VFX / buildings.
* Done when: selftest `stealth` (an enemy cannot see a stealthed unit outside range; the server does not replicate hidden units to enemies; ambush crit bonus applies once).

### T23e Micro-tasks and Golden Opportunities (size M)

* Keep pawns useful mid-fight (mine a gleaming gold rock before enemies arrive, herd / harvest sheep for healing meat, repair a burning tower) and optional time-limited events with a countdown (intercept a fleeing gold-carrier Imp within 25 s, hold a cliff shrine 20 s for a permanent squad blessing, extinguish a burning barracks before expiry for double wood).
  Failure never kills the player; success pays out. Server-side `Opportunity` entities, persisted, spawned only in Calm / Aftermath via the Pacing Director.
* Done when: selftest `opportunities` (spawn, success, expiry without penalty, persistence).

### T23f Game juice II (size M)

* Builds on T28a: hitstop / freeze frames (40-80 ms) on heavy hits, crits, kills and parries, directional knockback, squash-and-stretch on 10 fps sprites, escalating pitch chimes, magnetic resource pickups flying into the banner, stretchable Ribbon combo callouts ("CLIFF DROP! +50 Gold", "AMBUSH!") in 5 colors, existing VFX combined.
  Driven by server fx events; client presentation only; settings toggle for reduced effects.
* Done when: screenshots of callouts and pickups; `BW_AUDIOLOG` shows escalating cue pitch in a scripted combo; reduced-effects toggle disables hitstop.

### T23c Enemy ecology ("learn and counter") - blocked (needs Enemy Pack)

When unblocked, split into at least three rows: (1) archetype framework (`KindDef` entries + `Charger`, `Volatile`, `Igniter`, `Ranged`, `Disruptor` components + systems; every threat with a visible telegraph: wind-up flash, ground danger zone, icon badge; stances Lancers *Hold*, Warriors *Guard / Bash*),
(2) armored chargers countered by Lancers in Hold on narrow stairs, (3) volatile / ranged (Torch Goblins ignite via `FireSystem`, Boompuffs explode on death, counters: elevated archers, shove with Bash, knock off cliffs into water) and disruptors / swarmers (fast flyers countered by Guard-stance Warriors shielding Monks). Raids use them through the Pacing Director.

## T17 Cleanup (size S)

* Remove leftover dead code paths, finish the structure leftovers listed under "Known gaps" in `TODO.md`, final warning pass, a style pass over any file earlier tasks did not cover, doc consistency pass over `CLAUDE.md` / `ARCHITECTURE.md` / `TODO.md`, prune Decisions that no longer matter.
