# Base Wars - code map

| File | Role |
|------|------|
| `shared.hpp` | Constants, enums, `KINFO` unit/building table, biome generation, placement rules (`checkPlace`), protocol enums, `Writer`/`Reader`. Included by client, server and bots. |
| `net.hpp` | `ServerNet` / `ClientNet`: one transport interface over ENet (direct IP) and Steam Networking Sockets (P2P relay). `NetEvent`, `ServerEntry`. |
| `steam_layer.hpp` | Steamworks wrappers (login, friends, server browser, dedicated-server registration). Compiled only with `BW_STEAM`; otherwise inert stubs. |
| `settings.hpp` | Persistent client settings (`settings.txt`). |
| `assets.hpp` | Asset lookup, texture/animation cache, tiny JSON parser, Tiled `.tmj` UI maps. |
| `server.cpp` | Authoritative simulation (`struct Server`). Feature code that is large lives in `server_*.inc`, included inside the struct. |
| `client.cpp` | Raylib client (`struct App`). Large features live in `client_*.inc`, included inside the struct. |
| `client_menu.inc` | Main menu + server browser. |
| `server_world.inc` | World/NPC life (inside `struct Server`): clearing of surface features + persisted cleared tiles, gold-deposit mine bonus, hunting free pawns, sheep repopulation, faction patrols with loot and retaliation. Env test hooks: `BW_WORLDLOG`, `BW_PATROLNOW`, `BW_ARMY=<n>`. |
| `client_world.inc` | Client side of the above (inside `struct App`): cleared-tile sync, clear UX/progress, resource drops, gold sparkle, land clouds. |
| `server_ships.inc` | Naval transport: ship tiers/capacity, embark/disembark, ferry planner `planTransport`/`startTransport`, path-line (`S_Paths`) and `S_ShipInfo` senders, ship save section. API documented at the top of the file. |
| `client_ships.inc` | Ship drawing (3 tiers), ship panel, right-click embark, path-line rendering. |
| `tools/gen_ui_maps.py` | Generates the Tiled UI maps in `assets/ui/`. |
| `tools/bots/*.cpp` | Headless protocol bots; each file becomes an executable (see CMake) for verifying server behaviour without a window. |
| `assets/shaders/*.fs` | GLSL: outline, water, dissolve, bloom, post (AgX, grading, lighting). |

Conventions
* The server is authoritative; the client only sends commands (`C_*`) and renders `S_*` messages.
* Protocol: append new enumerators to the END of the `C_*` group / `S_*` group in `shared.hpp`'s `Msg`. The `state` byte bit registry is documented next to `UnitState`.
* Savegame: bump `SAVE_MAGIC` in `server.cpp` whenever the save layout changes.
* Windows: never name a variable `near` / `far` (macros in `windef.h`).
