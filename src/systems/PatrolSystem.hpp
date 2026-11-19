#pragma once
// Faction patrol groups: waypoints, attacks on sight, loot drops and retaliation against players.
#include "server/World.hpp"

struct Systems;
struct PatGroup {
    uint16_t town = 0;
    std::vector<uint32_t> m;                                                                              // member uids
    uint32_t leader = 0, tgt = 0;
    Pos wp{0, 0}, retPos{0, 0};
    bool hasWp = false, pending = true;
    int fails = 0, ret = 0;                                                                               // ret: player id being hunted (0 = peaceful patrol)
    double wait = 0, repath = 0, wpUntil = 0, respawnAt = 0, retUntil = 0;
};

class PatrolSystem {
public:
    PatrolSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    void worldOnKill(entt::entity e, Kind k, int owner, int killer, Pos p);   // processKills hook (victim still alive)
    void runPatrol(entt::entity e, Unit& u, Patrol& pt, float dt);
    void patrolSecond();

private:
    World& world;
    entt::registry& reg;
    Systems& s;
    PatGroup* groupOf(uint32_t id);
    void patrolLoot(Kind k, float mul, int killer, Pos p);
    void spawnPatrol(uint32_t gid, PatGroup& g);
    Pos groupPos(const PatGroup& g);
    bool pickWaypoint(PatGroup& g);
    std::unordered_map<uint32_t, PatGroup> pgroups;
    uint32_t nextGroup = 1;
};
