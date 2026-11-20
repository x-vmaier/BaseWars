/**
 * @file SpawnSystem.hpp
 * @brief Where new or defeated players found their base, plus the spawn test hooks.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class SpawnSystem
{
  public:
    SpawnSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    bool clearingOk(int cx, int cy);
    IV findClearing(const Pos *anchor = nullptr);
    // Where should a newcomer (or a player who lost their base) appear? Near Steam friends, then allies / clan mates,
    // then any player.
    bool spawnAnchor(int id, const std::vector<uint64_t> &friends, Pos &out);
    void spawnBase(int id, const std::vector<uint64_t> &friends = {});
    void foundBase(int id, IV clearing); // Town Hall, two pawns and a warrior around a clearing centre
    // Candidate site for a resettlement offer: anchors are friends, then clan mates / allies, then others; `attempt`
    // rotates through them. No player to settle near: any free clearing.
    IV offerSite(int id, const std::vector<uint64_t> &friends, int attempt);
    bool testSpawn(IV &c); // BW_SPAWN=x,y puts the first new base at the clearing nearest to that point
    void worldTestSpawn(int id, bool fresh); // BW_ARMY=<n>: a freshly spawned player also gets n extra warriors
    void baseTestInit(int id);               // BW_BASETEST: plenty of resources, level 3 troops

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
