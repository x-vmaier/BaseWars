#pragma once
// Building sites: construction, upgrades (C_Upgrade) and repairs (C_Repair) are SiteJobs worked by a pawn.
//
// A player building under construction (except a freshly founded Town Hall) gets a SiteJob automatically; upgrades and
// repairs are paid up front and add one. The nearest free pawn (idle pawns before workers, no range limit, so far-away
// Outposts get one too) is assigned, its previous work is stored in its BuildTask and it walks there normally (no
// teleport: hostiles can catch it on the way). The job timer only runs while the pawn stands at the site; if it leaves
// or dies the job pauses ("Waiting for a pawn") until another pawn takes over. While a building has a SiteJob (or is
// still under construction) it neither produces nor trains. Debug instant_build / instant_upgrade skip the pawn.
#include "common/Progression.hpp"
#include "server/World.hpp"

struct Systems;

class ConstructionSystem
{
public:
    ConstructionSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    bool needsPawn(entt::entity e) const;                                    // construction that waits for pawn work
    void tick(float dt);                                                     // create construction jobs, (re)assign pawns, S_Jobs
    void pawnTick(entt::entity e, Unit &u, BuildTask &bt, Pos &p, float dt); // unit loop: a pawn on a site job
    void releasePawn(entt::entity e, bool manual);                           // ends a pawn's site task and restores its previous work
    void upgrade(int id, entt::entity be);                                   // C_Upgrade
    void repairCmd(int id, Reader &r);                                       // C_Repair: start a repair / cancel the running upgrade or repair
    void sendJobs(int id);

private:
    World &world;
    entt::registry &reg;
    Systems &s;
    std::unordered_map<uint32_t, double> manualUntil; // pawns the player just commanded are not auto-picked for a while
    bool jobsWereSent[MAX_PLAYERS + 1]{};

    bool freePawn(entt::entity e, int owner) const;
    entt::entity pickPawn(int owner, Pos at);
    void assign(entt::entity site, SiteJob &j, entt::entity pawn);
    void work(entt::entity site, SiteJob &j, float dt); // the pawn at the site advances the job
    void finishUpgrade(entt::entity site);
    void endJob(entt::entity site); // remove the job and send its pawn back to its previous work
    void cancel(int id, entt::entity be);
    void repair(int id, entt::entity be);
};
