/**
 * @file PatrolSystem.hpp
 * @brief Faction patrol groups: waypoints, attacks on sight, loot drops and retaliation against players.
 */
#pragma once
#include "server/World.hpp"

struct Systems;
struct PatGroup
{
    uint16_t town = 0;
    std::vector<uint32_t> m; // member uids
    uint32_t leader = 0, tgt = 0;
    Pos wp{0, 0}, retPos{0, 0};
    bool hasWp = false, pending = true;
    bool roam = false, siege = false; // roam: map-wide group; siege: marching on a hostile town
    int fails = 0, ret = 0;           // ret: player id being hunted (0 = peaceful patrol)
    double wait = 0, repath = 0, wpUntil = 0, respawnAt = 0, retUntil = 0;
};

class PatrolSystem
{
  public:
    PatrolSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void worldOnKill(entt::entity e, Kind k, int owner, int killer, Pos p); // processKills hook (victim still alive)
    void runPatrol(entt::entity e, Unit &u, Patrol &pt, float dt); // movement only; combat is the normal combat() pass
    void patrolSecond();

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
    PatGroup *groupOf(uint32_t id);
    void patrolLoot(Kind k, float mul, int killer, Pos p);
    void spawnPatrol(uint32_t gid, PatGroup &g);
    Pos groupPos(const PatGroup &g);
    bool pickWaypoint(PatGroup &g);
    bool pickSiege(PatGroup &g, Pos from, int owner); // a hostile town (warring faction or hostile player) in range
    bool exploredTile(IV &out);                       // random walkable tile in land some player has explored
    Town *townNear(Pos p);                            // nearest living NPC town
    void manageRoamers(); // keeps cfg().patrolMapGroups wandering groups per 100 explored chunks
    void retaliateStep(entt::entity e, Unit &u, Patrol &pt, PatGroup &g, Pos p, uint32_t uid);
    void leaderStep(entt::entity e, Unit &u, PatGroup &g, Pos p);
    void followerStep(entt::entity e, Unit &u, Patrol &pt, PatGroup &g, Pos p);
    void ensureTownGroups(); // every living town owns its patrol groups
    void tickGroup(uint32_t gid, PatGroup &g);
    void reactToAttackers(PatGroup &g, int owner);
    void attackOnSight(PatGroup &g, entt::entity leader, Pos lp, int owner);
    void updateRetaliation(PatGroup &g, Pos lp);
    void nearestToMembers(const PatGroup &g, Pos q, uint32_t uid, float &bestNear, uint32_t &nearUid, Pos &nearPos);
    int roamWant = 0;
    double roamCount = -100;
    std::unordered_map<uint32_t, PatGroup> pgroups;
    uint32_t nextGroup = 1;
};
