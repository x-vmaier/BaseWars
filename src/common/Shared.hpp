/**
 * @file Shared.hpp
 * @brief Shared constants, world generation, placement rules, protocol and serialization helpers (server + client).
 */
#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI  // keep windows.h from clashing with raylib (Rectangle, DrawText...)
#define NOUSER // (CloseWindow, ShowCursor...)
#endif
#include "common/KindDefs.hpp"
#include "common/ServerConfig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

inline constexpr int MAX_PLAYERS = 16;
inline constexpr int TICK_HZ = 20;
inline constexpr int DEFAULT_PORT = 7777;
inline constexpr int UNIT_CAP = 60;
inline constexpr int CH = 16;
inline constexpr int TEAM_COLORS = 5;

// New bases spawn this far (in tiles) from an existing player: not on top of them, but close enough to find each other.
inline constexpr int SPAWN_MIN_DIST = 72;
inline constexpr int SPAWN_RADIUS = 140;

// A player whose last Town Hall fell: banner until they ask to resettle, then one offered site at a time.
enum DefeatPhase : uint8_t
{
    DF_None,
    DF_Defeated, // burning ruin, banner with the Resettle button
    DF_Offered   // a site is offered: only Accept / Resettle (next site) are allowed
};

// NPC factions own villages/towns. Their owner ids live above the player id range.
inline constexpr int FACTION_BASE = 100;
inline constexpr int FACTION_COUNT = 5;
inline constexpr int MAX_CLANS = 16;
inline constexpr int REGION_CHUNKS = 8;
inline constexpr int HOSTILE_REP = -25;
inline constexpr int FRIENDLY_REP = 25;
inline constexpr uint8_t UNIT_CONVOY_FLAG = 0x80; // set in the state byte of tribute convoys
inline constexpr const char *FACTION_NAME[FACTION_COUNT] = {
    "Ironvale Kingdom", "Sunreach Empire", "Nightfall Order", "Amethyst Conclave", "Azure League",
};
inline constexpr int FACTION_COLOR[FACTION_COUNT] = {1, 2, 4, 3, 0}; // index into Blue/Red/Yellow/Purple/Black

inline bool isFactionOwner(int o)
{
    return o >= FACTION_BASE && o < FACTION_BASE + FACTION_COUNT;
}

inline const char *repTier(int rep)
{
    return rep <= -60           ? "Hated"
           : rep <= HOSTILE_REP ? "Hostile"
           : rep < FRIENDLY_REP ? "Neutral"
           : rep < 60           ? "Friendly"
                                : "Allied";
}

enum Biome : uint8_t
{
    B_Ocean,
    B_Plains,
    B_Forest,
    B_Highlands,
    B_Desert,
    B_Flowers,
    B_Snow,
    B_Count
};

enum Msg : uint8_t
{
    C_Hello,
    C_Build,
    C_Train,
    C_Move,
    C_Attack,
    C_AttackMove,
    C_Ally,
    C_Upgrade,
    C_Cam,
    C_ClanCreate,
    C_ClanJoin,
    C_ClanLeave,
    C_Transfer,
    C_Donate,
    C_SetHaul,
    C_UpgradeUnit,
    C_Chat,
    C_ClanLeader,
    C_Clear, // [world] clear surface features: u8 n, n x (i32 x, i32 y)
    C_Board,
    C_Disembark, // [ships] embark troops / disembark all
    C_SetQuota,
    C_Support,
    C_MoveTown,
    C_Relocate,
    C_Dequeue, // [base] defense quota, support patrol, town move, relocation, queue cancel
    C_Repair,  // [sites] u32 building, u8 mode (0 start a repair, 1 cancel the running upgrade / repair)
    C_SetQuotas,
    C_SetAlloc,
    C_Dispose, // [town hall] u8 x4 defense quota per troop type (Warrior, Archer, Lancer, Monk); u8 res, u8 storage
               // share %; u8 res (discard it)
    S_Welcome,
    S_State,
    S_Fog,
    S_Text,
    S_Roster,
    S_Effect,
    S_Map,
    S_Chat,
    S_Cleared,
    S_ClearJobs,
    S_Drop, // [world] cleared-tile list/delta, clearing progress, resource drop effect
    S_Paths,
    S_ShipInfo, // [ships] path lines, ship passenger/cargo breakdown
    S_Queue,
    S_Alert,
    S_BaseInfo,       // [base] training queues, clan alerts, own base info
    S_Jobs,           // [sites] construction / upgrade / repair jobs of own and allied buildings
    S_Econ,           // [economy] donation cooldown, faction demands and gift multipliers
    C_Resettle,       // [defeat] no payload: request the first resettlement offer / the next candidate site
    C_ResettleAccept, // [defeat] no payload: found the Town Hall at the offered site
    S_Defeat,         // [defeat] u8 phase (DefeatPhase), then if phase != 0: str conqueror, str clan, f32 ruin x,y,
                      // f32 site x,y, u16 seconds left of the offer
    S_CmdReject,      // [predict] u32 seq: the server refused / could not carry out that predicted command (reliable)
    S_Pacing,     // [pacing] u8 phase, u16 seconds left, u16 total, u8 raiders, u8 faction, u8 hasOrigin, f32 x, f32 y,
                  // once per second (reliable)
    S_Objectives, // [objectives] u8 doneSeq, u8 lastDone id, u8 lastRewarded, then per tier: u8 id (255 = none),
                  // u16 progress, u16 seconds until it resolves by itself; once per second (reliable)
    C_UpgradeShip // [ships] u32 ship uid: pay and start the timed upgrade (the ship must lie at an own Harbour)
};
// S_BaseInfo: u8 quota (total), u8 homeless, u8 moving, u8 move %, then u8 x4 quota per QUOTA_KINDS type and u8 x
// R_Count storage shares %.

enum EffectType : uint8_t
{
    E_Explosion,
    E_Dust,
    E_Splash,
    E_Arrow,
    E_Heal,
    E_Fire,
    E_FireArrow,
    E_Extinguish
};

enum UnitState : uint8_t
{
    US_Idle,
    US_Move,
    US_Attack,
    US_Work,
    US_Heal,
    US_Extinguish,
    US_Build
}; // US_Build: pawn hammering at a building site
// Bit registry for the `state` byte of a state record (units: low 3 bits are the UnitState; buildings: only the flags
// below).

inline constexpr uint8_t UNIT_SUPPORT_FLAG = 0x40; // troop patrol sent to support a clan member (shown on maps)
inline constexpr uint8_t BLD_BURNING = 1, BLD_DISABLED = 2,
                         BLD_MOVING = 4;    // building flags: on fire / outside any territory / being relocated
inline constexpr uint8_t BLD_WORK = 8;      // building flag: upgrade or repair job running (no production / training)
inline constexpr uint8_t BLD_CRIPPLED = 16; // building flag: HP below damage_critical_hp, disabled until repaired

enum AllyAct : uint8_t
{
    AA_Invite,
    AA_Break
};

inline constexpr const char *BIOME_NAME[B_Count] = {"Ocean",  "Plains",        "Forest", "Highlands",
                                                    "Desert", "Flower Fields", "Snow"};
inline constexpr const char *RES_NAME[R_Count] = {"Wood", "Gold", "Stone", "Meat"};
inline constexpr int INVALID_BIOME = 255;

struct Pos
{
    float x, y;
};

struct IV
{
    int x, y;
};

inline constexpr float START_RES[R_Count] = {300, 200, 150, 200};
// Building level cap, Town Hall gates, cost / time curves and storage capacity: common/Progression.hpp

// Naval transport. Every ship type is a KT_Naval registry kind (KindDefs.hpp, KindDef::ship); Info.level is the ship's
// upgrade level 1..SHIP_MAX_LEVEL. Ship state fields in S_State: aux = upgrade level, prog = troops aboard, stock =
// resources aboard. Units aboard a ship are sent to allies with prog == UNIT_ABOARD_PROG (clients hide them) and are
// not sent to anyone else.
inline constexpr uint8_t UNIT_ABOARD_PROG = 255;
inline constexpr int SHIP_MAX_LEVEL = 3;
inline int shipLevel(int level)
{
    return std::clamp(level, 1, SHIP_MAX_LEVEL);
}
inline float shipLevelMul(int level)
{
    return 1.f + .35f * float(shipLevel(level) - 1);
}
inline int shipTroopCap(Kind k, int level)
{
    return int(std::lround(float(KINDS[k].ship.troops) * shipLevelMul(level)));
}
inline int shipResCap(Kind k, int level)
{
    return int(std::lround(float(KINDS[k].ship.cargo) * shipLevelMul(level)));
}
inline int shipCrewMin(Kind k)
{
    return KINDS[k].ship.crew;
}
inline float shipHpMul(int level)
{
    return 1.f + .3f * float(shipLevel(level) - 1);
}

// Troop types with a per-type clan defense quota (C_SetQuotas / S_BaseInfo order).
inline constexpr int QUOTA_TYPES = 4;
inline constexpr Kind QUOTA_KINDS[QUOTA_TYPES] = {K_Warrior, K_Archer, K_Lancer, K_Monk};

inline constexpr float HAULER_CARRY = 400.f;
inline constexpr int MAX_UNIT_LEVEL = 5;
inline float unitHpMul(int lv)
{
    return 1.f + .22f * float(lv - 1);
}
inline float unitDmgMul(int lv)
{
    return 1.f + .18f * float(lv - 1);
}
inline float unitSpeedMul(int lv)
{
    return 1.f + .03f * float(lv - 1);
}
inline int troopUpgradeNeedsArchery(int toLevel)
{
    return toLevel <= 2 ? 1 : (toLevel <= 4 ? 2 : 3);
}

inline float damageVs(Kind attacker, Kind target)
{ // unit roles: lancers shred units, archers are poor against walls
    bool bld = KINFO[target].building;
    if (attacker == K_Lancer)
        return bld ? .6f : 1.3f;
    if (attacker == K_Archer)
        return bld ? .7f : 1.f;
    return 1.f;
}

inline float armorOf(Kind target)
{
    return target == K_Warrior ? .8f : 1.f;
} // warriors take 20% less damage
inline float levelMul(int level)
{
    return 1.f + 0.5f * float(level - 1);
} // production / defence multiplier
inline float levelHp(int level)
{
    return 1.f + 0.25f * float(level - 1);
}

#include "common/Placement.hpp"
#include "common/Serialization.hpp"
#include "common/WorldGen.hpp"
