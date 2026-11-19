#pragma once
// Plain client-side data received from the server (non-ECS).
#include "common/Shared.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <vector>

struct LogEvent { std::string text; };                               // dispatched to the HUD log
struct Peer { int id; uint8_t flags, clan; std::string name; bool hasLoc; float lx, ly; };   // flags: 1 online, 2 friend/ally, 4 invite received, 8 invite sent
struct ClanInfo { int id; std::string name; int members, leader; };
struct FacTown { int faction; float x, y; };
struct Chunk { uint8_t biome[CH * CH]; uint8_t fog[32]; bool hasTerr = false, hasFog = false; };
struct Fx { uint8_t type; float x, y, x2, y2; double t; };          // S_State effect events
struct Ghost { Kind kind; uint8_t owner, level; float x, y; bool left; double t; };   // destroyed entity dissolving
struct Ripple { float x, y; double t; };                             // water shader ripples (world pixels)
struct Marker { float x, y; double t; };                             // command click markers (world pixels)
struct DropFx { float x, y; int res, amt; double t; };               // bouncing "+N" resource drops
struct CJob { float pct; uint8_t state = 0; };                       // clear job: progress 0..1, state 0 waiting for a pawn / 1 walking / 2 working
struct PendingSite { Kind kind; int tx, ty; };                       // own building waiting for its site to be cleared
struct QEntry { std::vector<uint8_t> kinds; int pct = 0; };          // training queue of a building
struct ShipDetail { int tier = 1, kinds[5]{}, res[R_Count]{}, voyage = 0; };
struct ChatLine { uint8_t ch; std::string name, text; double t; };
struct Toast { std::string text; float x, y; int type; double t; };  // clan alert (click to jump)
