# Status and remaining work

Branch `feature/prompt-implementation`. Everything in the Done table is implemented and committed.
**Do the first Roadmap row with status `todo`** (row order counts). One coherent task per session (see `CLAUDE.md`).
Only the current task is specified in detail below. Every later task has its full spec in `BACKLOG.md`: when a task becomes current, move its spec here (and delete it from `BACKLOG.md`).

## Design goals (every task is judged against these)

1. **Rhythm of stress and relief.** Calm (build, explore, trade, 6-10 min) -> Warning (60-90 s telegraph) -> Siege -> Aftermath (rewards, repair, guaranteed relief). Never two stress peaks back to back. Owned by the Pacing Director (T25a/b); content tasks add things to do in Calm and to survive in Siege, never their own random threat timers.
2. **Always a next goal.** A short (< 2 min), medium (< 15 min) and long (hours) goal are always visible (T26, T27).
3. **Fair and forgiving.** Every threat is telegraphed and counterable. One mistake never erases hours of progress (T18b, T20c, T23b).
4. **Feels great.** Every player action gets feedback within ~100 ms: animation, effect, sound, number (T28a, T23f). Controls never feel laggy (T19).
5. **Replayable.** Seed-driven worlds, faction personalities, capped meta-progression, optional modifiers (T27a/b).
6. **Robust.** No crashes, desyncs, dangling references or stuck states. Every task adds a selftest; invariants run in every selftest (T24a).
7. **Readable.** One screen never shows more than the player can parse; no overlapping UI at scale 1 and 2.

## Task rules (from the owner)

* The server is authoritative. The client renders, predicts (from T19c) and sends commands.
* New buildings, units, NPCs, ships, enemies go through the registries (`KindDefs.hpp`, `KindFactories.cpp`, `rendering/`); no `switch` on kinds. See "Extending the game" in `ARCHITECTURE.md`.
* Tuning values go in `ServerConfig` + `config/server.cfg` (documented, defaults reproduce the described behaviour); shared rules in `Progression.hpp`.
* New persistent state is written/read in `SaveSystem` and bumps `SAVE_MAGIC`. Anything that can dangle gets an invariant in `World::checkInvariants()`.
* Protocol is append-only; T19a is the one planned break.
* Every task adds at least one concise selftest (core assertions + invariants, no boilerplate), documents new env hooks in `ARCHITECTURE.md`, and lists in "only verified by reading" whatever could not be run.
  Purely visual / UI / shader / audio tasks (e.g. T18c, T16a, T28a rendering parts) may skip the new C++ selftest if existing selftests stay green and a build/screenshot check passes.
* Token economy: intermediate steps run only an incremental release build and the one relevant selftest; the full `tools/check.sh` (asan + all selftests + format) runs once before the final commit.
* Art: only the Tiny Swords assets in `assets/`, plus generated map art (T21a) and generated sounds (T28a).
* Every task leaves saves loadable (or cleanly migrated) and the game shippable.

## Done

| Task | Summary |
| --- | --- |
| T1 | `cmake/` modules, C++23, compile commands, mandatory Steamworks SDK with runtime copy, only `server` + `client` targets |
| T2 | Server split into `World` + 23 system classes + components; client split into App/Session/Input/... + `rendering/` + `ui/`; no `.inc`, no monolith files |
| T3 | ENet, P2P and bots removed; Steam GameServer + Networking Sockets (IP + SDR P2P). Direct-IP connect verified |
| T4 | `config/server.cfg` (biomes, density, towns, hostile %, raids, patrols, sheep, instant debug timers) |
| T5 | Obstacles hoverable and right-click clearable; pawn walks, timer runs on site; placement over obstacles queues clear jobs |
| T6 | Faction patrols wander/siege hostile towns, faction wars, map-wide patrol groups; client keeps last-seen NPC buildings dimmed |
| T7 | Convoy/haul loop fixed, fire attribution fixes |
| T8 | Pawn-driven construction/upgrade/repair, timers only run on site |
| T9 | Damaged-building efficiency, food costs on actions |
| T10 | Level cap 10, cost/time curves, Town Hall gating (`Progression.hpp`) |
| T11 | Donation cooldown, shifting faction demand, diminishing gift returns |
| T12 | `ui::CostRow` everywhere; tabbed Town Hall window with per-type defense quotas |
| T13 | Biome label, minimap trees, chat/build menu layout, high-contrast bars |
| T13b | Maintainability refactor: `.clang-format`, draw queue, kind registry (`KindDefs.hpp`), Doxygen headers. A new building + troop needs 2 files, 0 `switch` edits |
| T24a | `server --selftest` (boot, saveload, economy, raid, net-garbage, invariants) on a shared `Sim`; `World::checkInvariants()`; asan preset, `/W4` warnings policy, `tools/check.sh`, `tools/shot.sh`; saves now byte-stable; fixed OOB kind in `C_UpgradeUnit` |
| T18a | Identity: verified SteamID is the only owner key (Steam auth tickets, `--local` dev ids), names resolved client-side from Steam, no name editing, SAVE_MAGIC BWS9, selftest `identity` |
| T18b | Defeat flow: burning `K_Ruin` (shared `Ruin` component, wall-clock expiry), defeat banner naming the conqueror, Resettle -> offered site (friend > clan/ally > other) with Accept / next site, commands blocked meanwhile, offer timeout auto-accepts, SAVE_MAGIC BW10, selftest `defeat` |
| T18c | Tree canopy fade: pines/oaks fade to ~38% alpha (4/s) while an own unit stands behind the canopy box; per-tree alpha map in `FeatureRenderer`, picking/draw order untouched |
| T19a | Protocol v2: version in hello / welcome, command seq envelope, tick-numbered unreliable S_State chunks (< 1100 B, whole-tick removal rule), `snapshot_interval_ticks`, `BW_NETLAG` / `BW_NETLOSS`, selftest `net-snapshot` |
| T19b | Entity interpolation: per-entity `PosBuffer`, `InterpClock` render clock (delay 2 snapshot intervals), clamped extrapolation, `BW_STUTTER` metric, selftest `interp` |
| T19c | Command prediction: pending queue of move / attack-move orders, immediate marker + local movement start (bounded offset that fades), `S_CmdReject` rollback (red cross), selftest `net-predict` |
| T25a | Pacing Director v1: per-player Calm -> Warning -> Siege -> Aftermath (`PacingSystem`), raids only from the director (sized from a defense score, adaptive factor, caps), survivor reward, `S_Pacing` + edge arrow / chat telegraph (the phase badge was removed again, see Decisions), SAVE_MAGIC BW11, selftest `pacing` |
| T26 | Objectives: table + event bus, short / medium / long chains with one-time rewards, skipped steps resolve, timeout exit, `S_Objectives`, HUD tracker + completion banner + build-menu / world pointer, central hint table (`ui/Hints`) with settings toggle, SAVE_MAGIC BW12, selftest `objectives` |
| T28a | Audio + feedback: `audio::` cue table (15 generated samples, per-cue cooldown / pitch variance / 3 voices, positional falloff, no device = silent), `BW_AUDIOLOG`, master / effects sliders in the pause menu, white hit-flash shader, floating damage numbers, camera shake on building destruction, resource-bar pulse, button hover / press response |

## Roadmap (do in this order)

Status: `todo`, `blocked: <reason>` (skip it). Finished rows move to Done. Specs of rows after the current one are in `BACKLOG.md`.

| # | Task | Size | Status | Why here |
| --- | --- | --- | --- | --- |
| 11 | **T14a** Ships as registry kinds, harbour production, crews | M | todo | Naval foundation |
| 12 | **T14b** Wind, ship combat rules | S | todo | Needs T14a |
| 13 | **T14c** Harbour window, multi-modal routing | M | todo | Needs T14a |
| 14 | **T15a** Auto-defense vs manual orders | S | todo | Unit behaviour correctness |
| 15 | **T15b** Reliable caravans, relocation/donation/respawn cleanup | M | todo | Needs T14 |
| 16 | **T15c** Clear Zone | S | todo | Pawn anti-tedium |
| 17 | **T15d** Pawn roles and job priorities | M | todo | Needs T15c |
| 18 | **T20a** Combat triangle: roles, armor, damage matrix | M | todo | Data model for raids |
| 19 | **T20b** Garrisons, walls, watchtowers | M | todo | Needs T20a |
| 20 | **T20c** Offline protection and peace shield | M | todo | Fairness in a persistent world |
| 21 | **T20d** Scorched ruins and cheap rebuild | S | todo | Reuses `Ruin` |
| 22 | **T25b** Pacing Director v2 (role-based raids, rewards) | S | todo | Needs T20a |
| 23 | **T27a** Chronicle and legacy perks | M | todo | Reason to start another town |
| 24 | **T27b** Faction personalities, optional decrees | M | todo | Every world plays differently |
| 25 | **T21a** Parchment map art generator, style tokens | M | todo | Map redesign step 1 |
| 26 | **T21b** Parchment terrain + animated ocean | M | todo | Needs T21a |
| 27 | **T21c** Map icons, borders, routes | M | todo | Needs T14, T15, T21b |
| 28 | **T21d** Map markers, minimap redesign, overlap fix | M | todo | Needs T21c |
| 29 | **T16a** Rain and ocean shader | S | todo | Polish |
| 30 | **T16b** Rebindable keys | M | todo | Polish |
| 31 | **T22a** NPC contracts, speed-up tokens | M | todo | Calm-phase activity |
| 32 | **T22b** World discoveries | S | todo | Calm-phase activity |
| 33 | **T22c** Sea events | M | todo | Needs T14, T15, T19 |
| 34 | **T23a** Command Focus (slow-motion tactics) | M | todo | Tactical feel |
| 35 | **T23b** Anti-punishment flow | M | todo | Fairness after defeats |
| 36 | **T23d** Stealth and ambush crits | M | todo | Existing assets |
| 37 | **T23e** Micro-tasks, Golden Opportunities | M | todo | Existing assets |
| 38 | **T23f** Game juice II | M | todo | Extends T28a |
| 39 | **T23c** Enemy ecology | M | blocked: needs owner-supplied Enemy Pack in `assets/`; split into 2-3 rows when unblocked | Largest content block |
| 40 | **T17** Cleanup | S | todo | Always last |

**Manual playtest checkpoints (owner, ~10 min):** after T19c, after T28a, after T20b, after T21d. Agents cannot fully judge feel, prediction or map art headless.

## Decisions log (append: `Txx: decision - reason`)

* T18a: names are never stored or trusted on the server. The server replicates SteamIDs; each client resolves persona names itself (`GetFriendPersonaName`, `RequestUserInformation` for strangers). `--local` + `BW_DEV_ID`.
* T18a: a second login of an online SteamID replaces the old session (identity is verified, avoids lock-out after a dropped link).
* T18a: server-side texts carry `\x01<id>\x02` name tokens; chat is control-char stripped so tokens cannot be forged. Unknown persona names show `Player<last digits>` until Steam delivers them.
* T18a: old uuid-only profiles are discarded (moved aside), like old save formats; no migration code.
* T23a: tactical slow-motion is client-side presentation only (orders previewed and buffered; the server tick never changes).
* T28a: audio is generated by `tools/gen_sounds.py` (CC0 / self-made); music is out of scope until the owner supplies licensed tracks.
* "Reseddle" in the owner's text is a typo for "Resettle".
* T18b: the ruin is one neutral `K_Ruin` building (4x3, the old Town Hall footprint) with a `Ruin{expiresAt, victim, burning}` component; the victim's other buildings and units vanish at once, as before - only the hall site stays visible.
* T18b: the offer timeout uses `world.clock` and only counts while the player is online (reset on login); the ruin expiry is wall-clock (`defeat_ruin_hours`). A defeated player whose ruin has burnt down is offered a site automatically, which then times out - so every state has an exit.
* T18b: clan members keep the old homeless flow (place the free hall near the clan) - the spec only covers players without a clan hall; old data without a hall and without defeat state enters the resettle flow without a ruin.
* T18c: occlusion = own unit foot above the tree foot and inside a fixed canopy box (no pixel test); fade state lives only in the renderer.
* T19a: the server loop was already a fixed 20 Hz step with packets drained between ticks, so `TICK_HZ` stays a constant (tick timers depend on it); only the snapshot interval is a `server.cfg` value.
* T19a: snapshots are full state (no deltas) in independent chunks; removal of unseen entities waits for a whole tick - simpler and loss-tolerant, at the cost of ~N packets per snapshot in huge views (delta / interest culling later if needed).
* T19a: sequence numbers are enforced strictly increasing (older / equal seq dropped) - the transport is ordered and reliable for commands, so a violation means a replay or bug.
* T19a: fake lag / loss uses Steam's `FakePacketLag_Send` / `FakePacketLoss_Send` globals (env), the selftest drops chunks itself through `SteamServerNet::tap`.
* T18b: new messages are appended after `S_Econ` (`C_Resettle`, `C_ResettleAccept`, `S_Defeat`) to keep the protocol append-only.

* T25a: the director state is per player (`BaseProf::pacing`, saved in the base section) instead of one `registry.ctx()` singleton - towns are per player and timers must pause per owner while offline.
* T25a: timers are remaining seconds that only count down while the owner is online (offline pause), so they need no wall-clock expiry and survive restarts.
* T25a: patrols no longer siege player towns (`PatrolSystem::pickSiege` skips them) - the only threat against players is the director's raid; NPC-vs-NPC sieges are unchanged.
* T25a: raid size = round((2 + .35 x score) x factor), score = 2 x hall level + .5 x buildings + army units; capped by `pacing_max_raiders` and `pacing_max_base_share` x (buildings + troops). Factor x.7 after > 25% asset loss, x1.1 after <= 5%, clamped .5..`pacing_max_raid_factor`. Size is fixed when the warning starts, so preparing never makes the raid bigger.
* T25a: `raid_*` config keys were replaced by `pacing_*`; a new town gets `max(first_raid_grace, calm)` of calm. No hostile faction with a town in range = the director just stays calm.
* T25a (owner follow-up): the HUD shows no phase badge, timer or tension; only an announced raid is telegraphed (arrow, chat text) because of the fairness rule. Director timings are randomised so cycles never repeat.
* Fonts (owner follow-up): glyph atlases are now built per on-screen pixel size instead of one 80 px atlas scaled down.
* T19c: only move / attack-move are predicted (marker + local movement start); build / train / etc. stay server-confirmed - the existing pending-site ghosts already give instant feedback and prediction of resource spending would risk wrong state.
* T19c: rejection is a new reliable `S_CmdReject{seq}` (append-only, no version bump); the server sends it when an order moved no unit or a command was dropped while defeated. Malformed packets get none.
* T19c: local movement start is a visual offset (2 tiles/s, max 0.6 tile, fades at 6/s), not a simulated unit - no client pathfinding, so nothing to reconcile.

* T26: the three chains are short (first-hour steps) / medium / long with one active goal each - the spec's 'survive first warning' and 'donation' sit in the medium chain so a slow raid never blocks the quick short steps.
* T26: progress uses lifetime counters (gather / train / donate) or live state (build / level / survive) so a step the player already satisfied resolves in one pass; an objective open for `objective_timeout_min` online minutes (20) is skipped without reward - the stuck-state exit.
* T26: SAVE_MAGIC bumped to BW12 (objective state appended after pacing per player); old BW11 saves are moved aside like earlier formats.

* T28a: sounds are driven by what the client already sees (fx events, drops, hp drops, new / destroyed entities, S_Pacing warning, objective banner) - no protocol change; nothing sounds in the first 2.5 s after the welcome so the first snapshot does not play a burst.
* T28a: hit flash is a sprite mode (`SM_Flash`, `flash.fs`) fed by `ClientWorld::hitAt` (net id -> last hp loss); damage numbers and the hit cue are throttled to one per entity per 0.35 s so burning ticks do not spam.
* T28a: the pause panel grew to 12x13 tiles (two volume rows); `assets/ui/pause_menu.tmj` was regenerated from `tools/gen_ui_maps.py`.
* T19b: no protocol change - velocity / facing are derived from the buffered samples (`aim` still drives attack facing), so `PROTOCOL_VERSION` stays 2.
* T19b: the render clock slews at most 10% towards its target instead of jumping, so jitter spikes never make units jump; delay = max(150 ms, 2 x smallest recent snapshot interval).

## Current task specs

### T14a Ships as registry kinds, harbour production, crews (size M)

* Replace `SHIP_TROOP_CAP` / `SHIP_RES_CAP` in `Shared.hpp` with ship entries in `KindDefs.hpp` (troop-focused and cargo-focused types, larger tiers at higher harbour levels with own speed / troop / cargo capacity).
* Harbours produce standalone ships (queue + cost rows, `TrainingSystem` queue + `canTrain`); a ship cannot move without crew aboard; timed, paid ship upgrades.
* Done when: selftest `ships` (train ship, crew, move only with crew, upgrade costs and times, save/reload keeps ships and crews); screenshot of a ship at sea.
* Code: `NavalSystem.*`, `components/Naval.hpp`, `TrainingSystem`, `ShipRenderer.*`, `Progression.hpp`. Replicated ship state rides `S_State` (`Info.level`=tier, `prog`=troops, `stock`=cargo) and `S_ShipInfo`.

(When T14a is done, replace this section with the spec of the next `todo` row from `BACKLOG.md` and delete that spec there.)

## Known gaps / follow-ups

* **Per-task gate notes, "only verified by reading" lists and gaps of T18a-T28a** (still open verification debt) live in `HISTORY.md`. Read the entry of a task before touching its area; new tasks append theirs there.

* **T24a only verified by reading:** `tools/shot.sh` (never run), the non-MSVC branches of `cmake/Compiler.cmake` (UBSan on gcc/clang), the clang-format step of `check.sh` on Linux. asan on MSVC has no UBSan. The asan `raid` scenario (700 simulated seconds) takes 15+ minutes: the owner told us not to wait for it, so the asan runs of `raid` and `invariants` were never completed (asan `boot`, `saveload`, `economy`, `net-garbage` passed). Consider shortening `raid` under asan.
* **Observed by `raid`:** a forced raid kills and respawns the player repeatedly (7 defeats in 700 s: raiders keep targeting the new hall for 600 s). Since T18b a defeated player stays defeated (banner, then resettle) so raiders no longer chain-kill a respawned hall; T25a still owns real relief after a raid.
* **Invariants not yet checked:** `Attack.target`, `Support`, `Convoy` endpoints, `Voyage.destUid` (may legitimately dangle until the owning system notices).
* **Selftest limits:** `economy` only covers a Lumber Camp and one Pawn; no selftest for clans, transfers, ships, relocation.

* **Structure leftovers:** `onPacket` (75 lines), `SaveSystem::readBaseEntity` (69), `NetSession::onHello` (62), `App::run` (57), `Input::update` (51); `World.cpp`, `SaveSystem.cpp`, `ClearingSystem.cpp`, `systems/PacketHandler.cpp` are 650-720 lines.
  A new troop beyond the existing five needs a card in `TrainingPanel` (fixed 5 cards) and unit-level replication (`K_Pawn..K_Monk`); small kind checks remain in `Input.cpp`, archer fire arrows and patrol/raid rosters. Fix opportunistically; T17 finishes.
* **T13b verification** compared one coastal scene only. Not compared: raids, combat, save/reload, the Outpost, ship rendering (T24a `raid`, `saveload` cover part).
* **Never exercised in real sessions:** save/reload of construction jobs, cancel refund, training-queue pausing, efficiency numbers, demand rotation, gift diminishing returns, per-type defense dispatch, discarded income above storage share, clan-leave confirm click, harbour ship training via the context panel. Add selftests when a task touches them.
* **SDR and the server browser are untested** (need a published app id; `steam_appid.txt` is Spacewar 480). Only direct IP was run.
* Client segfaults in `steamclient.so` on exit after a screenshot run; ~1 in 3 scripted client runs time out.
* **Behaviour changes to review:** `patrol_map_groups` defaults to 2; `faction_war_percent` / `patrol_siege_*` defaults changed; build times x2, production x0.6; starting meat 150 -> 200. Tune in `server.cfg` / `Progression.hpp` (T25a takes over raid timing).
* `debug_instant_*` timers skip the pawn walk entirely.
* Clear jobs and pending builds are not saved (clear jobs fixed in T15c). Relocation, town move and homeless Town Hall need a pre-cleared site.
* Clear tooltip shows base time (client does not know `clear_time_scale`); the working pawn's tool depends on biome, not obstacle.
* Clearing and construction compete for the same pawns (fixed in T15d).
* At UI scale 2.0 in very small windows the minimap overlaps the resource bar and the build menu runs off the bottom (T21d).
* The biome label hides while the chat bar is open.
* Raised-chat / social-build keyboard toggles and the greyed Clan-chat tooltip were only verified by reading.
* Old saves (BWS6 and earlier) are discarded (moved to `.bad`); format is BW12 (BW11 + objective state).
* The tiling WM gives the client a random window size, so `BW_WIN` is ignored.
