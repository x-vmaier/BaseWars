#pragma once
// Replicated world state on the client: ECS registry of server entities, terrain/fog chunks, roster, resources and the
// server-sent side tables (cleared tiles, clear jobs, training queues, ship details, path lines, base info).
// Also the read-only queries the renderers and UI share (terrain, diplomacy, names, placement features).
#include "client/ClientCommon.hpp"
#include "client/ClientState.hpp"
#include "client/Components.hpp"

#include <entt/entt.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ClientWorld
{
    // identity
    int me = 0; // our player id (0 until S_Welcome)
    uint32_t seed = 0;
    std::string myName; // display name (config / menu / profile / S_Welcome)

    // economy and diplomacy (S_State / S_Roster / S_Map)
    float res[R_Count]{};
    int facRep[FACTION_COUNT]{}, unitLvl[5]{1, 1, 1, 1, 1};
    std::vector<Peer> roster;
    std::vector<ClanInfo> clansList;
    std::vector<FacTown> facTowns;

    // terrain
    std::unordered_map<int64_t, Chunk> chunks;
    std::unordered_set<int64_t> occ;         // tiles covered by building footprints
    std::unordered_set<int64_t> clearedT;    // tiles whose feature was removed (synced from the server)
    std::unordered_map<int64_t, CJob> cjobs; // running clear jobs
    std::vector<PendingSite> pendingSites;   // own buildings queued behind clearing (S_ClearJobs)

    // entities
    entt::registry reg;
    std::unordered_map<uint32_t, entt::entity> ents; // server id -> entity
    uint32_t stamp = 0;

    // transient effects
    std::vector<Fx> fxs;
    std::vector<Ghost> ghosts;
    std::vector<Ripple> ripples;
    std::vector<Marker> markers;
    std::vector<DropFx> drops;

    // side tables
    std::unordered_map<uint32_t, QEntry> queues;             // building uid -> training queue (S_Queue)
    std::unordered_map<uint32_t, BJob> jobs;                 // building uid -> construction / upgrade / repair job (S_Jobs)
    double donateReadyAt = 0;                                // S_Econ: local time when donations / transfers are allowed again
    FacDemand demand[FACTION_COUNT];                         // S_Econ: what each faction wants
    std::unordered_map<uint32_t, ShipDetail> shipDetails;    // own ships (S_ShipInfo)
    std::unordered_map<uint32_t, std::vector<IV>> pathLines; // S_Paths
    int quota[QUOTA_TYPES]{3, 2, 0, 0}, movePct = 0;         // S_BaseInfo: defense quota per QUOTA_KINDS type
    int alloc[R_Count]{100, 100, 100, 100};                  // S_BaseInfo: Town Hall storage share per resource (%)
    double baseEditAt = -10;                                 // local edit time: ignore S_BaseInfo quota / shares briefly (no flicker)
    bool homeless = false, wasHomeless = false, townMoving = false;

    // ---- terrain
    int biomeAt(int x, int y) const;
    bool exploredAt(int x, int y) const;
    uint8_t featHere(int x, int y) const;    // surface feature (ignores clearing)
    bool featBlocking(int x, int y) const;   // uncleared blocking feature outside building footprints
    uint8_t featVisible(int x, int y) const; // explored, uncleared feature outside footprints (any kind, bushes too)
    std::vector<IV> blockedUnder(Kind k, IV t) const;
    int clearWoodCost(const std::vector<IV> &v) const;
    int goldNear(Pos c) const;               // gold deposits around a mine site
    bool coastNearShip(const RPos &p) const; // any walkable land within 2 tiles (same radius as the server)

    // ---- players and diplomacy
    const Peer *peerOf(int id) const;
    int myClan() const;
    bool alliedWith(int owner) const;
    bool hostileTo(int owner) const;
    std::string nameOf(int owner) const;
    std::string clanName(int c) const;
    int leaderClanOf(int owner) const; // the Town Hall of a clan's leader is its clan hall
    bool hostileTargetId(uint32_t id) const;
    bool attackable(uint32_t id) const;

    // ---- entities
    void rebuildOcc();
    bool visibleSource(float x, float y) const; // inside the vision of an allied entity
    bool nearStorage(float x, float y) const;   // within reach of a Town Hall / Outpost / Storage (convoys end there)
    Pos myTH(Vector2 camTarget) const;          // own Town Hall, or the camera position
    bool affordable(const float *cost) const;
    int thLevel() const;            // level of our completed Town Hall (0 = none)
    int countKind(Kind k) const;    // own buildings of a kind incl. sites and sites waiting for clearing
    int buildGateErr(Kind k) const; // GateErr for placing one more building of kind k
    float donateWait() const;       // seconds until the donation cooldown ends (0 = ready)
    const BJob *jobOf(uint32_t id) const;
    void clear();               // reset on join / leave (keeps the side tables)
    void interpolate(float dt); // ease rendered positions towards the server's, update facing
};
