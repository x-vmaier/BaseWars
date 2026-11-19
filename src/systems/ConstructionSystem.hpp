/**
 * @file ConstructionSystem.hpp
 * @brief Building sites: construction, upgrades (C_Upgrade) and repairs (C_Repair) are SiteJobs worked by a pawn.
 */
#pragma once
#include "common/Progression.hpp"
#include "server/World.hpp"

struct Systems;

class ConstructionSystem
{
  public:
    ConstructionSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    bool needsPawn(entt::entity e) const; // construction that waits for pawn work
    void tick(float dt);                  // create construction jobs, (re)assign pawns, S_Jobs
    void pawnTick(entt::entity e, Unit &u, BuildTask &bt, Pos &p, float dt); // unit loop: a pawn on a site job
    void releasePawn(entt::entity e, bool manual); // ends a pawn's site task and restores its previous work
    void upgrade(int id, entt::entity be);         // C_Upgrade
    void repairCmd(int id, Reader &r);             // C_Repair: start a repair / cancel the running upgrade or repair
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
