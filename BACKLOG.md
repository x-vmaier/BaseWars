# Backlog: specs of tasks that are not current yet

Open this file only when a task becomes the current one. Then **move its spec into `TODO.md`** ("Current task specs") and **delete it here** (part of the close-out: this file must only hold tasks that are not done).
Roadmap order, status and the task rules live in `TODO.md`. Task ids, "Done when" criteria and code pointers below are unchanged from the original plan.

## Naval (T14)


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

## Living NPCs (T29, owner idea)

Goal: the town feels alive. Nobody stands around waiting for the player; every pawn and troop has a visible purpose. Builds on T15a (orders beat auto-behaviour) and T15d (roles). Code: new `components/Brain.hpp`, `systems/BrainSystem.*` (replaces the scattered idle logic in `HuntSystem`, `DefenseSystem::idleArmy`, firefighting), `Replication`, `SaveSystem`.

### T29a Brain, personality and visible objective (size M)

* `Brain` component (persisted): `state` (table `BRAIN_STATES[]`: Idle, Work, Haul, Hunt, Explore, Escort, Spar, Rest, Forced), `task` chosen by the AI, `forced` (the player's order), and a `Personality` of weights per state (hunt, explore, build, haul, fight / spar, social) rolled from the unit uid + world seed at creation, so it is stable across save / reload. Pawns strongly favour Work / Haul / Hunt and explore only rarely (weight ~5%); troops favour Escort / Explore / Spar. Weights and rates in `server.cfg`.
* One decision step per unit every ~2 s (staggered by uid, no per-tick scans): score = personality weight x situation (sheep nearby, open jobs, hostile territory, fog nearby). Existing systems become the *executors* of a state (hunting stays in `HuntSystem`, building in `ConstructionSystem`); the brain only picks which one runs. A manual order (T15a) always wins and sets `forced`.
* Hover tooltip over an own / allied NPC shows its objective ("Hunting sheep", "Exploring the north", "Sparring", "Escorting a pawn") plus personality adjectives ("Loves hunting"). The state byte rides the `S_State` unit record (appended field inside the snapshot chunk, same rule as `prog`); the texts live in one client table.
* Done when: selftest `brain` (personality is deterministic for uid + seed and differs between units; idle pawns pick Hunt when sheep are near and Explore only rarely over a long run; a forced order overrides the AI and the AI resumes afterwards; state survives save / reload; no unit stays in a state without an exit); screenshot of the tooltip.

### T29b Task assignment, mood and productivity (size M, needs T29a)

* Clicking an own pawn opens a small task menu (context panel): Auto, Build / Repair, Clear, Haul, Hunt, Explore, Stay. This is the player's way to set `forced` tasks and is the UI of the T15d roles (merge: a role is a sticky forced task with a priority).
* Mood 0..100 per NPC (persisted, shown as an icon above the unit only when low / high). Every forced task the personality dislikes adds a grudge, liked work raises mood, free AI time recovers it slowly. Low mood scales productivity (gathering, build speed, hauling) from 1.0 down to a floor (`mood_min_productivity`, default 0.6). **Fairness:** no desertion and no death spiral; the floor and the slow recovery are the cap. A mood summary in the Town Hall (average, who is unhappy and why).
* Done when: selftest `mood` (disliked forced tasks lower mood over time and productivity follows, liked tasks raise it, floor holds, recovery after releasing, persistence); screenshot of the menu and a mood icon.

### T29c Troop AI: escort, explore, spar (size M, needs T29a; T20a recommended)

* Idle troops get purpose: **Escort** (accompany a hauling / building / hunting pawn or a convoy and engage threats near it), **Explore** (a bounded loop into unrevealed fog, much farther than pawns, then back), **Spar** (below), **Patrol** the base. Never leaves `explore_leash_tiles` (grows with Town Hall level), avoids hostile territory and raid zones, and returns when a Warning starts. Exploring reveals fog and feeds discoveries (T22b).
* **Spar:** two idle Warriors / Lancers near the Archery or Town Hall face each other and play their attack animations without dealing damage (a pair with a shared timer and a partner uid; both break off on any threat or order). Cosmetic by default; an optional small, capped training bonus sits behind a `server.cfg` flag (default 0). No new sprites.
* Done when: selftest `troop-ai` (idle troops escort a pawn within a radius, explore only inside the leash and come back, sparring pairs deal no damage, all states end on a threat or a manual order, invariants hold with partner uids dangling); screenshot of two sparring warriors.

## Mine veins (T30, owner idea)

### T30 One Mine, underground veins decide the yield (size M)

* The building is just **Mine** (gold-only wording disappears from texts, `siteNote`, tooltips; `KT_GoldBoosted` becomes a generic "vein-driven" tag). Output is a mix of stone and gold (more ores later through the same table) decided by the vein layer under its footprint.
* **Vein layer:** a hidden per-tile deposit field generated from the world seed (`WorldGen`, noise per resource; the biome sets the base odds: Highlands / Desert richer, Plains / Flowers poor, ocean none). A region without gold only ever yields stone; a random gold vein under the footprint multiplies gold output; rich stone veins exist too. Existing visible gold deposits stay as the "surface hint" and become part of the same field.
* **Fairness / readability:** the player is never surprised after paying. The placement ghost shows the expected yield ("Stone ++, Gold +") sampled from the footprint; an optional cheap **Prospecting** action (a pawn walks there, short timer) reveals veins in a radius as faint ink marks on the map. The yield is shown in the Mine's context panel and tooltip.
* Saves: the field is derived from the seed, so only mine state is saved; if any new persisted data is needed, bump `SAVE_MAGIC` (ask the owner first, irreversible).
* Done when: selftest `veins` (same seed -> same field; a gold-free footprint produces only stone; a gold-rich footprint produces measurably more gold than a poor one; yields match the ghost's prediction; save / reload); screenshots of the ghost with yield and of the prospected map.

## Missions: story mode (T31, owner idea)

Goal: a second way to play next to the sandbox: linear, hand-designed, challenging levels played alone or with friends, unlocked in sequence along a path on a parchment map, rewarded by stars. Needs T21a/b (parchment art), T20a/b (combat roles, garrisons), T14 (harbour, ships), T25b / T27a (rewards, persistence). Enemies and outposts use the existing faction units / buildings (the Enemy Pack is still missing: never invent sprites).

### T31a Mission instance framework (size M)

* Decide and log: missions run in the **same server process** as small isolated arenas placed in an unused part of the map grid (outside the sandbox world, hidden by an `Instance` component and by the replication / fog filter), so pathfinding, combat and saves keep working. Only players inside the instance see or interact with it. The home town keeps simulating, but the player's Pacing Director pauses as if offline while away (with a warning before leaving).
* `MissionRun` (id, level, players, start time, state): created from a lobby (solo or invited friends / clan), everything removed on success / failure / abandon / timeout (**stuck-state rule:** every run has a hard time limit and an abandon button; troops are returned). Disconnect = leave the run; reconnect rejoins while it lives.
* Troops on a mission are marked `Away` (not counted for defense). Fallen troops are lost except a configurable refund share (`mission_loss_refund_pct`, default 50; log the decision, ask the owner if it changes). Survivors return alive (ties into T23b).
* Done when: selftest `mission-instance` (a run isolates its entities from the sandbox and from other runs, two players share one run, abandon / timeout / disconnect clean up with no dangling references, troops return, pacing pauses and resumes, invariants).

### T31b Level format, collision walls, first level (size M, needs T31a)

* Levels are hand-authored data: a text grid per level under `assets/missions/` (tiles: ground, forest wall, ocean, cliff, path, markers for spawn, enemy camp, outpost, chest, checkpoint, exit) read by a small loader + `common/Missions.hpp` table (name, order, requirements, objectives, rewards). Linear, **not open world**: the walkable corridor is surrounded by impassable forest / water / cliffs; fog is revealed progressively along the path. Validator `tools/check_missions.py` (every level has a connected path from start to exit).
* Level 1 is the template: a path with two enemy camps and a small outpost to destroy, a hidden chest as the optional extra, an exit zone. Enemies are faction units spawned by a `MissionSpawner` (groups placed at design time, activated by proximity, telegraphed).
* Done when: selftest `mission-level` (all levels load, start is connected to the exit, enemies spawn where authored, walls block movement, the exit completes the objective); screenshot of level 1.

### T31c Objectives, stars and rewards (size M, needs T31b)

* Each level has **required** objectives (finish = 1 star) plus optional ones and performance rules: a second star for the optional objective (chest / second outpost), a third for performance (no troop lost, or under a par time). Thresholds are per-level data. Stars and best result are persisted per SteamID (`MissionProgress` beside Chronicle, T27a; new persisted data -> ask before bumping `SAVE_MAGIC`).
* Rewards scale with stars (resources, speed-up tokens T22a, a small capped perk T27a); repeat clears only pay the difference to the best stars, so there is no farming. Co-op: rewards per player, the result counts for everyone in the run, enemy strength scales with the player count (data-driven multiplier).
* Done when: selftest `mission-stars` (star rules per level, rewards paid once and only the delta on replays, persistence, co-op scaling, a failed run pays nothing beyond lost troops).

### T31d Mission map UI and loadout (size M, needs T21a/b, T31c)

* A **Missions** tab in the Town Hall window (and the Archery): a parchment map in the style of the big world map with a hand-drawn curvy ink path; each level is a circle on the path. Locked = faint, available = pulsing, finished = filled ink with 1-3 stars beneath it. Only the first is open at the start; clearing a level unlocks the next. Hover shows name, objectives, best stars and rewards.
* Starting a mission opens the **loadout**: pick how many of each troop type to bring (limited by army size, shows what stays home and the defense warning), invite friends (lobby with ready state), start. Missions that cross the sea ask for how many ships and which troops go on which ship (reuses `Ship` capacity and crew rules, T14a).
* Harbour unlock: building a Harbour unlocks the sea chapter of the path; until then those circles show an anchor and "Needs a Harbour".
* Done when: screenshots of the map at scale 1 and 2 (locked / open / finished / stars, harbour-locked chapter) and of the loadout; selftest `mission-unlock` (order of unlocks, harbour chapter gate, loadout limits validated server-side, malformed loadout rejected).

### T31e Cloud transition and result screen (size S, needs T31d, client only)

* Start of a mission: clouds drift in from the right (sprites in `assets/Terrain/Decorations/Clouds`), a loading icon sits in the centre over a dense cloud backdrop that scrolls right to left in 3-4 parallax layers (back layers slower than front layers) to convey travel to a far place. When the level is loaded the clouds sweep out to the left and the level map is revealed; the level starts after the reveal (input locked meanwhile, with a hard time cap so it cannot hang). The same transition plays in reverse on the way home.
* Result screen: stars fill one by one with a chime (T28a), rewards listed with `ui::drawCosts`.
* Done when: scripted `BW_SHOT` frames of the transition at start, mid-loading and reveal; the transition ends by itself under a forced slow load (log assertion).

### Refinements suggested for the mission mode (decide when the tasks become current)

* **Checkpoints:** a checkpoint flag halfway lets a failed run restart there once per run (keeps the challenge without lost hours, fits T23b).
* **Mid-level choices:** a fork (storm the gate or sneak around, fits stealth T23d) and a prisoner / merchant to rescue for a bonus unit.
* **Mastery:** a rotating weekly challenge mission (fixed seed + modifier, T27b decrees) with a times board among friends; a "hard" replay modifier for a fourth ink seal.
* **Story glue:** one short parchment text card per level (ink drawing + 2 lines) so the path feels like a journey; faction personalities (T27b) decide who the enemy is.
* **Commander perks:** stars unlock a small permanent, capped choice (e.g. +1 troop slot on ships), so replaying for stars has a purpose.

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
