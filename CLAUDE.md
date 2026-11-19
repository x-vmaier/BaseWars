# Base Wars - agent handoff

C++23 RTS (EnTT + Raylib + Steamworks). One authoritative `server`, one `client`. **Read `TODO.md` first** (status + remaining tasks T14-T17),
then `ARCHITECTURE.md` (layout, conventions, test hooks). Branch: `feature/prompt-implementation`. Work is committed through T13.

## Build
* Steamworks SDK must exist in `third_party/steamworks` (git-ignored; it is on the dev machine). CMake fails without it.
* `cmake --preset release && cmake --build build/release` (or any build dir: `cmake -S . -B <dir> -G Ninja && ninja -C <dir>`).
  Both targets must build with **zero warnings**. After CMake changes re-run configure (sources are globbed with CONFIGURE_DEPENDS).
* The build dir gets `assets` (symlink), `config.txt`, `server.cfg`, `steam_appid.txt`, libsteam_api.

## Running the game (verified workflow)
* **The Steam client must be running** (`steam -silent &`, wait until `~/.steam/steam.pipe` is fresh and it has finished updating) or both exes fail
  in `SteamAPI_Init` / `SteamGameServer_Init` (the client prints the reason). The dev machine's Steam works once started.
* From the build dir: `./server 7777 42 mysave.bin Test --local &` (fresh save name each time; `--local` = no Steam backend login, direct IP only), then
  `BW_SHOT=out.png BW_SHOT_FRAMES=300 timeout 60 ./client --connect 127.0.0.1:7777` and view the PNG with the Read tool.
* Client hooks (`src/client/TestHooks.cpp`): `BW_UI`, `BW_SITE`, `BW_CAM`, `BW_ZOOM`, `BW_CMD`, `BW_MOUSE`, `BW_SCALE`, `BW_THTAB`... Server hooks and
  `debug_instant_*` timers are listed in `ARCHITECTURE.md` / `config/server.cfg`. `BW_NETLOG=1` traces Steam connection states.
* Known flakiness: ~1 in 3 scripted client runs time out without a screenshot (re-run); the client segfaults in `steamclient.so` at exit (ignore).

## Rules that bit us
* Protocol is append-only: new `Msg` enumerators go at the END of the `C_*` / `S_*` group. Bump `SAVE_MAGIC` (`SaveSystem.hpp`) when the save layout changes.
* Tunables -> `ServerConfig` + `config/server.cfg` (documented); gate/cost/time rules -> `src/common/Progression.hpp` (shared by client and server).
* No `.inc` files, no monoliths (keep files < ~600 lines), header beside source, no extra executables/bots.
* Client UI: every resource cost goes through `ui::drawCosts` (`src/ui/CostRow`); buttons carry only a label. Use `ui::` screen/mouse helpers inside UI code.
* Do not run `pkill -f './server 7777'` in the same shell command line that contains that text (it kills its own shell); use `pkill -x server`.
* Parallel agents: split by disjoint files, and only one agent at a time may touch shared files (`Shared.hpp`, `World.*`, `PacketHandler.*`,
  `SaveSystem.*`, `ServerConfig.*`). Agents must not run git write commands; the lead builds, verifies and commits per task.
* Never claim a mechanic works unless it was run; `TODO.md` lists what was only verified by reading.

## How to continue
1. Pick the next task in `TODO.md` (T14 naval first; T14 and T15 touch the same server files, so do them sequentially).
2. Implement server + client + UI end to end, build warning-free, run the game and screenshot the new UI/behavior.
3. Add the new env hooks you used to `ARCHITECTURE.md`, update `TODO.md` (move the task to Done, record what was only read-verified), commit.
