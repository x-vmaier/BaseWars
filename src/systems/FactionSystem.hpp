#pragma once
// NPC faction reputation, town self-defense, garrison AI and help for friendly players, donations and town demand.
//
// Donations (C_Donate to a faction, C_Transfer to a clan mate) share one per-player cooldown (donation_cooldown).
// Each faction's towns want one resource at a time; it changes every demand_change_min..max seconds, or as soon as
// demand_satisfy of it was given. Gifts of the wanted resource earn demand_bonus x reputation; repeated gifts of the same
// resource earn less (1 / (1 + recent / donation_diminish), the memory fades over donation_memory seconds).
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
