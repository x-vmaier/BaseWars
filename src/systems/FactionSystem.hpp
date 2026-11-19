#pragma once
// NPC faction reputation, town self-defense, garrison AI and help for friendly players.
#include "server/World.hpp"

struct Systems;

class FactionSystem {
public:
    FactionSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    void addRep(int player, int f, float d);
    void alertTown(entt::entity victim, uint32_t by);       // a faction town defends itself against whoever hit it
    void factionAI(entt::entity e, Unit& u, Pos& p, float);
    void callHelp();                                         // friendly factions send troops when a player is attacked

private:
    World& world;
    entt::registry& reg;
    Systems& s;
};
