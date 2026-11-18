#pragma once
// Shared constants, world generation, placement rules, protocol and serialization helpers (server + client).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI   // keep windows.h from clashing with raylib (Rectangle, DrawText...)
#define NOUSER  // (CloseWindow, ShowCursor...)
#endif
#include <enet/enet.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

inline constexpr int MAX_PLAYERS = 16, TICK_HZ = 20, DEFAULT_PORT = 7777, UNIT_CAP = 60, CH = 16, TEAM_COLORS = 5;
// New bases spawn this far (in tiles) from an existing player: not on top of them, but close enough to find each other.
inline constexpr int SPAWN_MIN_DIST = 72, SPAWN_RADIUS = 140;
// NPC factions own villages/towns. Their owner ids live above the player id range.
inline constexpr int FACTION_BASE = 100, FACTION_COUNT = 5, MAX_CLANS = 16, REGION_CHUNKS = 8;
inline constexpr int HOSTILE_REP = -25, FRIENDLY_REP = 25;
inline constexpr uint8_t UNIT_CONVOY_FLAG = 0x80;       // set in the state byte of tribute convoys
inline constexpr const char* FACTION_NAME[FACTION_COUNT] = {"Ironvale Kingdom", "Sunreach Empire", "Nightfall Order", "Amethyst Conclave", "Azure League"};
inline constexpr int FACTION_COLOR[FACTION_COUNT] = {1, 2, 4, 3, 0};      // index into Blue/Red/Yellow/Purple/Black
inline bool isFactionOwner(int o) { return o >= FACTION_BASE && o < FACTION_BASE + FACTION_COUNT; }
inline const char* repTier(int rep) { return rep <= -60 ? "Hated" : rep <= HOSTILE_REP ? "Hostile" : rep < FRIENDLY_REP ? "Neutral" : rep < 60 ? "Friendly" : "Allied"; }

enum Biome : uint8_t { B_Ocean, B_Plains, B_Forest, B_Highlands, B_Desert, B_Flowers, B_Snow, B_Count };
enum Res : uint8_t { R_Wood, R_Gold, R_Stone, R_Meat, R_Count };
enum Kind : uint8_t { K_TownHall, K_Outpost, K_Harbour, K_Storage, K_Mine, K_Lumber, K_Campfire, K_Archery,   // buildings
                      K_Pawn, K_Warrior, K_Archer, K_Lancer, K_Monk, K_Ship, K_Sheep, K_Count };       // units / NPCs
enum Msg : uint8_t { C_Hello, C_Build, C_Train, C_Move, C_Attack, C_AttackMove, C_Ally, C_Upgrade, C_Cam,
                     C_ClanCreate, C_ClanJoin, C_ClanLeave, C_Transfer, C_Donate, C_SetHaul, C_UpgradeUnit, C_Chat, C_ClanLeader,
                     C_Clear,   // [world] clear surface features: u8 n, n x (i32 x, i32 y)
                     C_Board, C_Disembark,   // [ships] embark troops / disembark all
                     C_SetQuota, C_Support, C_MoveTown, C_Relocate, C_Dequeue,   // [base] defense quota, support patrol, town move, relocation, queue cancel
                     S_Welcome, S_State, S_Fog, S_Text, S_Roster, S_Effect, S_Map, S_Chat,
                     S_Cleared, S_ClearJobs, S_Drop,   // [world] cleared-tile list/delta, clearing progress, resource drop effect
                     S_Paths, S_ShipInfo,              // [ships] path lines, ship passenger/cargo breakdown
                     S_Queue, S_Alert, S_BaseInfo };   // [base] training queues, clan alerts, own base info
enum EffectType : uint8_t { E_Explosion, E_Dust, E_Splash, E_Arrow, E_Heal, E_Fire, E_FireArrow, E_Extinguish };
enum UnitState : uint8_t { US_Idle, US_Move, US_Attack, US_Work, US_Heal, US_Extinguish };
// Bit registry for the `state` byte of a state record (units: low 3 bits are the UnitState; buildings: only the flags below).
inline constexpr uint8_t UNIT_SUPPORT_FLAG = 0x40;                          // troop patrol sent to support a clan member (shown on maps)
inline constexpr uint8_t BLD_BURNING = 1, BLD_DISABLED = 2, BLD_MOVING = 4;  // building flags: on fire / outside any territory / being relocated
enum AllyAct : uint8_t { AA_Invite, AA_Break };

inline constexpr const char* BIOME_NAME[B_Count] = {"Ocean", "Plains", "Forest", "Highlands", "Desert", "Flower Fields", "Snow"};
inline constexpr const char* RES_NAME[R_Count] = {"Wood", "Gold", "Stone", "Meat"};
inline constexpr int INVALID_BIOME = 255;

struct Pos { float x, y; };
struct IV { int x, y; };

struct KindInfo {
    const char* name;
    bool building;
    int fw, fh;                 // footprint in tiles
    float cost[R_Count];        // wood, gold, stone, meat
    float hp, speed, dps, range, buildTime;
    int vision;                 // reveal radius in tiles
};
inline constexpr KindInfo KINFO[K_Count] = {
    {"Town Hall", true, 4, 3, {250, 200, 150, 0}, 800, 0, 14, 6.5f, 40, 9},
    {"Outpost", true, 2, 2, {80, 40, 60, 0}, 350, 0, 8, 7.f, 20, 8},
    {"Harbour", true, 2, 2, {120, 60, 40, 0}, 300, 0, 0, 0, 25, 7},
    {"Storage House", true, 2, 2, {100, 20, 60, 0}, 400, 0, 0, 0, 25, 5},
    {"Mine", true, 2, 2, {60, 20, 0, 0}, 250, 0, 0, 0, 18, 4},
    {"Lumber Camp", true, 2, 2, {40, 10, 0, 0}, 250, 0, 0, 0, 15, 4},
    {"Campfire", true, 1, 1, {20, 0, 0, 0}, 60, 0, 0, 0, 5, 4},
    {"Archery", true, 3, 2, {120, 60, 80, 0}, 450, 0, 0, 0, 30, 5},
    {"Pawn", false, 1, 1, {0, 0, 0, 30}, 40, 2.6f, 3, 1.2f, 0, 5},
    {"Warrior", false, 1, 1, {10, 20, 0, 40}, 120, 2.6f, 12, 1.4f, 0, 6},
    {"Archer", false, 1, 1, {30, 20, 0, 35}, 60, 2.8f, 9, 6.f, 0, 7},
    {"Lancer", false, 1, 1, {20, 40, 20, 50}, 100, 3.4f, 15, 2.4f, 0, 6},
    {"Monk", false, 1, 1, {0, 50, 0, 30}, 50, 2.6f, 8, 4.f, 0, 6},
    {"Ship", false, 1, 1, {80, 30, 0, 20}, 220, 4.2f, 0, 0, 0, 10},
    {"Sheep", false, 1, 1, {0, 0, 0, 0}, 12, 0.9f, 0, 0, 0, 0}};
inline constexpr float START_RES[R_Count] = {300, 200, 150, 150};
inline constexpr int MAX_LEVEL = 3;

// ---------------------------------------------------------------- naval transport (ships are empty boats; the Harbour level at build time is the tier)
// Strict capacities per tier (1..3). Ship state fields in S_State: Info.level = tier (aux), prog = troops aboard, stock = resources aboard.
// Units aboard a ship are sent to allies with prog == UNIT_ABOARD_PROG (clients hide them) and are not sent to anyone else.
inline constexpr int SHIP_TROOP_CAP[3] = {6, 12, 20};
inline constexpr int SHIP_RES_CAP[3] = {400, 900, 1600};
inline constexpr const char* SHIP_NAME[3] = {"Small Boat", "Longship", "Galleon"};
inline constexpr uint8_t UNIT_ABOARD_PROG = 255;
inline int shipTier(int level) { return std::clamp(level, 1, 3); }
inline int shipTroopCap(int level) { return SHIP_TROOP_CAP[shipTier(level) - 1]; }
inline int shipResCap(int level) { return SHIP_RES_CAP[shipTier(level) - 1]; }
inline float shipHpMul(int level) { return 1.f + .6f * float(shipTier(level) - 1); }

inline bool isBuilding(Kind k) { return KINFO[k].building; }
inline bool isResourceBuilding(Kind k) { return k == K_Mine || k == K_Lumber || k == K_Harbour; }
inline bool isStorage(Kind k) { return k == K_TownHall || k == K_Outpost || k == K_Storage; }
inline bool isArmy(Kind k) { return k == K_Warrior || k == K_Archer || k == K_Lancer; }
// Troops are trained only in the Archery; the Harbour builds ships.
inline bool canTrain(Kind building, Kind unit) {
    if (building == K_Archery) return unit == K_Pawn || unit == K_Warrior || unit == K_Archer || unit == K_Lancer || unit == K_Monk;
    return building == K_Harbour && unit == K_Ship;
}
inline int unitUnlockLevel(Kind u) { return u == K_Pawn || u == K_Warrior ? 1 : (u == K_Archer || u == K_Monk ? 2 : (u == K_Lancer ? 3 : 1)); }   // Archery level needed
// Storage capacity (total resources) by building level 1..3.
inline float storageCap(Kind k, int level) {
    static const float th[3] = {3000, 6000, 10000}, op[3] = {300, 600, 1000}, sh[3] = {800, 1600, 2800};
    int i = std::clamp(level, 1, 3) - 1;
    return k == K_TownHall ? th[i] : (k == K_Outpost ? op[i] : (k == K_Storage ? sh[i] : 0.f));
}
inline constexpr float HAULER_CARRY = 400.f;
inline constexpr int MAX_UNIT_LEVEL = 5;
inline bool isTroop(Kind k) { return k == K_Pawn || k == K_Warrior || k == K_Archer || k == K_Lancer || k == K_Monk; }
inline float unitHpMul(int lv) { return 1.f + .22f * float(lv - 1); }
inline float unitDmgMul(int lv) { return 1.f + .18f * float(lv - 1); }
inline float unitSpeedMul(int lv) { return 1.f + .03f * float(lv - 1); }
inline int troopUpgradeNeedsArchery(int toLevel) { return toLevel <= 2 ? 1 : (toLevel <= 4 ? 2 : 3); }
inline float damageVs(Kind attacker, Kind target) {      // unit roles: lancers shred units, archers are poor against walls
    bool bld = KINFO[target].building;
    if (attacker == K_Lancer) return bld ? .6f : 1.3f;
    if (attacker == K_Archer) return bld ? .7f : 1.f;
    return 1.f;
}
inline float armorOf(Kind target) { return target == K_Warrior ? .8f : 1.f; }   // warriors take 20% less damage
inline float upgradeCostMul(int level) { return 1.f + float(level); }       // cost of going from `level` to level+1
inline float levelMul(int level) { return 1.f + 0.5f * float(level - 1); }  // production / defence multiplier
inline float levelHp(int level) { return 1.f + 0.25f * float(level - 1); }

// ---------------------------------------------------------------- hashing & noise
inline uint32_t hash2(int x, int y, uint32_t s) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float hf(int x, int y, uint32_t s) { return float(hash2(x, y, s) & 0xFFFF) / 65535.f; }
inline float vnoise(float x, float y, uint32_t s) {
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - float(xi), fy = y - float(yi);
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hf(xi, yi, s), b = hf(xi + 1, yi, s), c = hf(xi, yi + 1, s), d = hf(xi + 1, yi + 1, s);
    float top = a + (b - a) * fx, bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}
inline float fbm(float x, float y, uint32_t s, int oct = 4) {
    float v = 0, a = 0.5f, t = 0;
    for (int o = 0; o < oct; o++) { v += a * vnoise(x, y, s + uint32_t(o) * 131u); t += a; x *= 2; y *= 2; a *= 0.5f; }
    return v / t;
}
inline constexpr float OCEAN_LEVEL = .43f, ISLAND_DEPTH = .06f, ISLAND_GATE = .68f, ISLAND_BLOB = .74f;   // below OCEAN_LEVEL: ocean (rare islands excepted)
inline float stretch(float v) { return std::clamp((v - .5f) * 2.1f + .5f, 0.f, 1.f); }

// ---------------------------------------------------------------- world generation (infinite, deterministic)
inline uint8_t genBiome(int x, int y, uint32_t seed) {
    float fx = float(x), fy = float(y);
    float e = stretch(fbm(fx * .0105f, fy * .0105f, seed, 4));
    float m = stretch(fbm(fx * .016f + 311.f, fy * .016f - 97.f, seed + 11, 3));
    float t = stretch(fbm(fx * .0085f - 57.f, fy * .0085f + 143.f, seed + 23, 3));
    float d = fbm(fx * .09f, fy * .09f, seed + 37, 3);
    float f = fbm(fx * .045f, fy * .045f, seed + 53, 3);
    e += (d - .5f) * .07f;
    if (e < OCEAN_LEVEL) {
        // rare small islands in open (deep) ocean: a regional gate keeps them scattered, a finer noise shapes 1-3 chunk wide blobs
        if (e > OCEAN_LEVEL - ISLAND_DEPTH) return B_Ocean;
        float gate = vnoise(fx * .021f + 17.f, fy * .021f - 41.f, seed + 91);
        float blob = vnoise(fx * .033f - 9.f, fy * .033f + 23.f, seed + 93) * .88f + vnoise(fx * .12f, fy * .12f, seed + 95) * .12f;
        if (gate < ISLAND_GATE || blob < ISLAND_BLOB) return B_Ocean;
        e = .5f;                                                      // classify the island like ordinary lowland
    }
    if (e > .80f) return B_Highlands;
    if (t < .27f) return B_Snow;
    if (t > .62f && m < .52f) return B_Desert;
    if (m > .58f) return B_Forest;
    if (f > .54f && m > .25f) return B_Flowers;
    return B_Plains;
}
inline bool isLand(int b) { return b != B_Ocean && b != INVALID_BIOME; }
inline bool isCliffTile(int self, int above) { return isLand(self) && self != B_Highlands && above == B_Highlands; }

inline int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline int chunkOf(int v) { return floorDiv(v, CH); }
inline int64_t chunkKey(int cx, int cy) { return (int64_t(cx) << 32) | int64_t(uint32_t(cy)); }
inline int keyCx(int64_t k) { return int(k >> 32); }
inline int keyCy(int64_t k) { return int(uint32_t(k & 0xFFFFFFFF)); }
inline int localIdx(int x, int y) { return (y - chunkOf(y) * CH) * CH + (x - chunkOf(x) * CH); }

// ---------------------------------------------------------------- economy rules
// Production per second of a resource building, by the biome it stands on.
struct Yield { float r[R_Count]; };
inline Yield yieldOf(Kind k, int biome, bool coastal) {
    Yield y{{0, 0, 0, 0}};
    if (k == K_Lumber) {
        switch (biome) {
        case B_Forest: y.r[R_Wood] = 2.6f; break;
        default: y.r[R_Wood] = .45f; break;                             // lumberjacks thrive in forests only; elsewhere a trickle (meat comes from hunting)
        }
    } else if (k == K_Mine) {
        switch (biome) {
        case B_Highlands: y.r[R_Stone] = 2.4f; y.r[R_Gold] = .4f; break;
        case B_Desert: y.r[R_Gold] = 2.4f; y.r[R_Stone] = .3f; break;
        default: y.r[R_Stone] = .4f; y.r[R_Gold] = .1f; break;
        }
    } else if (k == K_Harbour) {
        y.r[R_Meat] = coastal ? 1.2f : 0.f;
        if (biome == B_Plains || biome == B_Flowers) y.r[R_Meat] += .4f;
    }
    return y;
}
inline bool preferredBiome(Kind k, int biome) {
    return (k == K_Lumber && biome == B_Forest) || (k == K_Mine && (biome == B_Highlands || biome == B_Desert));
}
inline constexpr float OFFLINE_PROD = 0.12f;
inline constexpr float COLD_UNIT_DPS = 2.f, COLD_BUILDING_FRAC = .01f, WARMTH_RADIUS = 6.5f;

// ---------------------------------------------------------------- [world] surface features, clearing, gold bonus (server + client)
enum Feat : uint8_t { F_None, F_Pine, F_Oak, F_Bush, F_Rock, F_Gold };
inline uint8_t featureAt(int x, int y, int b, uint32_t seed) {
    uint32_t h = hash2(x, y, seed + 777) % 1000;
    switch (b) {
    case B_Forest: return h < 250 ? (hash2(x, y, 3) % 100 < 30 ? F_Oak : F_Pine) : (h < 290 ? F_Bush : (h < 315 ? F_Rock : F_None));
    case B_Plains: return h < 35 ? F_Oak : (h < 80 ? F_Bush : (h < 92 ? F_Rock : F_None));
    case B_Flowers: return h < 110 ? F_Bush : (h < 125 ? F_Oak : F_None);
    case B_Desert: return h < 30 ? F_Gold : (h < 65 ? F_Rock : F_None);
    case B_Highlands: return h < 110 ? F_Rock : (h < 122 ? F_Gold : F_None);
    case B_Snow: return h < 140 ? F_Pine : (h < 158 ? F_Rock : F_None);
    default: return F_None;
    }
}
inline bool featureBlocks(uint8_t f) { return f == F_Pine || f == F_Oak || f == F_Rock || f == F_Gold; }   // bushes are decoration only
inline float clearSeconds(uint8_t f) { return f == F_Gold ? 5.f : (f == F_Rock ? 3.5f : 2.5f); }
inline int clearCostWood(uint8_t f) { return f == F_Gold ? 8 : (f == F_Rock ? 5 : 3); }              // paid up front by the player
inline Res dropRes(uint8_t f) { return f == F_Gold ? R_Gold : (f == F_Rock ? R_Stone : R_Wood); }      // what the cleared feature yields
inline int dropAmount(uint8_t f) { return f == F_Gold ? 14 : (f == F_Rock ? 8 : 10); }
inline constexpr int MAX_CLEAR_TILES = 24;                                                              // per C_Clear request
// Gold deposits near a Mine raise its output: +GOLD_BONUS_EACH per deposit within GOLD_RADIUS tiles of its centre, capped.
inline constexpr float GOLD_RADIUS = 4.5f, GOLD_BONUS_EACH = .2f, GOLD_BONUS_CAP = 1.f;
inline float goldBonusOf(int deposits) { return std::min(GOLD_BONUS_CAP, GOLD_BONUS_EACH * float(deposits)); }
// W: int biome(x,y); bool cleared(x,y); uint32_t seedValue()
template <class W> int goldNear(W& w, Pos c) {
    int n = 0, cx = int(std::floor(c.x)), cy = int(std::floor(c.y)), r = int(std::ceil(GOLD_RADIUS));
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            float dx = float(x) + .5f - c.x, dy = float(y) + .5f - c.y;
            if (dx * dx + dy * dy > GOLD_RADIUS * GOLD_RADIUS) continue;
            int b = w.biome(x, y);
            if (b != B_Highlands && b != B_Desert) continue;
            if (featureAt(x, y, b, w.seedValue()) == F_Gold && !w.cleared(x, y)) n++;
        }
    return n;
}

// ---------------------------------------------------------------- placement rules (identical on server & client)
enum PlaceErr { P_OK, P_Unexplored, P_Terrain, P_Occupied, P_Territory, P_Coast, P_TooClose, P_Enemy, P_Spacing, P_Radius, P_Count };
enum PlaceMode : int { PM_Normal = 0, PM_HomelessHall = 1, PM_MoveHall = 2 };   // PM_*: where a Town Hall may stand (see checkPlace)
inline constexpr const char* PLACE_TEXT[P_Count] = {"OK", "Explore this area first", "Cannot build on this terrain", "Something is in the way", "Outside your territory",
                                                  "Harbours must touch the ocean", "Too close to another base", "Inside enemy territory",
                                                  "Too close to another campfire", "Too far from your clan's towns"};
inline int anchorRadius(Kind k) { return k == K_TownHall ? 14 : (k == K_Outpost ? 10 : 0); }
inline Pos footCenter(Kind k, int tx, int ty) { return {float(tx) + float(KINFO[k].fw) * .5f, float(ty) + float(KINFO[k].fh) * .5f}; }

// W: int biome(x,y) [INVALID_BIOME if unknown]; bool explored(x,y); bool occupied(x,y);
//    void anchors(F) calling F(Pos center, Kind kind, bool mine, bool allied) for each FUNCTIONAL Town Hall / Outpost.
//    optional: bool campfireNear(Pos c, float r)  (any campfire closer than r), bool inClanRadius(Pos c) (homeless Town Hall placement)
inline constexpr float CAMPFIRE_SPACING = 6.f, CLAN_TOWN_RADIUS = 120.f;
inline constexpr float OUTPOST_REACH = 34.f;
inline constexpr float TOWN_MOVE_COST[R_Count] = {300, 500, 200, 0};   // wood, gold, stone, meat for moving the Town Hall
inline constexpr float TOWN_MOVE_TIME = 180.f;                       // seconds of construction   // an Outpost may stand this far (tiles) from the Town Hall
template <class W> bool wCampfireNear(W& w, Pos c, float r) {
    if constexpr (requires { w.campfireNear(c, r); }) return w.campfireNear(c, r);
    else return false;
}
template <class W> bool wInClanRadius(W& w, Pos c) {
    if constexpr (requires { w.inClanRadius(c); }) return w.inClanRadius(c);
    else return true;
}
template <class W> int checkPlace(W& w, Kind k, int tx, int ty, int mode = PM_Normal) {
    const KindInfo& ki = KINFO[k];
    for (int y = ty; y < ty + ki.fh; y++)
        for (int x = tx; x < tx + ki.fw; x++) {
            if (!w.explored(x, y)) return P_Unexplored;
            int b = w.biome(x, y);
            if (b == INVALID_BIOME) return P_Unexplored;
            if (!isLand(b) || isCliffTile(b, w.biome(x, y - 1))) return P_Terrain;
        }
    for (int y = ty; y < ty + ki.fh; y++)
        for (int x = tx; x < tx + ki.fw; x++)
            if (w.occupied(x, y)) return P_Occupied;
    if (k == K_Harbour) {
        bool coast = false;
        for (int y = ty - 1; y <= ty + ki.fh; y++)
            for (int x = tx - 1; x <= tx + ki.fw; x++) {
                bool inside = x >= tx && x < tx + ki.fw && y >= ty && y < ty + ki.fh;
                bool corner = (x < tx || x >= tx + ki.fw) && (y < ty || y >= ty + ki.fh);
                if (!inside && !corner && w.biome(x, y) == B_Ocean) coast = true;
            }
        if (!coast) return P_Coast;
    }
    Pos c = footCenter(k, tx, ty);
    if (k == K_Campfire) return wCampfireNear(w, c, CAMPFIRE_SPACING) ? P_Spacing : P_OK;   // campfires: anywhere, but spaced out; they claim no territory
    bool freeHall = k == K_TownHall && mode != PM_Normal;
    bool inTerritory = false, tooClose = false, enemy = false, anyOwn = false;
    float reach = k == K_TownHall ? 60.f : (k == K_Outpost ? OUTPOST_REACH : 0.f);
    w.anchors([&](Pos a, Kind ak, bool mine, bool ally) {
        float d = std::max(std::abs(a.x - c.x), std::abs(a.y - c.y));
        int rad = anchorRadius(ak);
        if (mine) anyOwn = true;
        if (mine && (k == K_Outpost ? (ak == K_TownHall && d <= reach) : (reach > 0 ? d <= reach : d <= float(rad)))) inTerritory = true;
        if (!mine && !ally && d <= float(rad + 2)) enemy = true;
        if (k == K_Outpost && (mine || ally) && d < 12.f) tooClose = true;
        if (k == K_TownHall && d < 26.f) tooClose = true;
    });
    if (enemy) return P_Enemy;
    if (!freeHall && (!anyOwn || !inTerritory)) return P_Territory;
    if (tooClose) return P_TooClose;
    if (mode == PM_HomelessHall && !wInClanRadius(w, c)) return P_Radius;
    return P_OK;
}

// ---------------------------------------------------------------- serialization
struct Writer {
    std::vector<uint8_t> b;
    template <class T> Writer& put(T v) {
        static_assert(std::is_trivially_copyable_v<T>);
        auto p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(T));
        return *this;
    }
    void bytes(const void* d, size_t n) { auto p = static_cast<const uint8_t*>(d); b.insert(b.end(), p, p + n); }
    void str(const std::string& s) { put<uint16_t>(uint16_t(std::min<size_t>(s.size(), 65535))); bytes(s.data(), std::min<size_t>(s.size(), 65535)); }
};

struct Reader {
    const uint8_t* p;
    const uint8_t* e;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (size_t(e - p) < sizeof(T)) { ok = false; return v; }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    const uint8_t* bytes(size_t n) {
        if (size_t(e - p) < n) { ok = false; return nullptr; }
        auto r = p;
        p += n;
        return r;
    }
    std::string str() {
        size_t n = get<uint16_t>();
        auto d = bytes(n);
        return d ? std::string(reinterpret_cast<const char*>(d), n) : std::string();
    }
};

inline void sendPacket(ENetPeer* peer, const Writer& w) {
    enet_peer_send(peer, 0, enet_packet_create(w.b.data(), w.b.size(), ENET_PACKET_FLAG_RELIABLE));
}
