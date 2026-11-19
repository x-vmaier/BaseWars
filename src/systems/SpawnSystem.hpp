#pragma once
// Where new (or defeated) players found their base, plus the spawn-related test hooks (BW_SPAWN, BW_ARMY, BW_HURT, BW_BASETEST).
#include "server/World.hpp"

struct Systems;

class SpawnSystem
{
public:
    SpawnSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    bool clearingOk(int cx, int cy);
    IV findClearing(const Pos *anchor = nullptr);
    // Where should a newcomer (or a player who lost their base) appear? Near Steam friends, then allies / clan mates, then any player.
    bool spawnAnchor(int id, const std::vector<uint64_t> &friends, Pos &out);
    void spawnBase(int id, const std::vector<uint64_t> &friends = {});
    bool testSpawn(IV &c);                   // BW_SPAWN=x,y puts the first new base at the clearing nearest to that point
    void worldTestSpawn(int id, bool fresh); // BW_ARMY=<n>: a freshly spawned player also gets n extra warriors
    void baseTestInit(int id);               // BW_BASETEST: plenty of resources, level 3 troops

private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
