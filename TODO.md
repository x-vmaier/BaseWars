# Status and remaining work

Branch `feature/prompt-implementation`. T1-T13b are implemented and committed.
**Do the first Roadmap row with status `todo`** (row order counts). One coherent task per session (see `CLAUDE.md`).
Only the first tasks are specified in detail below. Every later task has its full spec in `BACKLOG.md`: when a task becomes current, move its spec here.

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
| T13b | Maintainability refactor: `.clang-format`, draw queue, kind registry (`KindDefs.hpp`), Doxygen headers. A new building + troop needs 2 files, 0 `switch` edits |
| T24a | `server --selftest` (boot, saveload, economy, raid, net-garbage, invariants) on a shared `Sim`; `World::checkInvariants()`; asan preset, `/W4` warnings policy, `tools/check.sh`, `tools/shot.sh`; saves now byte-stable; fixed OOB kind in `C_UpgradeUnit` |
| T13 | Biome label, minimap trees, chat/build menu layout, high-contrast bars |

## Roadmap (do in this order)

Status: `todo`, `blocked: <reason>` (skip it). Finished rows move to Done. Specs for rows after #3 are in `BACKLOG.md`.

| # | Task | Size | Status | Why here |
| --- | --- | --- | --- | --- |
| 2 | **T18a** Identity: SteamID ownership, Steam names | M | todo | Real player bug; identity before new per-player state |
| 3 | **T18b** Defeat flow: burning ruin, banner, resettlement | M | todo | Real player bug; creates the `Ruin` component |
| 4 | **T18c** Tree canopy occlusion transparency | S | todo | Small visible win |
| 5 | **T19a** Networking: fixed step, tick snapshots, sequence envelope (protocol break) | M | todo | Later features replicate on this |
| 6 | **T19b** Entity interpolation buffer | M | todo | Removes unit stutter |
| 7 | **T19c** Command prediction + reconciliation | M | todo | Removes input lag |
| 8 | **T25a** Pacing Director v1 | M | todo | Stress/relief rhythm is the core experience |
| 9 | **T26** Objectives, onboarding chain, central hints | M | todo | First-hour hook, always a next goal |
| 10 | **T28a** Audio + first feedback pass | M | todo | Game feel is cheap now, expensive later |
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
* T18a: old uuid-only profiles are discarded (moved aside), like old save formats; no migration code.
* T23a: tactical slow-motion is client-side presentation only (orders previewed and buffered; the server tick never changes).
* T28a: audio is generated by `tools/gen_sounds.py` (CC0 / self-made); music is out of scope until the owner supplies licensed tracks.
* "Reseddle" in the owner's text is a typo for "Resettle".

## Current task specs

### T18a Identity: SteamID ownership and persona names (size M, critical bug)

Today a profile is matched by a client-generated `uuid` (`Session.cpp`, `NetSession::onHello` compares `pl[i].uuid`); the client also sends its own name and an unauthenticated `steamId` in `C_Hello`.
Switching Steam accounts on one machine reuses the local uuid, so account 2 inherits and renames account 1's town.

* Authenticate the SteamID with a Steam auth ticket (client `GetAuthSessionTicket`, server `BeginAuthSession`; handle the validation callback, cancel ticket on disconnect). Never use machine ids, local files, IPs or connection slots.
  Profiles, town ownership and save mappings are keyed only by the verified 64-bit SteamID. A reconnect from any machine loads exactly that town.
* Remove the uuid from matching (`Profile.uuid`, `C_Hello`, the local `profile` file); bump `SAVE_MAGIC`; old uuid-only profiles are discarded (see Decisions).
* Remove every way to edit the player name: main-menu name box, `Settings.name`, `config.txt` / profile `name=`, any protocol field carrying a client-chosen name. Clients resolve names via Steam and refresh on each login.
* `--local` has no Steam identity: `BW_DEV_ID=<n>` is accepted only when the server runs with `--local`. Unauthenticated connections are dropped with a clear client message.
* Done when: selftest `identity` (two fake ids get separate towns and persist across save/reload; same id reconnecting gets its town; a hello with a forged id and no valid auth is rejected); screenshot shows the Steam name and no name box.
* Code: `src/systems/NetSession.*` (`onHello`, `claimSlot`), `src/client/Session.cpp`, `Settings.hpp`, `src/ui/MainMenu.cpp`, `src/networking/Steam*Net.*`, `SteamClient.*`, `SaveSystem`.

### T18b Defeat flow: burning ruin, banner, resettlement (size M, critical bug)

Fixes: a defeated player keeps the endless damage post-process (`PostFx` vignette), the town vanishes instantly, the camera pans freely and no Town Hall can be placed (see `NetSession::respawnIfDestroyed`, `homeless`).

* Stop the endless damage post-process cleanly on defeat (find what keeps re-triggering it).
* Shared `Ruin` component (wall-clock expiry, burning flag): the defeated town stays as a burning ruin and disappears after `defeat_ruin_hours` (default 24, real time, persisted). T20d reuses it.
* Defeat banner in the existing theme (paper banner, ribbon): names the conqueror (last attacker, `Hit` component) and their clan if any; button "Resettle".
* On click the server picks a candidate site with priority near a Steam friend -> near a clan member -> near any other player (reuse `SpawnSystem` clearing checks, `SPAWN_MIN_DIST`).
  The client enters a resettlement preview where only **Accept** and **Resettle** (= next candidate) work; the server rejects every other command while an offer is pending. Only Accept places the Town Hall. Pending offer and defeated state are persisted.
* Stuck-state rule: no candidate -> fall back to any valid free site; an offer unanswered for `resettle_offer_timeout_s` is auto-accepted.
* Done when: selftest `defeat` (force defeat; ruin persists across save/reload and expires on wall-clock; priority friend > clan > other; commands rejected during an offer; Accept places a Town Hall and ends the state; no-candidate and timeout paths resolve); screenshot of banner + preview.
* Hooks: force defeat of the local player, force candidate priority cases.
* Code: `NetSession::respawnIfDestroyed`, `SpawnSystem`, `PostFx.cpp`, `src/ui/BaseMenus.*`, `SaveSystem`.

## Known gaps / follow-ups

* **T24a only verified by reading:** `tools/shot.sh` (never run), the non-MSVC branches of `cmake/Compiler.cmake` (UBSan on gcc/clang), the clang-format step of `check.sh` on Linux. asan on MSVC has no UBSan. The asan `raid` scenario (700 simulated seconds) takes 15+ minutes: the owner told us not to wait for it, so the asan runs of `raid` and `invariants` were never completed (asan `boot`, `saveload`, `economy`, `net-garbage` passed). Consider shortening `raid` under asan.
* **Observed by `raid`:** a forced raid kills and respawns the player repeatedly (7 defeats in 700 s: raiders keep targeting the new hall for 600 s). Stress without relief; T18b / T25a must fix.
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
* Old saves (BWS6 and earlier) are discarded (moved to `.bad`); format is BWS8.
* The tiling WM gives the client a random window size, so `BW_WIN` is ignored.
