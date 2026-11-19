/**
 * @file FactionSystem.hpp
 * @brief NPC faction reputation, town self-defense, garrison AI and help for friendly players, donations and town
 * demand.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class FactionSystem
{
  public:
    FactionSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void addRep(int player, int f, float d);
    void alertTown(entt::entity victim, uint32_t by); // a faction town defends itself against whoever hit it
    void factionAI(entt::entity e, Unit &u, Pos &p, float);
    void callHelp();                                   // friendly factions send troops when a player is attacked
    bool donationReady(int id);                        // false (and tells the player) while the cooldown runs
    void donated(int id);                              // start the cooldown
    void donate(int id, int f, int res, float amount); // C_Donate
    float giftMul(int id, int f, int res) const;       // reputation multiplier of the next gift
    void demandSecond();                               // rotate demands, fade gift memories, S_Econ

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
