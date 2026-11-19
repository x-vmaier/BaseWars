#pragma once
// Clearing of surface features (trees, bushes, rocks, gold veins) by pawns, buildings queued behind clearing, resource
// drops, gold-deposit mine bonus.
//
// A clear job belongs to one tile. A pawn (one of the pawns the player selected, else the nearest free pawn) is assigned,
// its previous work (Worker / move order) is stored in its ClearTask, it walks to the tile and the timer only runs while it
// stands there working. On completion the feature turns into resource drops and the pawn resumes its prior work (or chains
// to the next waiting job nearby). A building placed over features is a PendingBuild: placed once its tiles are cleared.
#include "server/World.hpp"

struct Systems;

class ClearingSystem
{
public:
    ClearingSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    std::vector<IV> blockedTiles(Kind k, int tx, int ty);                            // feature tiles under a footprint that still have to be cleared
    bool worldBuildOk(int id, Kind k, int tx, int ty);                               // relocation / town move hook: surface features must be cleared first
    void queueBuild(int id, Kind k, int tx, int ty, const std::vector<IV> &blocked); // C_Build over features: clear, then place
    bool inClearReach(int id, int x, int y);
    void onClear(int id, Reader &r);
    void sendDrop(Pos p, int res, int amt, int credited); // visible bouncing "+N" drop for everyone who can see the spot
    void creditDrop(int player, Pos p, int res, int amt);
    void sendClearedList(int id, const std::vector<int64_t> &tiles, bool full);
    void sendClearedFull(int id);
    void clearTick(float dt);
    void pawnTick(entt::entity e, Unit &u, ClearTask &ct, Pos &p, float dt); // unit loop: a pawn on a clear job
    void releasePawn(entt::entity e, bool manual);                           // ends a pawn's clear task and restores its previous work
    float mineMul(entt::entity e, Kind k, Pos c);                            // output multiplier from nearby gold deposits (cached per clear-version)
    int pendingOf(int id, Kind k) const;                                     // own buildings of a kind waiting for their site to be cleared

private:
    World &world;
    entt::registry &reg;
    Systems &s;
    struct ClearJob
    {
        int owner;
        uint8_t feat;
        float t, total;
        uint32_t pawn = 0;          // assigned pawn uid (0 = waiting for one)
        std::vector<uint32_t> want; // pawns the player picked for this job
        uint8_t state = 0;          // 0 waiting for a pawn, 1 pawn walking, 2 pawn working
        double since = 0;           // last time the job had a pawn (for the no-pawn timeout)
        int fails = 0;
        float paid = 0; // wood paid (refunded when the job is cancelled)
    };
    struct PendingBuild
    {
        int owner;
        Kind k;
        int tx, ty;
        float cost[R_Count];
        std::vector<IV> tiles;
    };
    std::unordered_map<int64_t, ClearJob> clearJobs;
    std::unordered_map<uint32_t, PendingBuild> pending;
    uint32_t nextPending = 1;
    std::unordered_map<uint32_t, double> manualUntil; // pawns the player just commanded are not auto-picked for a while
    bool jobsWereSent = false;
    std::unordered_map<uint32_t, std::pair<uint32_t, float>> mineCache;
    std::vector<Source> dropSrc[MAX_PLAYERS + 1];
    uint32_t dropSrcTick[MAX_PLAYERS + 1]{};

    bool freePawn(entt::entity e, int owner, bool explicitPick);
    entt::entity pickPawn(const ClearJob &j, Pos at);
    void assign(int64_t key, ClearJob &j, entt::entity pawn);
    void cancelJob(int64_t key, const std::string &why);
    void finishJob(int64_t key);
    void pendingTick();
    void sendJobs();
};
