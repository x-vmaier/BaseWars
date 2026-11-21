/**
 * @file World.hpp
 * @brief Shared server state plus the entity and query helpers every system uses.
 */
#pragma once
#include "common/Pacing.hpp"
#include "common/Shared.hpp"
#include "components/Components.hpp"
#include "networking/SteamServerNet.hpp"

#include <entt/entt.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef near
#undef near
#endif

using Bits = std::array<uint8_t, 32>;

struct Systems;

struct Clan
{
    bool used = false;
    std::string name;
    uint8_t leader = 0;
};
struct Town
{
    uint16_t id;
    uint8_t faction;
    int cx, cy;
    uint32_t th;
    bool alive;
    double respawnAt, nextSpawn, lastAlert;
};

struct Profile
{
    bool used = false, online = false;
    uint64_t conn = 0; // transport connection id (Steam connection)
    uint64_t steamId = 0;
    std::string name; // runtime-only name token (NameToken.hpp), never saved or client-chosen
    uint64_t lastSeen = 0;
    uint32_t lastInputSeq = 0; // newest client command seq accepted on this connection (echoed in snapshots)
    float res[R_Count]{};
    bool allied[MAX_PLAYERS + 1]{}, pending[MAX_PLAYERS + 1]{}; // pending[x] = I invited x
    Pos cam{0, 0};
    std::unordered_map<int64_t, Bits> expl; // permanently explored tiles, per chunk
    std::unordered_map<int64_t, Bits> sentFog;
    std::unordered_set<int64_t> sentTerr;
    double lastWarn = -100;
    uint8_t clan = 0, ulevel[K_Count]{};
    double lastLootWarn = -100;
    float rep[FACTION_COUNT]{};
    double helpCool[FACTION_COUNT]{};
    double donateReady = 0;                     // donation / clan transfer cooldown (world clock)
    float giftRecent[FACTION_COUNT][R_Count]{}; // recent gifts per faction and resource (fading memory)
};

struct Demand
{
    uint8_t res = 0;
    double until = 0;
    float filled = 0;
}; // what a faction's towns want right now

// Defeat / resettlement state of one player (saved).
struct DefeatState
{
    uint8_t phase = DF_None;
    uint8_t conqueror = 0; // owner id of the last attacker (0 = unknown)
    uint8_t tries = 0;     // offers shown so far (picks the next candidate)
    Pos ruin{0, 0}, site{0, 0};
    double offerAt = 0; // world.clock when the offer was made; only counts while online (reset on login)
};

// Pacing Director state of one player (saved). Timers count down only while the owner is online (offline pause).
struct Pacing
{
    uint8_t phase = PH_Calm;
    double left = -1, total = 1; // seconds left / total of the phase; left < 0 = not started yet
    uint8_t faction = 0;         // faction of the announced / running raid
    uint8_t raiders = 0;         // planned raid size (announced at the start of the warning)
    float factor = 1.f;          // adaptive strength factor: heavy losses lower it, a flawless defense raises it
    Pos origin{0, 0};            // town the raid marches from (warning arrow)
    uint16_t assetsBefore = 0;   // buildings + troops when the siege began
    uint16_t survived = 0;       // raids survived
};

struct BaseProf
{ // per-player base mechanics state (saved in the "base" section)
    DefeatState defeat;
    Pacing pacing;
    uint8_t quota[QUOTA_TYPES]{3, 2, 0,
                               0}; // defense quota per QUOTA_KINDS type: troops sent to every attacked clan mate
    uint8_t alloc[R_Count]{100, 100, 100, 100}; // Town Hall storage share per resource (% of the hall's capacity)
    bool homeless = false, testDone = false;
    double alertNext = 0, defNext = 0, supNext = 0, donateNote = 0, allocNote = 0;
    int quotaTotal() const
    {
        int t = 0;
        for (uint8_t q : quota)
            t += q;
        return t;
    }
    std::vector<uint64_t> friends; // Steam friends reported at login (used when respawning)
};

struct Eff
{
    uint8_t type;
    float x, y, x2, y2;
};

// Vision source (a completed entity of the viewer or of someone sharing vision with it).
struct Source
{
    float x, y, r;
};
// Vision sources bucketed into 16x16-tile cells (each source is inserted with a 3-tile margin, the largest padding used
// by callers).
struct SrcGrid
{
    std::unordered_map<int64_t, std::vector<Source>> cells;
    explicit SrcGrid(const std::vector<Source> &s);
    bool seen(float x, float y, float pad = 0) const;
};
bool seen(const std::vector<Source> &s, float x, float y, float pad = 0);

inline uint64_t nowSec()
{
    return uint64_t(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
inline int64_t tkey(int x, int y)
{
    return chunkKey(x, y);
}
inline bool isPlayer(int o)
{
    return o >= 1 && o <= MAX_PLAYERS;
}
inline Pos tileCenter(IV t)
{
    return {float(t.x) + .5f, float(t.y) + .5f};
}
inline IV tileOf(Pos p)
{
    return {int(std::floor(p.x)), int(std::floor(p.y))};
}
float baseTestSpeed(); // test hook: BW_BASESPEED=<x> speeds up queues, moves and relocations

inline constexpr int MAX_QUOTA = 20;

class World
{
  public:
    World(uint32_t seed, std::string savePath);

    // ---- state
    net::SteamServerNet net;
    std::unordered_map<uint64_t, int> connPlayer; // connection -> player id
    double lastPublish = -100;
    entt::registry reg;
    Profile pl[MAX_PLAYERS + 1];
    BaseProf bp[MAX_PLAYERS + 1];
    std::unordered_map<uint32_t, entt::entity> byUid;
    std::unordered_map<int64_t, uint32_t> occ;                      // tile -> building uid
    std::unordered_map<int64_t, std::array<uint8_t, CH * CH>> terr; // generated terrain chunks
    std::unordered_set<int64_t> spawned;                            // chunks whose NPCs exist
    std::unordered_set<uint32_t> dying;
    std::vector<std::pair<entt::entity, int>> toKill;
    std::vector<Eff> effects;
    uint32_t seed, tick = 0, nextUid = 1;
    std::mt19937 rng;
    std::string savePath;
    double clock = 0, lastSave = 0;
    bool loadStarted = false;
    Clan clans[MAX_CLANS + 1];
    std::vector<Town> towns;
    std::unordered_set<int64_t> regionsDone;
    float facDonated[FACTION_COUNT]{};
    Demand demand[FACTION_COUNT];
    std::unordered_set<int64_t> cleared; // tiles whose surface feature has been removed (persisted)
    std::vector<int64_t> clearedNew;     // delta waiting to be broadcast
    uint32_t clearVer = 1;               // bumps whenever `cleared` changes (invalidates mine bonus cache)

    // ---- terrain
    int biome(int x, int y);
    bool cliff(int x, int y);
    bool walkable(int x, int y, bool water, uint32_t ignore);
    uint8_t featOf(int x, int y);
    bool isCleared(int x, int y) const;
    void markCleared(int x, int y);
    void markFootprint(Kind k, int tx,
                       int ty); // a building's footprint never shows (or demands clearing of) features again

    // ---- relations
    bool sameClan(int a, int b) const;
    bool shares(int a, int b) const; // shared vision
    bool allied(int a, int b) const;
    float repOf(int player, int owner) const;
    bool hostile(int a, int b) const;
    bool factionsAtWar(int fa, int fb) const; // per world seed and faction_war_percent
    int ulv(int owner, Kind k) const;
    std::string ownerName(int o) const;
    int colorOf(int id) const { return (id - 1) % TEAM_COLORS; }
    std::vector<int> clanHalls(int victim); // clan mates that still have a Town Hall
    bool inClanRadiusOf(int id, Pos c);

    // ---- resources
    float totalRes(int id) const;
    float stockOf(entt::entity e) const;
    float capOf(entt::entity e) const;
    float storeCargo(entt::entity st,
                     float *cargo); // returns the amount stored (the hall discards cargo beyond a resource's share)
    float allocRoom(int id, int r); // room left under resource r's storage share (huge without a Town Hall)
    float creditRes(int id, int r, float amt);    // adds income within the storage share; returns the amount kept
    void noteDiscard(int id, int r, float lost);  // throttled "storage share full" message
    bool afford(int id, const float *cost) const; // pooled resources: own first, then allies
    void spend(int id, const float *cost);
    void resetProfile(int id);

    // ---- messaging
    void say(int id, const std::string &s);
    void reject(int id, uint32_t seq); // S_CmdReject: lets the client roll back a predicted command
    void fx(EffectType t, float x, float y, float x2 = 0, float y2 = 0);
    void notifyClan(int clan, const std::string &msg);
    void sendAlert(int to, uint8_t type, Pos at, const std::string &text);

    // ---- entity lookup / queries
    entt::entity find(uint32_t uid) const;
    Pos posOf(entt::entity e) const;
    float rectDist(Pos p, entt::entity e) const;
    bool complete(entt::entity e) const;
    entt::entity townHallOf(int id);
    Town *townById(int id);
    int unitCount(int id);
    int workersOf(uint32_t home);
    bool spawnSpot(entt::entity b, bool water, Pos &out);
    entt::entity nearestHostile(Pos p, int owner, float range, bool buildings);
    bool inactive(entt::entity e) const;       // non-functional building (outside territory or critically damaged)
    bool outOfTerritory(entt::entity e) const; // BLD_DISABLED: outside functional territory (relocatable)
    bool crippled(entt::entity e) const;       // BLD_CRIPPLED: complete player building below damage_critical_hp
    float efficiency(entt::entity e) const;    // damage efficiency 0..1 (production, training, storage, defence)
    bool siteBusy(entt::entity e) const;       // under construction / upgrading / repairing: no production or training
    int thLevel(int id);                       // level of the player's completed Town Hall (0 = none)
    int countKind(int id, Kind k);             // own buildings of a kind, including sites under construction
    uint8_t bflagsOf(entt::entity e) const;
    bool townMoveActive(int id);
    bool isPatrol(entt::entity e) const { return reg.all_of<Patrol>(e); }
    bool ephemeral(entt::entity e) const { return reg.all_of<Raider>(e) || reg.all_of<Patrol>(e); } // not persisted
    void releaseUnit(entt::entity e); // a player command takes the unit over

    // ---- fog of war
    bool bitAt(const std::unordered_map<int64_t, Bits> &m, int x, int y) const;
    bool explored(int id, int x, int y) const;
    void reveal(int id, int cx, int cy, int r);
    std::vector<Source> sourcesFor(int id);

    // ---- entity creation / destruction
    entt::entity makeBase(Kind k, uint8_t owner, uint32_t uid = 0);
    entt::entity makeUnit(Kind k, uint8_t owner, float x, float y);
    entt::entity makeBuilding(Kind k, uint8_t owner, int tx, int ty, bool done);
    void destroyNow(entt::entity e);
    void shipDestroyed(entt::entity ship); // passengers go down with the ship
    void removeOwner(int id);

    // ---- verification (Invariants.cpp): human-readable violations, empty when consistent
    std::vector<std::string> checkInvariants();

    // ---- pathfinding (A*, 8-dir, bounded, partial paths allowed)
    bool findPath(IV s, IV g, bool water, uint32_t ignore, std::vector<Pos> &out, int maxNodes = 7000);
    bool order(entt::entity e, Pos goal, uint32_t ignore = 0, int maxNodes = 7000);
};

// Adapter for the shared placement rules (checkPlace).
struct PlaceWorld
{
    World &s;
    int me;
    int biome(int x, int y) { return s.biome(x, y); }
    bool explored(int x, int y) { return s.explored(me, x, y); }
    bool occupied(int x, int y) { return s.occ.count(tkey(x, y)) > 0; }
    template <class F> void anchors(F f)
    {
        s.reg.view<Info, Building, Owner>().each([&](entt::entity e, const Info &i, const Building &b, const Owner &o)
        {
            if (hasTag(i.kind, KT_Anchor) && !s.inactive(e))
                f(footCenter(i.kind, b.tx, b.ty), i.kind, o.id == me, s.allied(me, o.id) && o.id != me);
        });
    }
    bool campfireNear(Pos c, float r)
    {
        bool hit = false;
        s.reg.view<Info, Building>().each([&](const Info &i, const Building &b)
        {
            if (!hasTag(i.kind, KT_Warm))
                return;
            Pos q = footCenter(i.kind, b.tx, b.ty);
            if (std::hypot(q.x - c.x, q.y - c.y) < r)
                hit = true;
        });
        return hit;
    }
    bool inClanRadius(Pos c) { return s.inClanRadiusOf(me, c); }
};
