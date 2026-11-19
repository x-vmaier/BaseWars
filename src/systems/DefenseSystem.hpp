#pragma once
// Clan alerts, defense quota dispatch and support patrols.
#include "server/World.hpp"

struct Systems;
struct DefRoute { bool ok = false; float eta = 0; std::vector<Pos> path; };

class DefenseSystem {
public:
    DefenseSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    void baseOnDamage(entt::entity e, int owner, int attacker);   // from damage(): cheap gate, real work is deferred to flushAlerts()
    DefRoute planDefenseRoute(int member, Pos from, Pos to);
    bool idleArmy(entt::entity e, Kind only = K_Count);      // only: restrict to one troop type (monks count then too)
    std::vector<entt::entity> pickIdleArmy(int owner, Pos near2, int n, Kind only = K_Count);
    Pos homeOf(int id, Pos fallback);
    void sendPatrol(entt::entity e, Pos dest, int victim, bool defense, bool route = true);
    void dispatchDefense(int victim, Pos at, int attacker);
    void alertFriends(int victim, Pos at, int attacker);
    void flushAlerts();                                      // end of every tick: process deferred alerts
    void doSupport(int id, Pos dest, int n);                 // manual support patrol (big map right-click on an allied town)
    void supportSecond();

private:
    World& world;
    entt::registry& reg;
    Systems& s;
    struct PendingAlert { int victim; Pos at; int attacker; };
    std::vector<PendingAlert> pendingAlerts;
};
